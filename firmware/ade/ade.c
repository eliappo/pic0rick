//!
//! \file       ade.c
//! \author     Elias Illeris Poggi
//! \date       2026-10-05
//!
//! \brief      Controls for Acoustic Droplet Ejection.
//!
//! Built only when -DADE=ON. See ade.h for mode descriptions.
//!

//---------------------------------------------------------------------------
// INCLUDES
//---------------------------------------------------------------------------

#include "ade.h"

#include "u4rk.h"
#include "acquisition.h"

#include "delayed_pulse.pio.h"
#include "burst_drive.pio.h"
#include "burst_gate.pio.h"

//---------------------------------------------------------------------------
// MODULE STATE
//---------------------------------------------------------------------------

static ade_config_t config;
static ade_mode_t   current_mode;
static PIO          pio_ade;

/* Camera and strobe SMs claimed at ade_init(), held for both modes. */
static uint sm_camera;
static uint sm_strobe;
static uint offset_delayed_pulse;

/* Burst SMs claimed only during ejection mode. */
static uint sm_burst_drive;
static uint sm_burst_gate;
static uint offset_burst_drive;
static uint offset_burst_gate;

//---------------------------------------------------------------------------
// HELPERS
//---------------------------------------------------------------------------

/* Convert µs value to delayed_pulse ticks (ADE_CLK-based), min 4. */
static uint32_t us_to_delayed_ticks(uint us) {
    uint32_t t = (uint32_t)((float)us * (ADE_CLK / 1000000.0f));
    return t < 4u ? 4u : t;
}

/* Convert µs value to burst ticks (ADE_BURST_CLK-based), min 4. */
static uint32_t us_to_burst_ticks(uint us) {
    uint32_t t = (uint32_t)((float)us * (ADE_BURST_CLK / 1000000.0f));
    return t < 4u ? 4u : t;
}

//---------------------------------------------------------------------------
// INIT
//---------------------------------------------------------------------------

void ade_init(void) {
    float delayed_div = (float)clock_get_hz(clk_sys) / (float)ADE_CLK;

    pio_ade = pio1;
    sm_camera = pio_claim_unused_sm(pio_ade, true);
    sm_strobe  = pio_claim_unused_sm(pio_ade, true);
    offset_delayed_pulse = pio_add_program(pio_ade, &delayed_pulse_program);
    delayed_pulse_init(pio_ade, sm_camera, offset_delayed_pulse,
                       ADE_CAMERA_TRIGGER, delayed_div);
    delayed_pulse_init(pio_ade, sm_strobe,  offset_delayed_pulse,
                       ADE_STROBE_TRIGGER, delayed_div);

    config = (ade_config_t){
        .camera_exposure = 1000,
        .camera_trigger  = 0,
        .strobe_width    = 100,
        .strobe_trigger  = 0,
        .burst_count     = 20,
        .burst_freq      = 1000000,
        .camera_enabled  = false,
        .strobe_enabled  = false,
        .log_enabled     = false,
    };
    current_mode = ADE_MODE_CALIBRATION;
}

//---------------------------------------------------------------------------
// MODE SWITCH
//---------------------------------------------------------------------------

