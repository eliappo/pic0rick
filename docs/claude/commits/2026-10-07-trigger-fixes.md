# 2026-10-07 — Camera/strobe trigger fixes

## Commit 76207da — fix: camera/strobe triggers not firing and camera hanging HIGH

### Symptoms
- GPIO18 (camera) idled HIGH at boot
- Camera/strobe never fired after `arm eject` + `fire`
- Camera fired after clkdiv fix but hung HIGH after fire completed

### Bug 1 — GPIO idle HIGH (delayed_pulse_init)
`pio_sm_set_pins_with_mask()` was called AFTER `pio_sm_init()`. The SDK function
iterates over set bits and calls `pio_sm_set_config()` internally with a default
config, clobbering CLKDIV from 62.5 → 1.0. delayed_pulse ran at 125 MHz instead of
2 MHz; pulses were 62× shorter than expected.
Fix: moved `pio_sm_set_pins_with_mask()` BEFORE `pio_sm_init()`.

Note: burst_drive and burst_gate also call `pio_sm_set_pins_with_mask()` after init,
but their target clkdiv is 1.0 (= default), so the clobber is harmless for them.

### Bug 2 — camera/strobe cut off mid-pulse
Camera/strobe SMs were disabled immediately when burst_gate fired IRQ 5. With
burst(20µs) + damp(10µs) ≈ 30µs and strobe trig=30µs, the strobe SM was disabled
the instant it started its pulse.
Fix: success path of `ade_fire()` only disables burst_drive and burst_gate.
Camera/strobe SMs run to completion and wrap to `wait 1 irq 2`. `delayed_pulse_load()`
on the next `fire()` call disables and resets them.

### Bug 3 — pin stuck HIGH when SM disabled mid-pulse
Fix: added `pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 0))` in `delayed_pulse_load()`
immediately after `pio_sm_set_enabled(false)`. Ensures pin is LOW whenever the SM
is reset, regardless of where it was in the program.

### Verified on hardware (RP2040, 2026-10-07)
```
arm eject → enable camera → enable strobe → fire
GPIO18: LOW at idle, 50µs pulse at +20µs after burst ✓
GPIO19: LOW at idle, 5µs pulse at +30µs after burst ✓
GPIO11: burst waveform unchanged ✓
arm calibration → start acq → imaging still works ✓
```
