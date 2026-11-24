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

#include <string.h>

#include "hardware/pio_instructions.h"

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

/* Init the camera SM and make GPIO18 active-LOW: idle HIGH, pulse starts on the
 * falling edge, ends on the rising edge. The invert MUST be set after the PIO
 * init — pio_gpio_init() → gpio_set_function() rewrites the whole GPIO ctrl
 * register and clears OUTOVER, so re-applying it here keeps the camera inverted
 * across every mode switch re-init. Strobe is left active-HIGH.
 * gpio_trigger selects the program: false = delayed_pulse (IRQ 2 from burst),
 * true = delayed_pulse_gpio (edge on ADE_TRIGGER_IN). */
static void ade_camera_init(float delayed_div, bool gpio_trigger) {
    if (gpio_trigger)
        delayed_pulse_gpio_init(pio_ade, sm_camera, offset_delayed_pulse,
                                ADE_CAMERA_TRIGGER, ADE_TRIGGER_IN, delayed_div);
    else
        delayed_pulse_init(pio_ade, sm_camera, offset_delayed_pulse,
                           ADE_CAMERA_TRIGGER, delayed_div);
    gpio_set_outover(ADE_CAMERA_TRIGGER, GPIO_OVERRIDE_INVERT);
}

static void ade_strobe_init(float delayed_div, bool gpio_trigger) {
    if (gpio_trigger)
        delayed_pulse_gpio_init(pio_ade, sm_strobe, offset_delayed_pulse,
                                ADE_STROBE_TRIGGER, ADE_TRIGGER_IN, delayed_div);
    else
        delayed_pulse_init(pio_ade, sm_strobe, offset_delayed_pulse,
                           ADE_STROBE_TRIGGER, delayed_div);
}

static float ade_delayed_div(void) {
    return (float)clock_get_hz(clk_sys) / (float)ADE_CLK;
}

/* Trigger-in pad: input with pull-down; falling-edge mode inverts the input
 * path so the PIO program (which waits for 0 then 1) sees a falling edge as a
 * rising one. Must be applied after gpio_init (gpio_set_function clears INOVER). */
static void ade_trigger_pin_apply(void) {
    gpio_set_inover(ADE_TRIGGER_IN, config.trigger_falling
                    ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
}

void ade_init(void) {
    float delayed_div = ade_delayed_div();

    pio_ade = pio1;
    sm_camera = pio_claim_unused_sm(pio_ade, true);
    sm_strobe  = pio_claim_unused_sm(pio_ade, true);
    offset_delayed_pulse = pio_add_program(pio_ade, &delayed_pulse_program);
    ade_camera_init(delayed_div, false);
    ade_strobe_init(delayed_div, false);

    gpio_init(ADE_TRIGGER_IN);
    gpio_set_dir(ADE_TRIGGER_IN, GPIO_IN);
    gpio_pull_down(ADE_TRIGGER_IN);

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
        .trigger_falling = false,
    };
    current_mode = ADE_MODE_CALIBRATION;
}

//---------------------------------------------------------------------------
// MODE SWITCH
//---------------------------------------------------------------------------

/* Trigger mode → calibration state: swap delayed_pulse_gpio back to
 * delayed_pulse on the camera/strobe SMs. Pulser stays loaded throughout. */
static void ade_trigger_to_calibration(void) {
    pio_sm_set_enabled(pio_ade, sm_camera, false);
    pio_sm_set_enabled(pio_ade, sm_strobe,  false);
    pio_remove_program(pio_ade, &delayed_pulse_gpio_program, offset_delayed_pulse);

    float delayed_div = ade_delayed_div();
    offset_delayed_pulse = pio_add_program(pio_ade, &delayed_pulse_program);
    ade_camera_init(delayed_div, false);
    ade_strobe_init(delayed_div, false);
    gpio_set_inover(ADE_TRIGGER_IN, GPIO_OVERRIDE_NORMAL);

    current_mode = ADE_MODE_CALIBRATION;
}

/* Ejection mode → calibration state: drop burst programs, restore pulser. */
static void ade_ejection_to_calibration(void) {
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

    float delayed_div = ade_delayed_div();
    offset_delayed_pulse = pio_add_program(pio_ade, &delayed_pulse_program);
    ade_camera_init(delayed_div, false);
    ade_strobe_init(delayed_div, false);

    current_mode = ADE_MODE_CALIBRATION;
}

