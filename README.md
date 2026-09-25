# SandForge GDExtension

A high-performance cellular automata engine for Godot 4.x written in C++ via GDExtension. Build massive sand, fluid, gas, and reaction-based grid simulations with native speed.

## Features

* Sparse chunked world grid (`WorldGrid`) so simulation cost scales with active particle count, not world size
* Cache-friendly 64x64 `SandSimulationChunk` grids with dirty-rect tracking, so idle chunks are skipped entirely
* Data-driven materials (`SandWorld.add_material`) covering `SOLID_FIXED`, `SOLID_POWDER`, `LIQUID`, and `GAS` matter states
* Density-based sinking/floating for powders and liquids, and buoyant rising for gases (e.g. smoke)
* Chemical reactions: acid corrosion, fire spreading, and material decay (e.g. Fire burning out into Smoke)
* Brush tools (line, circle, rectangle) and explosion/destruction helpers
* Direct RGBA8888 texture rendering for fast display via `Image`/`ImageTexture`
* World snapshot save/load (`save_snapshot` / `load_snapshot`)

See [IMPLEMENTATION.md](./IMPLEMENTATION.md) for a deeper look at the simulation architecture, and [doc_classes/SandWorld.xml](./doc_classes/SandWorld.xml) for the full GDScript API reference (also browsable in the Godot editor's built-in docs after building).

## Repository contents

* C++ source for the GDExtension ([src/](./src/))
* A sample Godot project with an interactive sandbox scene ([project/](./project)), used to exercise the extension
* Native regression tests for `WorldGrid` boundary behavior ([tests/](./tests/))
* `godot-cpp` as a submodule (`godot-cpp/`)
* GitHub Issue template ([.github/ISSUE_TEMPLATE/](./.github/ISSUE_TEMPLATE/))
* GitHub CI/CD workflows: [ci.yml](./.github/workflows/ci.yml) (build verification on every push/PR) and [make_build.yml](./.github/workflows/make_build.yml) (manually triggered multi-platform release builds)

## Building

Requirements: a C++17 compiler, Python 3.x, and [SCons](https://scons.org/).

```shell
# Initialize the godot-cpp submodule (first time only)
git submodule update --init --recursive

# Build the extension for your host platform
scons
```

The build compiles the GDExtension and copies the platform library into `project/bin/<platform>/`, where `project/bin/sandforge.gdextension` loads it. To match a specific CI configuration:

```shell
scons target=template_debug platform=linux arch=x86_64 precision=double
```

CMake is also supported as an alternative build system:

```shell
cmake -S . -B build
cmake --build build
```

## Testing

Run the native `WorldGrid` boundary regression suite:

```shell
scons test
```

This builds and executes [tests/world_grid_tests.cpp](./tests/world_grid_tests.cpp) against the simulation sources directly (no Godot runtime required).

To exercise the extension interactively, open [project/project.godot](./project/project.godot) in Godot 4.1+ after building and run the main scene (`sand_sandbox.tscn`), which provides a paintable sandbox with brushes for every built-in material, pause/step controls, and a chunk/dirty-rect debug overlay.

## Configuring an IDE

Generate a `compile_commands.json` compilation database so IDEs with C++ tooling (e.g. clangd) can self-configure:

```shell
# Generate compile_commands.json while compiling
scons compiledb=yes

# Generate compile_commands.json without compiling
scons compiledb=yes compile_commands.json
```

## Code style

C++ formatting is enforced by [.clang-format](./.clang-format) (tabs, width 4, LLVM-derived, project headers before system headers). Check a file with:

```shell
clang-format --dry-run --Werror src/sand_world.cpp
```

## Contributing

Issues and pull requests are welcome. Please open an issue describing the bug or proposal before submitting large changes, and make sure `scons test` and a full `scons` build both pass before submitting a PR.

## License

This project is released into the public domain under [The Unlicense](./LICENSE.md). See [LICENSE.md](./LICENSE.md) for details. Note that the `godot-cpp` submodule is a separate project distributed under its own (MIT) license.
