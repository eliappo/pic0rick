# 2026-10-06 — ADE module implementation

## Commits in this session (after harness commit)

### f381647 — add: ADE CMake block and pulser release/reclaim API
- `CMakeLists.txt`: `option(ADE ...)`, ADE source wiring inside REPL else-block,
  mutual-exclusion guard with DSP
- `hw/acquisition.h/.c`: `u4rk_acquisition_release_pulser()` and
  `u4rk_acquisition_reclaim_pulser()` — the only additions outside `firmware/ade/`

### d9c6ad1 — add: burst_drive.pio + burst_gate.pio; fix delayed_pulse IRQ
- `burst_drive.pio`: 14-instruction bipolar burst generator; fires IRQ 2 to trigger
  camera+strobe, IRQ 4 when burst complete
- `burst_gate.pio`: 8-instruction gate controller; OE immediately, waits IRQ 4,
  damps, fires IRQ 5 to signal firmware
- `delayed_pulse.pio`: `wait 1 irq 0 rel` → `wait 1 irq 2` (absolute) so both
  camera and strobe SMs share one trigger — brings pio1 instruction use to exactly 32

### 8a1d30f — add: ADE module implementation
- `ade.h`: rewrote with `ade_config_t`, `ade_mode_t`, correct function signatures
- `ade.c`: full implementation of all functions
- `main.c`: fix `#ifdef MUX` → `#ifdef ADE` for ade.h include; wire all 10 REPL cmds
- PIO C-SDK: fix naming typos (`_program_get_default_config`, `_consecutive_pindirs`)
- Build passes clean: `cmake --preset ade && cmake --build build-ade`

## Design decisions recorded here

- burst_drive on SM0, burst_gate on SM1 (reclaimed from pulser on `arm eject`)
- camera SM2, strobe SM3 (claimed at `ade_init()`, held for both modes)
- ADE_CLK (2 MHz) for delayed_pulse; ADE_BURST_CLK (125 MHz) for burst programs
- IRQ 2 = burst start trigger (shared by camera+strobe); IRQ 4 = burst done;
  IRQ 5 = gate done (polled by firmware to confirm ejection complete)
- `pio_enable_sm_mask_in_sync()` starts all 4 SMs simultaneously per burst