/* Trigger mode: (re)load delay/width into the enabled camera/strobe SMs and
 * start them in sync. Called on arm and whenever a setting changes. */
static void ade_trigger_load_and_start(void) {
    uint32_t mask = 0;
    if (config.camera_enabled) {
        delayed_pulse_load(pio_ade, sm_camera, offset_delayed_pulse,
                           us_to_delayed_ticks(config.camera_trigger),
                           us_to_delayed_ticks(config.camera_exposure));
        mask |= 1u << sm_camera;
    }
    if (config.strobe_enabled) {
        delayed_pulse_load(pio_ade, sm_strobe, offset_delayed_pulse,
                           us_to_delayed_ticks(config.strobe_trigger),
                           us_to_delayed_ticks(config.strobe_width));
        mask |= 1u << sm_strobe;
    }
    if (mask == 0) {
        printf("ade: trigger armed but camera and strobe both disabled\n");
        return;
    }
    pio_enable_sm_mask_in_sync(pio_ade, mask);
    if (config.log_enabled)
        printf("ade: trigger listening on GPIO%u (%s edge) | "
               "cam %s trig=%u exp=%u us | strob %s trig=%u w=%u us\n",
               ADE_TRIGGER_IN, config.trigger_falling ? "falling" : "rising",
               config.camera_enabled ? "on" : "off",
               config.camera_trigger, config.camera_exposure,
               config.strobe_enabled ? "on" : "off",
               config.strobe_trigger, config.strobe_width);
}

void ade_arm_ejection(const char *args) {
    if (current_mode == ADE_MODE_EJECTION) {
        printf("ade: already in ejection mode\n");
        return;
    }
    if (current_mode == ADE_MODE_TRIGGER)
        ade_trigger_to_calibration();

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

    float delayed_div = ade_delayed_div();
    float burst_div   = (float)clock_get_hz(clk_sys) / (float)ADE_BURST_CLK;

    /* Load in order: delayed_pulse → 0-9, burst_drive → 10-23, burst_gate → 24-31. */
    offset_delayed_pulse = pio_add_program(pio_ade, &delayed_pulse_program);
    offset_burst_drive   = pio_add_program(pio_ade, &burst_drive_program);
    offset_burst_gate    = pio_add_program(pio_ade, &burst_gate_program);

    ade_camera_init(delayed_div, false);
    ade_strobe_init(delayed_div, false);

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
    if (current_mode == ADE_MODE_EJECTION)
        ade_ejection_to_calibration();
    else
        ade_trigger_to_calibration();
    printf("ade: calibration mode armed\n");
}

void ade_arm_trigger(const char *args) {
    if (current_mode == ADE_MODE_TRIGGER) {
        printf("ade: already in trigger mode\n");
        return;
    }
    if (current_mode == ADE_MODE_EJECTION)
        ade_ejection_to_calibration();

    /* Calibration state: pulser 0-16, delayed_pulse 17-26. Swap delayed_pulse
     * (10) for delayed_pulse_gpio (11) → 17-27, 28/32 used. Ejection programs
     * cannot coexist (that mode already fills all 32 slots). */
    pio_sm_set_enabled(pio_ade, sm_camera, false);
    pio_sm_set_enabled(pio_ade, sm_strobe,  false);
    pio_remove_program(pio_ade, &delayed_pulse_program, offset_delayed_pulse);

    float delayed_div = ade_delayed_div();
    offset_delayed_pulse = pio_add_program(pio_ade, &delayed_pulse_gpio_program);
    ade_camera_init(delayed_div, true);
    ade_strobe_init(delayed_div, true);
    ade_trigger_pin_apply();

    current_mode = ADE_MODE_TRIGGER;
    printf("ade: trigger mode armed (GPIO%u, %s edge, dp@%u)\n", ADE_TRIGGER_IN,
           config.trigger_falling ? "falling" : "rising", offset_delayed_pulse);
    ade_trigger_load_and_start();
}

//---------------------------------------------------------------------------
// FIRE
//---------------------------------------------------------------------------

