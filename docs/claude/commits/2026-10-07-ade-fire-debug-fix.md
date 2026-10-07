# 2026-10-07 — ADE fire debug and PIO fragmentation fix

## Commit b8775c5 — fix: PIO instruction memory fragmentation in ade_arm_ejection

### Bug
`arm eject` → `fire` returned "fire timeout" on hardware. Diagnostic output added to
the timeout path showed `offset_burst_gate = 0xFFFFFFF7` (garbage), confirming
`pio_add_program` failed silently.

### Root cause
PIO instruction memory layout fragmentation:
- pulser: slots 0-16 (17 instr, loaded by acquisition_init)
- delayed_pulse: slots 17-26 (10 instr, loaded by ade_init)
- Slots 27-31 free (5)

After `release_pulser`: free = 0-16 + 27-31 (non-contiguous).
burst_drive (14 instr) → takes 0-13. burst_gate (8 instr) needs 8 contiguous;
only 14-16 (3) + 27-31 (5) remain — not contiguous. `pio_add_program` returned
garbage without panicking.

### Fix
`ade_arm_ejection()` now:
1. Disables camera/strobe SMs
2. Removes delayed_pulse (frees 17-26)
3. Reloads: delayed_pulse → 0-9, burst_drive → 10-23, burst_gate → 24-31
4. Re-inits camera/strobe SMs at new delayed_pulse offset

`ade_arm_calibration()` now:
1. Removes burst_gate + burst_drive + delayed_pulse (full 32 slots free)
2. Reclaims pulser → loads at 0-16
3. Reloads delayed_pulse → 17-26, re-inits camera/strobe SMs

Also fixed: camera/strobe SMs were left enabled after successful fire.

### Verified on hardware (RP2040, 2026-10-07)
```
arm eject   → "ejection mode armed (drive@8 gate@0 dp@22)"
fire        → "fire done"
fire        → "fire done"
arm calibration → "calibration mode armed"
start acq   → "Acquisition of 8000 samples started / Acquisition ended"
```
