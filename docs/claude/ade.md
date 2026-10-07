# ADE — Acoustic Droplet Ejection module

**READ THIS FILE at the start of every ADE session. UPDATE it before ending.**

Branch: `acoustic-droplet-ejection-hack`
Build check: `cd firmware && cmake --preset ade && cmake --build build-ade 2>&1 | head -40`

---

## What ADE Does

Plug-in board on top of the pulse-echo board. Two operational modes share the
same transducer GPIOs (11/12 drive, 16/17 gate):

1. **Calibration** — standard pulse-echo imaging to find transducer-to-liquid
   distance. Uses existing REPL `start acq` / `read` commands unmodified.
2. **Ejection** — sinusoidal burst drives transducer N cycles at a set frequency.
   Camera and strobe triggers fire synchronously with configurable delays.

Enabled at build time with `-DADE=ON` (see `firmware/CMakePresets.json` preset `ade`).

---

## Pin & State Machine Assignment

### Calibration mode (default at boot)
| SM | Program | Pins | Purpose |
|----|---------|------|---------|
| pio1 SM0 | `u4rk_pulser` (drive) | GPIO11/12 | P+/P- drive |
| pio1 SM1 | `u4rk_pulser` (gate) | GPIO16/17 | OE/PDAMP |
| pio0 SM | `u4rk_adc` | GPIO0–10 | ADC capture |
| pio1 SM2 | `delayed_pulse` | GPIO18 | Camera trigger (init at boot) |
| pio1 SM3 | `delayed_pulse` | GPIO19 | Strobe trigger (init at boot) |

### Ejection mode (after `arm eject`)
| SM | Program | Pins | Purpose |
|----|---------|------|---------|
| pio1 SM0 | `burst_drive` | GPIO11/12 | Bipolar burst N cycles |
| pio1 SM1 | `burst_gate` | GPIO16/17 | OE during burst + PDAMP after |
| pio1 SM2 | `delayed_pulse` | GPIO18 | Camera trigger |
| pio1 SM3 | `delayed_pulse` | GPIO19 | Strobe trigger |

Mode switch: `arm eject` releases SM0/SM1 from pulser, loads burst programs.
`arm calibration` reverses this.

---

## Mode Switch Mechanism

Switching requires swapping PIO programs on pio1 SM0/SM1 (pulser ↔ burst).
Two functions added to `hw/acquisition.c`:
- `u4rk_acquisition_release_pulser()` — disables SM0/SM1, unclaims, removes program
- `u4rk_acquisition_reclaim_pulser()` — re-claims SM0/SM1, reloads pulser program

These are the ONLY modifications to files outside `firmware/ade/`.

---

## Files

| File | Status | Notes |
|------|--------|-------|
| `firmware/ade/ade.h` | complete | constants, `ade_config_t`, `ade_mode_t`, all declarations |
| `firmware/ade/ade.c` | complete | all functions implemented and hardware-verified |
| `firmware/ade/delayed_pulse.pio` | complete | camera + strobe pulse gen (IRQ 2 trigger) |
| `firmware/ade/burst_drive.pio` | complete | 14-instruction bipolar burst; fires IRQ 2 + IRQ 4 |
| `firmware/ade/burst_gate.pio` | complete | 8-instruction OE+PDAMP gate; fires IRQ 5 |
| `firmware/burst.pio` | stale (repo root) | original scaffolding; can be removed |
| `firmware/CMakeLists.txt` | complete | ADE block added (mirrors MUX pattern) |
| `firmware/CMakePresets.json` | complete | `ade` preset: pico, ADE=ON, MUX=OFF |
| `firmware/hw/acquisition.h/.c` | complete | `release_pulser` + `reclaim_pulser` added |
| `firmware/main.c` | complete | 12 ADE REPL commands wired under `#ifdef ADE` |

---

## ade_config_t Struct (ade.h)

```c
typedef struct {
    uint camera_exposure;   // camera pulse width (µs)
    uint camera_trigger;    // camera delay from burst start (µs)
    uint strobe_width;      // strobe pulse width (µs)
    uint strobe_trigger;    // strobe delay from burst start (µs)
    uint burst_count;       // number of bipolar cycles per ejection
    uint burst_freq;        // burst frequency (Hz)
    bool camera_enabled;
    bool strobe_enabled;
    bool log_enabled;
} ade_config_t;
```