void ade_arm_ejection(const char *args) {
    if (current_mode == ADE_MODE_EJECTION) {
        printf("ade: already in ejection mode\n");
        return;
    }

    u4rk_acquisition_release_pulser();

    /* After releasing the pulser (17 instr at 0-16) the freed slots are 0-16
     * and delayed_pulse (10 instr) sits at 17-26.  That leaves only two
     * non-contiguous free regions (0-16 and 27-31) which cannot fit
     * burst_gate (8 instr) after burst_drive (14 instr) takes 0-13.
     * Fix: remove delayed_pulse and reload everything from scratch so that
     * delayed_pulse(10)+burst_drive(14)+burst_gate(8) = 32 fills pio1 cleanly. */
    pio_sm_set_enabled(pio_ade, sm_camera, false);
    pio_sm_set_enabled(pio_ade, sm_strobe,  false);
    pio_remove_program(pio_ade, &delayed_pulse_program, offset_delayed_pulse);

    float delayed_div = (float)clock_get_hz(clk_sys) / (float)ADE_CLK;
    float burst_div   = (float)clock_get_hz(clk_sys) / (float)ADE_BURST_CLK;

    /* Load in order: delayed_pulse → 0-9, burst_drive → 10-23, burst_gate → 24-31. */
    offset_delayed_pulse = pio_add_program(pio_ade, &delayed_pulse_program);
    offset_burst_drive   = pio_add_program(pio_ade, &burst_drive_program);
    offset_burst_gate    = pio_add_program(pio_ade, &burst_gate_program);

    delayed_pulse_init(pio_ade, sm_camera, offset_delayed_pulse,
                       ADE_CAMERA_TRIGGER, delayed_div);
    delayed_pulse_init(pio_ade, sm_strobe,  offset_delayed_pulse,
                       ADE_STROBE_TRIGGER, delayed_div);

    sm_burst_drive = pio_claim_unused_sm(pio_ade, true);
    sm_burst_gate  = pio_claim_unused_sm(pio_ade, true);
    burst_drive_init(pio_ade, sm_burst_drive, offset_burst_drive,
                     ADE_OUTPUT_P_POS, burst_div);
    burst_gate_init(pio_ade, sm_burst_gate, offset_burst_gate,
                    U4RK_PULSER_GATE_PIN_BASE, burst_div);

    current_mode = ADE_MODE_EJECTION;
    printf("ade: ejection mode armed (drive@%u gate@%u dp@%u)\n",
           offset_burst_drive, offset_burst_gate, offset_delayed_pulse);
}

void ade_arm_calibration(const char *args) {
    if (current_mode == ADE_MODE_CALIBRATION) {
        printf("ade: already in calibration mode\n");
        return;
    }

    pio_sm_set_enabled(pio_ade, sm_burst_drive, false);
    pio_sm_set_enabled(pio_ade, sm_burst_gate,  false);
    pio_sm_set_enabled(pio_ade, sm_camera,       false);
    pio_sm_set_enabled(pio_ade, sm_strobe,        false);
    pio_sm_unclaim(pio_ade, sm_burst_drive);
    pio_sm_unclaim(pio_ade, sm_burst_gate);

    /* Remove all ejection-mode programs to free the full 32-slot space. */
    pio_remove_program(pio_ade, &burst_gate_program,    offset_burst_gate);
    pio_remove_program(pio_ade, &burst_drive_program,   offset_burst_drive);
    pio_remove_program(pio_ade, &delayed_pulse_program, offset_delayed_pulse);

    /* Reclaim pulser → loads at 0-16; then delayed_pulse lands at 17-26. */
    u4rk_acquisition_reclaim_pulser();

    float delayed_div = (float)clock_get_hz(clk_sys) / (float)ADE_CLK;
    offset_delayed_pulse = pio_add_program(pio_ade, &delayed_pulse_program);
    delayed_pulse_init(pio_ade, sm_camera, offset_delayed_pulse,
                       ADE_CAMERA_TRIGGER, delayed_div);
    delayed_pulse_init(pio_ade, sm_strobe,  offset_delayed_pulse,
                       ADE_STROBE_TRIGGER, delayed_div);

    current_mode = ADE_MODE_CALIBRATION;
    printf("ade: calibration mode armed\n");
}

//---------------------------------------------------------------------------
// FIRE
//---------------------------------------------------------------------------