void ade_fire(const char *args) {
    if (current_mode != ADE_MODE_EJECTION) {
        printf("ade: run 'arm eject' first%s\n",
               current_mode == ADE_MODE_TRIGGER
               ? " (trigger mode fires on GPIO edge, not 'fire')" : "");
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
    if (config.log_enabled)
        printf("ade: fire mask=0x%02x cam_en=%d(sm%u) strob_en=%d(sm%u)\n",
               (unsigned)mask, (int)config.camera_enabled, sm_camera,
               (int)config.strobe_enabled, sm_strobe);
    pio_enable_sm_mask_in_sync(pio_ade, mask);

    /* Wait for burst_gate IRQ 5 (signals full ejection + damp complete).
     * Watchdog disabled: busy-wait with no deadline. If IRQ 5 never fires,
     * this hangs forever (recover via BOOTSEL / reboot-dfu). */
    while (!pio_interrupt_get(pio_ade, 5)) {
        tight_loop_contents();
    }
    pio_interrupt_clear(pio_ade, 5);
    pio_sm_set_enabled(pio_ade, sm_burst_drive, false);
    pio_sm_set_enabled(pio_ade, sm_burst_gate,  false);
    /* camera and strobe SMs continue running to complete their pulses;
     * delayed_pulse_load() on the next fire() disables and resets them. */

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

void burstcount_set(const char *args) {
    uint n = (uint)atoi(args);
    if (n == 0) { printf("burst count must be >= 1\n"); return; }
    config.burst_count = n;
    printf("burst_count=%u\n", config.burst_count);
}

void burstfreq_set(const char *args) {
    uint f = (uint)atoi(args);
    if (f == 0) { printf("burst freq must be >= 1 Hz\n"); return; }
    uint32_t half = ADE_BURST_CLK / (2u * f);
    if (half < 4u) { printf("freq too high (max ~%u Hz)\n", ADE_BURST_CLK / 8u); return; }
    config.burst_freq = f;
    printf("burst_freq=%u Hz\n", config.burst_freq);
}

void camtrig_set(const char *args) {
    config.camera_trigger  = (uint)atoi(args);
    printf("camtrig=%u us\n", config.camera_trigger);
    if (current_mode == ADE_MODE_TRIGGER) ade_trigger_load_and_start();
}

void camexp_set(const char *args) {
    config.camera_exposure = (uint)atoi(args);
    printf("camexp=%u us\n", config.camera_exposure);
    if (current_mode == ADE_MODE_TRIGGER) ade_trigger_load_and_start();
}

void strobtrig_set(const char *args) {
    config.strobe_trigger  = (uint)atoi(args);
    printf("strobtrig=%u us\n", config.strobe_trigger);
    if (current_mode == ADE_MODE_TRIGGER) ade_trigger_load_and_start();
}

void strobwidth_set(const char *args) {
    config.strobe_width    = (uint)atoi(args);
    printf("strobwidth=%u us\n", config.strobe_width);
    if (current_mode == ADE_MODE_TRIGGER) ade_trigger_load_and_start();
}

void camera_enable(const char *args) {
    config.camera_enabled = true;
    printf("ade: camera enabled\n");
    if (current_mode == ADE_MODE_TRIGGER) ade_trigger_load_and_start();
}

void strobe_enable(const char *args) {
    config.strobe_enabled = true;
    printf("ade: strobe enabled\n");
    if (current_mode == ADE_MODE_TRIGGER) ade_trigger_load_and_start();
}

void trigedge_set(const char *args) {
    while (*args == ' ') args++;
    if (strncmp(args, "rising", 6) == 0) {
        config.trigger_falling = false;
    } else if (strncmp(args, "falling", 7) == 0) {
        config.trigger_falling = true;
    } else {
        printf("usage: set trigedge rising|falling\n");
        return;
    }
    printf("trigedge=%s\n", config.trigger_falling ? "falling" : "rising");
    if (current_mode == ADE_MODE_TRIGGER) {
        ade_trigger_pin_apply();
        ade_trigger_load_and_start();   /* re-arm so a stale half-edge is discarded */
    }
}

void logging_enable(const char *args) {
    config.log_enabled = true;
    printf("ade: logging enabled\n");
}
