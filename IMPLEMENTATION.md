# SandForge Implementation Guide

## Overview

SandForge is a high-performance falling sand simulation engine implemented as a Godot GDExtension. It provides physics-based cellular automata simulation suitable for 2D games.

## Architecture

### Core Components

1. **Particle** (`particle.h`)
   - Minimal 2-byte structure: `mat_id` (material) + `flags`
   - Flags: `PARTICLE_FLAG_UPDATED` (moved this tick), `PARTICLE_FLAG_REACTED` (produced by a reaction this tick); both are transient and cleared before the next tick

2. **MaterialConfig** (`materialconfig.h`)
   - Data-driven material definitions with properties:
     - `state`: SOLID_FIXED, SOLID_POWDER, LIQUID, GAS
     - `density`: Controls sinking/floating (0-255)
     - `dispersion`: Horizontal flow distance for liquids
     - `decay_chance` / `decay_into`: Per-tick probability of converting into another material (e.g. Fire decaying into Smoke)
   - `MaterialRegistry` (`material_registry.h`) holds one `MaterialConfig` per `uint8_t` ID plus the `ReactionRule` table. It is owned by each `WorldGrid`, so worlds don't share materials.

3. **SandSimulationChunk** (`sand_chunk.h/cpp`)
   - 64×64 grid (4096 particles per chunk)
   - Flat 1D array layout for cache efficiency
   - Two-phase tick system, scoped to the chunk's dirty rect:
     - **Phase 1**: Movement (bottom-to-top, alternating scan direction)
     - **Phase 2**: Chemical reactions (neighborhood checking)
   - Activity tracking via a `dirty_rect` bounding box instead of a whole-chunk flag: any cell write calls `mark_dirty()`, which pads the changed cell by 1 in each direction and grows the rect. A tick only scans/reacts within that rect, then resets it so subsequent writes accumulate the region needed for the *next* tick. An empty rect means the chunk is asleep and its tick is skipped.

4. **WorldGrid** (`world_grid.h/cpp`)
   - Sparse chunk management using `unordered_map<Vector2i, SandSimulationChunk>`
   - Seamless coordinate conversion:
     - World → Chunk: `x >> 6` (divide by 64)
     - World → Local: `x & 63` (modulo 64)
   - On-demand chunk creation
   - Owns the world's `MaterialRegistry` and `SimRandom` (seedable xorshift64* PRNG used for all simulation randomness)

5. **SandWorld** (`sand_world.h/cpp`)
   - GDExtension class extending `RefCounted`
   - Godot integration via `_bind_methods()`
   - User-facing API for simulation control

## Physics Simulation

### Movement Rules

**SOLID_POWDER** (sand, gunpowder):
1. Try move straight down
2. Try diagonal down-left/down-right (random)
3. Stop if blocked

**LIQUID** (water, acid, oil):
1. Try move straight down
2. Try diagonal down-left/down-right
3. Horizontal dispersion: slide left/right through empty cells, up to `dispersion` cells, stopping at the first occupied cell (never jumps walls)

**SOLID_FIXED** (stone, brick):
- No movement

**GAS** (smoke, fire, steam):
1. Try moving up (mirrors powder/liquid falling, but rises)
2. Try diagonal up-left/up-right
3. Horizontal dispersion: slide left/right through empty cells, up to `dispersion` cells, stopping at the first occupied cell (never jumps walls)
4. Density comparisons are inverted vs. SOLID_POWDER/LIQUID: lower density rises past a denser gas/fluid above it

### Chemical Reactions

**Reaction rules** (data-driven, registered with `add_reaction()`):
- Each rule is registered on a material and names a neighbor material `other`, the products `material_into` / `other_into`, and a `chance` (1-255, out of 256, per contact per tick)
- Examples: `(SAND, FIRE, FIRE, FIRE, 120)` spreads fire; `(STONE, ACID, EMPTY, ACID, 80)` dissolves stone without consuming acid
- The 8 neighbors are checked starting at a random offset; products get `PARTICLE_FLAG_REACTED` and can't react again in the same tick, so reactions spread at most one cell per tick in every direction
- A reaction that was possible but lost its roll keeps the cell dirty, so it is retried instead of stalling when the chunk would otherwise sleep

**Decay**:
- Particles with `decay_chance > 0` have a per-tick probability of converting into `decay_into`
- Used so Fire burns out into Smoke instead of burning forever
- A failed roll keeps the cell dirty, so trapped decaying material still decays

### Optimization

