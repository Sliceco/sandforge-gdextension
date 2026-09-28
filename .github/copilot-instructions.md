# SandForge GDExtension Instructions

## Build, format, and run

- Initialize the binding submodule before the first build:
  ```sh
  git submodule update --init --recursive
  ```
- The primary build is SCons. It compiles the GDExtension and copies the platform library into `project/bin/<platform>/`, where `project/bin/sandforge.gdextension` can load it:
  ```sh
  scons
  ```
- Match CI or build a specific target with:
  ```sh
  scons target=template_debug platform=linux arch=x86_64 precision=double
  ```
- CMake is also supported:
  ```sh
  cmake -S . -B build
  cmake --build build
  ```
- Generate `compile_commands.json` for IDE support without compiling:
  ```sh
  scons compiledb=yes compile_commands.json
  ```
- Run native `WorldGrid` boundary regressions with:
  ```sh
  scons test
  ```
  This builds and executes `tests/world_grid_tests.cpp` against the simulation sources directly. Exercise the full extension through the Godot sample project at `project/project.godot`; run its main scene (`sand_sandbox.tscn`) after building.
- CI does not currently run formatting, but `.clang-format` is the C++ style source of truth. Check one changed C++ file with:
  ```sh
  clang-format --dry-run --Werror src/sand_world.cpp
  ```

## Architecture

- `SandWorld` is the Godot-facing `RefCounted` GDExtension class. Its methods are bound in `SandWorld::_bind_methods()` and it is made available to GDScript by `GDREGISTER_CLASS(SandWorld)` in `register_types.cpp`.
- `WorldGrid` is the sparse world layer: it maps chunk coordinates to `SandSimulationChunk` instances and translates world coordinates using the fixed 64-cell chunk size. Reads through `get_particle_readonly()` do not allocate chunks; `get_or_create_chunk()`-based writes do. All particle writes (initial placement, movement, reactions) should go through `WorldGrid::set_particle()` so dirty-rect tracking and neighbor wake-up stay correct.
- `SandSimulationChunk` owns the cache-friendly 64x64 particle array and advances local simulation, scoped to its `dirty_rect` (a padded bounding box of recently changed cells). A tick snapshots and clears that rect, clears update flags and scans only within it (bottom-to-top movement pass with alternating horizontal direction), then evaluates chemical reactions in the same region. An empty `dirty_rect` means the chunk is asleep and its tick is skipped. Particle movement crosses chunk boundaries transparently via `WorldGrid`, which also creates destination chunks on demand.
- Any code path that mutates a particle (movement, reactions, external edits) must call `WorldGrid::set_particle()` (never write to `chunk->grid[]` directly) so the chunk's `dirty_rect` is updated via `mark_dirty()`. Edits on a chunk's border also mark the mirrored cell dirty on any existing neighboring chunk, so a sleeping neighbor wakes up instead of silently missing changes at the boundary (e.g. removing a block from the chunk below must wake the chunk above).
- `Particle` is a compact material-ID/flag value. `MaterialConfig` defines matter state, density, rendering color, flow dispersion, and reaction probabilities. The material registry is the static `SandSimulationChunk::mat_registry`, so it is shared by all `SandWorld` instances.
- Rendering flows from `SandWorld::render_to_texture()` through `WorldGrid` reads into an RGBA8888 `PackedByteArray`. GDScript converts that byte array into an `Image` or `ImageTexture`.

## Repository conventions

- Keep simulation internals (`SandSimulationChunk`, `WorldGrid`) as ordinary C++ types. Only Godot-facing classes that inherit engine base classes need `GDCLASS`, method binding, and registration.
- When adding or changing a GDScript-visible `SandWorld` API, update all three surfaces together: the C++ declaration/implementation, `_bind_methods()`, and `doc_classes/SandWorld.xml`. Debug/editor builds include XML documentation data through `SConstruct`.
- Material IDs are `uint8_t` values; ID `0` means empty. Current reaction logic reserves IDs `4` and `5` for acid and fire respectively. Preserve these assumptions unless the related simulation and documentation are updated together.
- `MatterState` values are part of the GDScript material configuration contract: `0=EMPTY`, `1=SOLID_FIXED`, `2=SOLID_POWDER`, `3=LIQUID`, and `4=GAS`.
- The project uses C++17. C++ formatting uses tabs (width 4), LLVM-derived `.clang-format`, and project headers before system headers.
- The extension manifest’s `entry_symbol` (`sandforge_library_init`) must match the exported initializer in `register_types.cpp`; its library paths must match the `SandForge` library name and SCons output names.
