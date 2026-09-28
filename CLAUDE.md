# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

SandForge is a Godot 4.1+ GDExtension (C++17, godot-cpp submodule) implementing a chunked falling-sand / cellular-automata simulation. `.github/copilot-instructions.md` and `IMPLEMENTATION.md` hold further detail; keep this file consistent with them when conventions change.

## Commands

```sh
git submodule update --init --recursive   # first time only; SConstruct aborts without godot-cpp
scons                                      # build for host, installs lib into project/bin/<platform>/
scons target=template_debug platform=linux arch=x86_64 precision=double   # match Linux CI config
scons test                                 # build + run tests/world_grid_tests.cpp (no Godot needed)
scons compiledb=yes compile_commands.json  # clangd database without compiling
clang-format --dry-run --Werror src/sand_world.cpp   # style check (not enforced in CI)
```

CMake alternative: `cmake -S . -B build && cmake --build build`, tests via `ctest --test-dir build`.

The test binary has no filter: `main()` runs every `test_*` function in sequence; a failed check prints a message and exits with `EXIT_FAILURE`. To run a single test, temporarily comment out the others in `main()`; after `scons test` the binary can be re-run directly as `build/tests/world_grid_tests`. Tests link only `sand_chunk.cpp` + `world_grid.cpp`, so anything they need must not depend on `SandWorld`.

Manual verification: open `project/project.godot` in Godot and run `sand_sandbox.tscn` (paint brushes, pause/step, chunk/dirty-rect debug overlay).

## Architecture

Three layers, only the top one is a Godot class:

- **`SandWorld`** (`src/sand_world.*`) — `RefCounted` GDCLASS exposed to GDScript; registered in `register_types.cpp`. Brushes, explosions, material registration, snapshot save/load, and `render_to_texture()` (RGBA8888 `PackedByteArray` that GDScript wraps in an `Image`). Delegates everything to a `WorldGrid`.
- **`WorldGrid`** (`src/world_grid.*`) — sparse `unordered_map<Vector2i, SandSimulationChunk>`; world→chunk via `>> 6` / `& 63` (64-cell chunks, negative coords work). `tick()` sorts chunks bottom row first, with horizontal order following `alternate_direction`, so falls cascade across chunk seams within one tick — don't replace this with map iteration order. Destination chunks created mid-tick are simulated starting next tick.
- **`SandSimulationChunk`** (`src/sand_chunk.*`) — 64x64 `Particle` array plus `dirty_rect`. A tick snapshots/clears the rect, scans bottom-to-top with alternating horizontal direction, then evaluates reactions in the same region. Empty `dirty_rect` = asleep, skipped. Movement across chunk edges goes through `WorldGrid`.

Invariants that are easy to break:

- All particle mutations must go through `WorldGrid::set_particle()`, never `chunk->grid[]` or the mutable `get_particle()` reference. `set_particle` marks the dirty rect and wakes the mirrored cell in an existing neighbor chunk for border edits; bypassing it leaves neighbors asleep.
- Moved particles carry a transient "updated" flag so they move at most once per tick, including across chunk boundaries; `mark_particle_updated()` records them and `WorldGrid::tick()` clears them first. The regression tests cover exactly this (once-per-tick crossing, contiguous columns across seams).
- `get_particle_readonly()` never allocates chunks; writes do.
- The material registry is the static `SandSimulationChunk::mat_registry`, shared across all `SandWorld` instances.

## Conventions

- Changing a GDScript-visible `SandWorld` API means updating three places together: the C++ declaration/implementation, `_bind_methods()`, and `doc_classes/SandWorld.xml` (compiled into editor/template_debug builds as doc data).
- Material IDs are `uint8_t`; `0` = empty. Reactions hardcode `ACID_MAT_ID` = 4 and `FIRE_MAT_ID` = 5. `MatterState` ints are part of the GDScript contract: `0=EMPTY, 1=SOLID_FIXED, 2=SOLID_POWDER, 3=LIQUID, 4=GAS`.
- Keep simulation internals as plain C++ types; only engine-derived classes get `GDCLASS`/binding/registration.
- `project/bin/sandforge.gdextension`'s `entry_symbol` (`sandforge_library_init`) and library filenames must match `register_types.cpp` and the `SandForge` libname in `SConstruct`.
- Formatting: tabs (width 4), LLVM-derived `.clang-format`, project headers before system headers.
