# 2026-11-17 — ADE external trigger-in mode

Branch: `ade-trigger-in` (from `acoustic-droplet-ejection-hack`). Firmware 0.1.12.

## What
New third ADE mode `ADE_MODE_TRIGGER` (`arm trigger`): camera (GPIO18, active-LOW)
and strobe (GPIO21) pulse on every edge of external input GPIO20, using the
existing `camtrig/camexp/strobtrig/strobwidth` settings measured from the edge.
No burst. Continuous until `arm calibration` / `arm eject`.

## How
- `firmware/ade/delayed_pulse.pio`: new program `delayed_pulse_gpio` (11 instr):
  `wait 0 pin 0; wait 1 pin 0` instead of `wait 1 irq 2`, plus
  `delayed_pulse_gpio_init()` (sets IN base = trigger pin). `delayed_pulse_load()`
  reused unchanged.
- `firmware/ade/ade.c`: refactored mode switches into `ade_ejection_to_calibration()`
  / `ade_trigger_to_calibration()`; `ade_arm_trigger()` swaps delayed_pulse →
  delayed_pulse_gpio (pulser 17 + 11 = 28/32 slots; can't coexist with burst
  programs, ejection already uses 32). `ade_trigger_load_and_start()` reloads
  + sync-enables SMs; setters call it live in trigger mode. `trigedge_set` uses
  `gpio_set_inover` for falling edge. GPIO20 init as input + pull-down in `ade_init`.
- `firmware/ade/ade.h`: `ADE_TRIGGER_IN 19`, mode enum, `trigger_falling`, decls.
- `firmware/main.c`: `arm trigger`, `set trigedge`.

## Verified
Build passes for `ade` and `ade-pico2` presets. Trigger pin moved 19 → 20 on
the user's request (GPIO20 is free: only `MAX14866_SPI_LE`, and MUX is off in ADE builds).
Flashed to the RP2350 board (serial 0D26F3E8B47D1608) on 2026-11-23: version 0.1.12, all mode
switches, `fire`, and `start acq` OK. One unexplained hang right after reflash,
not reproduced. Scope test with a real edge on GPIO20 still pending.
