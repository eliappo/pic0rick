# 2026-10-06 — ADE development harness scaffolding

## What changed
- `docs/claude/ade.md` — new cross-session knowledge file for ADE module: pin/SM
  assignment table, mode-switch mechanism, implementation status checklist, known
  bugs in scaffolding, smoke test sequence, gotchas
- `firmware/CMakePresets.json` — new CMake preset `ade` (pico, ADE=ON, MUX=OFF)
  for fast compile-check: `cmake --preset ade && cmake --build build-ade`
- `docs/claude/README.md` — added ade.md entry
- `CLAUDE.md` — added ade.md to current topic notes list
- `docs/claude/TODO.md` — added ADE implementation task list

## Why
Sets up the cross-session context and build tooling needed to implement the ADE
module incrementally, with a clear record of what has and hasn't been done yet.
No firmware code changed in this commit.
