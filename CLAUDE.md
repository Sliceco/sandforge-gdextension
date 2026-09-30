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

- **`SandWorld`** (`src/sand_world.*`) — `RefCounted` GDCLASS exposed to GDScript; registered in `register_types.cpp`. Brushes, explosions, material registration (a new world starts with the defaults from `data/default_materials.json`, which the first user-registered material replaces), snapshot save/load, and `render_to_texture()` (RGBA8888 `PackedByteArray` that GDScript wraps in an `Image`). Delegates everything to a `WorldGrid`.
- **`WorldGrid`** (`src/world_grid.*`) — sparse `unordered_map<Vector2i, SandSimulationChunk>`; world→chunk via `>> 6` / `& 63` (64-cell chunks, negative coords work). `tick()` sorts chunks bottom row first, with horizontal order following `alternate_direction`, so falls cascade across chunk seams within one tick — don't replace this with map iteration order. Destination chunks created mid-tick are simulated starting next tick.
- **`SandSimulationChunk`** (`src/sand_chunk.*`) — 64x64 `Particle` array plus `dirty_rect`. A tick snapshots/clears the rect, scans bottom-to-top with alternating horizontal direction, then evaluates reactions in the same region. Empty `dirty_rect` = asleep, skipped. Movement across chunk edges goes through `WorldGrid`.

Invariants that are easy to break:

- All particle mutations must go through `WorldGrid::set_particle()`, never `chunk->grid[]` (there is no mutable `get_particle()`). `set_particle` marks the dirty rect and wakes the mirrored cell in an existing neighbor chunk for border edits; bypassing it leaves neighbors asleep.
- Moved particles carry a transient `UPDATED` flag so they move at most once per tick, including across chunk boundaries; reaction products carry `REACTED` so reactions spread at most one cell per tick. `mark_particle_updated()` records any cell holding transient flags (including a particle displaced by a swap) and `WorldGrid::tick()` clears them first; an unrecorded flag freezes that particle forever.
- A chance-based reaction or decay that loses its roll must `mark_dirty()` its cell, or it stalls once the chunk sleeps.
- `get_particle_readonly()` never allocates chunks; writes do, except writing an empty particle into a missing chunk (no-op). `tick()` frees chunks that went to sleep with no particles.
- Snapshots (`SNAPSHOT_VERSION` 3, 4 bytes per cell: `mat_id`, `flags`, `hp`, `shade`) store an ID → name table and remap by name on load; `add_material`'s `name` must be stable for that to work. Older versions are rejected.
- Particles placed from outside the simulation must come from `WorldGrid::make_particle()` (full `max_hp`, random `shade`), and material changes inside it from `convert_particle()` / `reaction_product()`, which reset `hp` for the new material. A bare `Particle{}` has 0 hp. Whenever hp runs out (damage, `hp_loss_chance`, damaging reactions) the particle becomes its material's `break_into`.
- Each `WorldGrid` owns its `MaterialRegistry` (materials + reaction rules) and `SimRandom`. All simulation randomness must come from `world_grid.get_random()`, never `rand()`, so a seed reproduces a run.

## Conventions

- Changing a GDScript-visible `SandWorld` API means updating three places together: the C++ declaration/implementation, `_bind_methods()`, and `doc_classes/SandWorld.xml` (compiled into editor/template_debug builds as doc data).
- Default materials live in `data/default_materials.json` (object keyed by id string, HTML colors). SConstruct validates it (valid JSON, ids 1-255, no references to unknown ids) and embeds it as `src/gen/default_materials.gen.cpp`; edit the JSON, not generated code. The sandbox reads its palette from it.
- Material IDs are `uint8_t`; `0` = empty. No material IDs are hardcoded: interactions are `ReactionRule`s registered via `add_reaction()`. `MatterState` ints are part of the GDScript contract: `0=EMPTY, 1=SOLID_FIXED, 2=SOLID_POWDER, 3=LIQUID, 4=GAS`.
- Keep simulation internals as plain C++ types; only engine-derived classes get `GDCLASS`/binding/registration.
- `project/bin/sandforge.gdextension`'s `entry_symbol` (`sandforge_library_init`) and library filenames must match `register_types.cpp` and the `SandForge` libname in `SConstruct`.
- Formatting: tabs (width 4), LLVM-derived `.clang-format`, project headers before system headers.