- **Dirty Rect Tracking**: Each chunk only simulates the bounding box of cells changed since its last tick (padded by 1 cell); the rect is emptied and re-accumulated every tick, so a chunk with no writes near it goes fully idle. The world tracks moved particles directly, clearing their transient update flags before the next tick without scanning or changing unrelated dirty regions.
- **Alternating Scan**: Prevents directional pooling bias
- **Sparse Grid**: Only active regions consume memory

## Usage Example

```gdscript
extends Node2D

var world: SandWorld
var texture: ImageTexture

func _ready():
    # Create simulation
    world = SandWorld.new()
    
    # Define materials
    world.set_materials_from_dict({
        1: {"state": 1, "color": Color.GRAY},  # Stone
        2: {"state": 2, "color": Color.TAN, "density": 50},  # Sand
        3: {"state": 3, "color": Color.BLUE, "density": 30, "dispersion": 4},  # Water
    })

    # Acid dissolves stone (80/256 chance per contact per tick) without being used up
    world.add_material(4, "Acid", 3, Color.GREEN, 45, 3)
    world.add_reaction(1, 4, 0, 4, 80)
    
    # Place some particles
    world.brush_circle(Vector2i(512, 100), 10, 2)  # Sand circle
    world.brush_rectangle(Vector2i(0, 700), Vector2i(1024, 32), 1)  # Stone floor
    
    # Create texture for rendering
    texture = ImageTexture.create_from_image(Image.create(1024, 768, false, Image.FORMAT_RGBA8))

func _process(delta):
    # Update simulation
    world.tick()
    
    # Render to texture
    var pixel_data = world.render_to_texture(Vector2i(1024, 768), Vector2i(0, 0))
    var img = Image.create(1024, 768, false, Image.FORMAT_RGBA8)
    img.set_data(img.get_width(), img.get_height(), false, Image.FORMAT_RGBA8, pixel_data)
    texture.set_image(img)
    
    # Display
    queue_redraw()

func _draw():
    draw_set_transform_matrix(Transform2D())
    draw_texture(texture, Vector2.ZERO)
    draw_string_outline(get_theme_font("font"), Vector2(10, 30), 
        "Chunks: %d" % world.get_chunk_count())
```

## Implementation Status

### ✅ Complete

- **Phase 1: Scaffolding**
  - Particle and MaterialConfig structures
  - SandSimulationChunk with movement physics
  
- **Phase 2: Global Management**
  - WorldGrid sparse chunk allocation
  - Cross-chunk coordinate mapping
  - GDExtension boilerplate and SandWorld class
  
- **Phase 3: Simulation Core**
  - SOLID_POWDER and LIQUID movement
  - Neighborhood checking for reactions
  - Data-driven reaction rules
  
- **Phase 4: Optimization & Interaction**
  - Chunk activity sleeping
  - Brush injection (line, circle, rectangle)
  - Explosion/destruction mechanics
  - Texture rendering pipeline

### ⚠️ Known Limitations

- **Chunk Boundaries**: Particles move and react across chunk boundaries. Chunks created as movement destinations begin updating on the following simulation tick.
  
## Building

### Requirements
- C++17 compiler
- Python 3.x
- SCons build system
- Godot 4.0+ headers (via godot-cpp submodule)

### Build Steps

```bash
# Initialize submodules
git submodule update --init

# Build extension
scons

# Output: bin/linux/libSandForge.so (or platform-specific)

# Run native regression tests
scons test
```

### IDE Setup

Generate compile database for IDE integration:
```bash
scons compiledb=yes
```

## API Reference

See `doc_classes/SandWorld.xml` for complete API documentation.

Key methods:
- `tick()` - Advance simulation
- `set_particle(pos, mat_id)` - Place particle
- `get_particle_mat_id(pos)` - Query particle
- `brush_*()` - Draw shapes
- `render_to_texture()` - Get pixel data
- `add_material()` - Define new material types
- `add_reaction()` / `clear_reactions()` - Define material interactions
- `set_seed()` - Make the simulation reproducible

## Performance Characteristics

- **Chunk Update**: O(4096) per active chunk (64×64 grid, two passes)
- **Memory**: ~8KB base + 8KB per active chunk + registry
- **Rendering**: O(width × height) for texture generation

Typical performance:
- 1000 active chunks: ~60fps on modern CPU
- Scales with particle count, not world size (sparse grid benefit)

## Future Enhancements

1. Multi-threaded chunk updates
2. Neighbor chunk synchronization
3. Particle merging/stacking for optimization
4. GPU-accelerated rendering