`ade_mode_t current_mode` (enum: `ADE_MODE_CALIBRATION` / `ADE_MODE_EJECTION`)
is a static variable in `ade.c`.

---

## REPL Commands (main.c)

All wired under `#ifdef ADE`. `fire` dispatches as single-word command.

| Command | Handler | Description |
|---------|---------|-------------|
| `set burstcount <n>` | `burstcount_set` | Bipolar cycle count (≥1) |
| `set burstfreq <hz>` | `burstfreq_set` | Burst frequency in Hz |
| `set camtrig <us>` | `camtrig_set` | Camera trigger delay from burst start |
| `set camexp <us>` | `camexp_set` | Camera exposure pulse width |
| `set strobtrig <us>` | `strobtrig_set` | Strobe trigger delay from burst start |
| `set strobwidth <us>` | `strobwidth_set` | Strobe pulse width |
| `enable logging` | `logging_enable` | Print timing summary on each fire |
| `enable camera` | `camera_enable` | Arm camera trigger SM on next fire |
| `enable strobe` | `strobe_enable` | Arm strobe trigger SM on next fire |
| `arm eject` | `ade_arm_ejection` | Switch to ejection mode (loads burst programs) |
| `arm calibration` | `ade_arm_calibration` | Switch back to imaging mode (restores pulser) |
| `fire` | `ade_fire` | Execute ejection sequence |

---

## Smoke Test Sequence (after flash)

```
version                           # ADE: enabled
start acq 200 200 2000            # calibration works
read
set camtrig 100
set camexp 50
enable camera
arm eject
fire                              # oscilloscope: GPIO11 burst, GPIO18 camera pulse
arm calibration
start acq 200 200 2000            # imaging still works after mode switch
read
```

---

## Implementation Status

- [x] Plan approved
- [x] `docs/claude/ade.md` created (this file)
- [x] `firmware/CMakePresets.json` created
- [x] `firmware/CMakeLists.txt` — ADE block added
- [x] `firmware/hw/acquisition.h/.c` — release/reclaim functions added
- [x] `firmware/ade/burst_drive.pio` — created (14 instructions)
- [x] `firmware/ade/burst_gate.pio` — created (8 instructions)
- [x] `firmware/ade/ade.c` — fully implemented
- [x] `firmware/ade/ade.h` — rewritten with mode enum, all declarations
- [x] `firmware/main.c` — all 12 ADE REPL commands wired (`set burstcount`, `set burstfreq` added in d7a533e)
- [x] Build passes (`cmake --preset ade && cmake --build build-ade`)
- [x] Hardware smoke test (RP2040, 2026-10-07): arm eject → fire×2 → arm calibration → start acq all pass

---

## Gotchas

- **PIO instruction memory fragmentation (fixed, b8775c5):** pulser (17 instr, 0-16) +
  delayed_pulse (10 instr, 17-26) = 27 used. After releasing pulser, free blocks are
  0-16 and 27-31 (non-contiguous). burst_drive (14) takes 0-13; burst_gate (8) needs
  8 contiguous but only 3+5 remain. Fix: `arm_ejection` removes delayed_pulse, reloads
  it at 0 first, then loads burst_drive (10-23) + burst_gate (24-31) → 32 exact.
  `arm_calibration` reverses: removes all three, reclaims pulser, reloads delayed_pulse.
- `pio_enable_sm_mask_in_sync()` must be used to start burst_drive + burst_gate
  simultaneously (same as pulser). Use `(1u << sm0) | (1u << sm1)`.
- Tick conversion for delays: `ticks = (us * ADE_CLK) / 1000000` then subtract
  `delayed_pulse` overhead (3 ticks per the C-SDK comment in delayed_pulse.pio).
- Motor control (GPIO20/21) — intentionally skipped, commented out.
- MUX (MAX14866) shares GPIO18–21 with ADE — cannot enable both simultaneously.
  `-DADE=ON` implies `-DMUX=OFF` in the build preset.
