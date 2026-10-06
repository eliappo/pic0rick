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
| `firmware/ade/ade.h` | exists, complete | constants, struct, declarations |
| `firmware/ade/ade.c` | exists, incomplete | `ade_init()` partial; all others stubbed |
| `firmware/ade/delayed_pulse.pio` | exists, complete | camera + strobe pulse gen |
| `firmware/ade/burst_drive.pio` | TODO | bipolar burst, inspired by pulser.pio drive SM |
| `firmware/ade/burst_gate.pio` | TODO | OE+PDAMP control for burst duration |
| `firmware/burst.pio` | exists (wrong location) | scaffolding; superseded by burst_drive.pio |
| `firmware/CMakeLists.txt` | needs ADE block | mirrors MUX pattern |
| `firmware/CMakePresets.json` | exists | `ade` preset: pico, ADE=ON, MUX=OFF |
| `firmware/hw/acquisition.h/.c` | needs 2 new fns | release/reclaim pulser |
| `firmware/main.c` | needs ADE cmds wired | 8 commands currently commented out |

---

## Known Bugs in Scaffolding (fix these first)

1. **`ade.c:32`** — `float div = (float)clock_get_hz(clk_sys) / 10e6f;`
   → should be `/ (float)ADE_CLK` (i.e. 2000000). Wrong clock = wrong timing.

2. **`ade_init()` SM claims** — `pio_sm_claim(pio_ade, 0)` and `pio_sm_claim(pio_ade, 1)`
   will **panic** at runtime: SM0/SM1 are already owned by `u4rk_acquisition_init()`.
   Fix: claim only SM2 + SM3 at init; SM0/SM1 are claimed by `ade_arm_ejection()`.

3. **`burst.pio` C-SDK** (line ~34) — calls `delayed_pulse_get_default_config(offset)`
   (copy-paste error). Fixed when content is rewritten as `burst_drive.pio`.

4. **`ade_init()` burst SM never initialized** — `offset_burst` computed but `burst_init()`
   never called. Moot once burst moves to ejection-mode init only.

---

## ade_config Struct (ade.h)

```c
typedef struct {
    uint camera_exposure;   // µs → ticks at ADE_CLK
    uint camera_trigger;    // delay after burst start, µs → ticks
    uint strobe_width;      // µs → ticks
    uint strobe_trigger;    // delay after burst start, µs → ticks
    uint burst_count;       // number of bipolar cycles
    uint burst_freq;        // Hz
    bool camera_enabled;
    bool strobe_enabled;
    bool log_enabled;
} ade_config;
```

Add `ade_mode_t current_mode` (enum: `ADE_MODE_CALIBRATION` / `ADE_MODE_EJECTION`)
as static variable in `ade.c`.

---

## REPL Commands (main.c)

All currently commented out. Wire these under `#ifdef ADE`:

| Command string | Handler | Status |
|----------------|---------|--------|
| `set camtrig` | `camtrig_set` | TODO |
| `set camexp` | `camexp_set` | TODO |
| `set strobtrig` | `strobtrig_set` | TODO |
| `set strobwidth` | `strobwidth_set` | TODO |
| `enable logging` | `logging_enable` | TODO |
| `enable camera` | `camera_enable` | TODO |
| `enable strobe` | `strobe_enable` | TODO |
| `arm eject` | `ade_arm_ejection` | TODO |
| `arm calibration` | `ade_arm_calibration` | TODO |
| `fire` | `ade_fire` | TODO |

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
- [x] `firmware/main.c` — all 10 ADE REPL commands wired
- [x] Build passes (`cmake --preset ade && cmake --build build-ade`)
- [ ] Hardware smoke test

---

## Gotchas

- PIO instruction memory on pio1 is shared. Removing pulser program frees space for
  burst programs. If instruction memory runs tight, check program sizes.
- `pio_enable_sm_mask_in_sync()` must be used to start burst_drive + burst_gate
  simultaneously (same as pulser). Use `(1u << sm0) | (1u << sm1)`.
- Tick conversion for delays: `ticks = (us * ADE_CLK) / 1000000` then subtract
  `delayed_pulse` overhead (3 ticks per the C-SDK comment in delayed_pulse.pio).
- Motor control (GPIO20/21) — intentionally skipped, commented out.
- MUX (MAX14866) shares GPIO18–21 with ADE — cannot enable both simultaneously.
  `-DADE=ON` implies `-DMUX=OFF` in the build preset.