void ade_fire(const char *args) {
    if (current_mode != ADE_MODE_EJECTION) {
        printf("ade: run 'arm eject' first\n");
        return;
    }
    if (config.burst_count == 0 || config.burst_freq == 0) {
        printf("ade: burst_count and burst_freq must be non-zero\n");
        return;
    }

    uint32_t cam_delay   = us_to_delayed_ticks(config.camera_trigger);
    uint32_t cam_width   = us_to_delayed_ticks(config.camera_exposure);
    uint32_t strob_delay = us_to_delayed_ticks(config.strobe_trigger);
    uint32_t strob_width = us_to_delayed_ticks(config.strobe_width);

    uint32_t half_period = (uint32_t)(ADE_BURST_CLK / (2u * config.burst_freq));
    if (half_period < 4u) half_period = 4u;
    uint32_t damp_ticks = us_to_burst_ticks(ADE_DAMP_US);

    /* Clear IRQ flags from any previous fire. */
    pio_interrupt_clear(pio_ade, 2);
    pio_interrupt_clear(pio_ade, 4);
    pio_interrupt_clear(pio_ade, 5);

    if (config.camera_enabled)
        delayed_pulse_load(pio_ade, sm_camera, offset_delayed_pulse,
                           cam_delay, cam_width);
    if (config.strobe_enabled)
        delayed_pulse_load(pio_ade, sm_strobe, offset_delayed_pulse,
                           strob_delay, strob_width);

    /* Load burst_gate first (single word), then burst_drive (two words).
     * Both SMs are currently disabled (stalled/freshly inited); putting data
     * in their FIFOs is safe before sync-enable. */
    pio_sm_put(pio_ade, sm_burst_gate,  damp_ticks - 3u);
    pio_sm_put(pio_ade, sm_burst_drive, config.burst_count - 1u);
    pio_sm_put(pio_ade, sm_burst_drive, half_period - 3u);

    uint32_t mask = (1u << sm_burst_drive) | (1u << sm_burst_gate);
    if (config.camera_enabled) mask |= (1u << sm_camera);
    if (config.strobe_enabled) mask |= (1u << sm_strobe);
    pio_enable_sm_mask_in_sync(pio_ade, mask);

    /* Wait for burst_gate IRQ 5 (signals full ejection + damp complete). */
    uint64_t deadline = time_us_64() + ADE_FIRE_TIMEOUT_US;
    while (!pio_interrupt_get(pio_ade, 5)) {
        if (time_us_64() > deadline) {
            printf("ade: fire timeout — pio_irq=0x%02x "
                   "drive_pc=%u(base=%u) gate_pc=%u(base=%u)\n",
                   (unsigned)(pio_ade->irq & 0xFF),
                   (unsigned)(pio_ade->sm[sm_burst_drive].addr),
                   (unsigned)offset_burst_drive,
                   (unsigned)(pio_ade->sm[sm_burst_gate].addr),
                   (unsigned)offset_burst_gate);
            pio_sm_set_enabled(pio_ade, sm_burst_drive, false);
            pio_sm_set_enabled(pio_ade, sm_burst_gate, false);
            if (config.camera_enabled)
                pio_sm_set_enabled(pio_ade, sm_camera, false);
            if (config.strobe_enabled)
                pio_sm_set_enabled(pio_ade, sm_strobe, false);
            return;
        }
    }
    pio_interrupt_clear(pio_ade, 5);
    pio_sm_set_enabled(pio_ade, sm_burst_drive, false);
    pio_sm_set_enabled(pio_ade, sm_burst_gate,  false);
    if (config.camera_enabled)
        pio_sm_set_enabled(pio_ade, sm_camera, false);
    if (config.strobe_enabled)
        pio_sm_set_enabled(pio_ade, sm_strobe, false);

    if (config.log_enabled) {
        printf("ade: fire done — %u cycles @ %u Hz | "
               "cam trig=%u exp=%u us | strob trig=%u w=%u us\n",
               config.burst_count, config.burst_freq,
               config.camera_trigger, config.camera_exposure,
               config.strobe_trigger, config.strobe_width);
    } else {
        printf("ade: fire done\n");
    }
}

//---------------------------------------------------------------------------
// CONFIGURATION SETTERS
//---------------------------------------------------------------------------

void camtrig_set(const char *args) {
    config.camera_trigger  = (uint)atoi(args);
    printf("camtrig=%u us\n", config.camera_trigger);
}

void camexp_set(const char *args) {
    config.camera_exposure = (uint)atoi(args);
    printf("camexp=%u us\n", config.camera_exposure);
}

void strobtrig_set(const char *args) {
    config.strobe_trigger  = (uint)atoi(args);
    printf("strobtrig=%u us\n", config.strobe_trigger);
}

void strobwidth_set(const char *args) {
    config.strobe_width    = (uint)atoi(args);
    printf("strobwidth=%u us\n", config.strobe_width);
}

void camera_enable(const char *args) {
    config.camera_enabled = true;
    printf("ade: camera enabled\n");
}

void strobe_enable(const char *args) {
    config.strobe_enabled = true;
    printf("ade: strobe enabled\n");
}

void logging_enable(const char *args) {
    config.log_enabled = true;
    printf("ade: logging enabled\n");
}
