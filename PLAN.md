# Aesthesis Engine — Project Plan & Architecture

## Overview

Aesthesis is a minimal, from-scratch game engine targeting Windows (Linux planned). The engine provides a Vulkan-based deferred renderer, core utilities, and a platform abstraction layer. Game code is written separately using the engine's renderer API.

This document defines the architecture, implementation plan, code conventions, and progress tracking for the engine rewrite.

---

## Architecture

```
┌─────────────────────────────────────────────┐
│                  Game Layer                  │
│  (game/, camera, scene, game-specific code)  │
├─────────────────────────────────────────────┤
│               Renderer API                  │
│         (renderer/api.hpp)                  │
│  submit_mesh, submit_light, draw_sky, ...   │
├─────────────────────────────────────────────┤
│           Vulkan Deferred Backend            │
│            (renderer/vulkan/)               │
│  G-buffer → lighting → compositing          │
├─────────────────────────────────────────────┤
│              Core Library                   │
│   types, math, memory, string, log,         │
│   array, file                               │
├─────────────────────────────────────────────┤
│            Platform Layer                   │
│  (platform.hpp → win32/, linux/ [future])   │
│  window, input, time, threading, vsurface   │
└─────────────────────────────────────────────┘
```

### Layer Rules

- **Game** depends on: Renderer API, Core, Platform (input/time only)
- **Renderer API** depends on: Core, Platform (window dimensions, Vulkan surface)
- **Vulkan Backend** depends on: Core, Platform, Vulkan SDK, VMA
- **Core** depends on: nothing
- **Platform** depends on: OS APIs only

### Game Entry Point

The engine defines a `GameInterface` struct with function pointers:

```cpp
struct GameInterface {
    void (*init)();
    void (*update)(f32 dt);
    void (*shutdown)();
};
```

The game implements a `create_game()` function that returns a filled `GameInterface`. The platform layer calls through this struct. This cleanly separates engine from game — new games implement these three functions and nothing else.

---

## Directory Structure

```
Aesthesis/
├── assets/                     # textures, models, fonts
│   └── textures/
│       └── global/
├── dependencies/               # header-only third-party libs
│   ├── cgltf.h
│   ├── stb_image.h
│   ├── stb_truetype.h
│   └── vk_mem_alloc.h          # Vulkan Memory Allocator
├── shaders/
│   ├── glsl/                   # source GLSL shaders
│   └── spv/                    # compiled SPIR-V (build output)
├── src/
│   ├── core/                   # standalone utilities, zero dependencies
│   │   ├── types.hpp
│   │   ├── math.hpp
│   │   ├── memory.hpp / .cpp
│   │   ├── string.hpp / .cpp
│   │   ├── log.hpp / .cpp
│   │   ├── array.hpp
│   │   └── file.hpp
│   ├── platform/               # OS abstraction
│   │   ├── platform.hpp        # public interface
│   │   └── win32/              # Windows implementation
│   │       ├── win32.cpp
│   │       ├── win32.hpp
│   │       └── win32_file.cpp
│   ├── renderer/               # rendering layer
│   │   ├── api.hpp             # game-facing renderer API
│   │   ├── api.cpp             # API implementation (dispatches to backend)
│   │   ├── mesh.hpp / .cpp     # vertex data, procedural generation (backend-agnostic)
│   │   ├── texture.hpp / .cpp  # image loading via stb (backend-agnostic)
│   │   ├── font.hpp / .cpp     # SDF atlas generation via stb_truetype (backend-agnostic)
│   │   ├── gltf.hpp / .cpp     # glTF 2.0 model loading (backend-agnostic)
│   │   ├── opengl/             # old OpenGL forward renderer (REFERENCE ONLY)
│   │   │   └── (moved old renderer files here, untouched)
│   │   └── vulkan/             # Vulkan deferred renderer
│   │       ├── vk_backend.hpp / .cpp    # instance, device, swapchain, command buffers
│   │       ├── vk_pipeline.hpp / .cpp   # render passes, graphics pipelines, descriptors
│   │       ├── vk_memory.hpp / .cpp     # VMA wrapper, buffer/image creation
│   │       ├── vk_mesh.hpp / .cpp       # vertex/index buffer GPU upload
│   │       ├── vk_texture.hpp / .cpp    # image upload, samplers, descriptor binding
│   │       ├── vk_shader.hpp / .cpp     # SPIR-V loading, shader modules
│   │       ├── vk_gbuffer.hpp / .cpp    # G-buffer setup and geometry pass
│   │       ├── vk_lighting.hpp / .cpp   # fullscreen lighting pass
│   │       ├── vk_shadow.hpp / .cpp     # shadow map pass
│   │       ├── vk_sky.hpp / .cpp        # cubemap sky rendering
│   │       ├── vk_particle.hpp / .cpp   # GPU-instanced particle system
│   │       ├── vk_draw2d.hpp / .cpp     # 2D overlay rendering
│   │       └── vk_text.hpp / .cpp       # SDF text rendering
│   └── game/                   # game-specific code (empty for new projects)
│       ├── camera.hpp / .cpp   # game-defined camera (free-fly for test scene)
│       └── scene.hpp / .cpp    # test scene setup
├── Aesthesis.sln
├── Aesthesis.vcxproj
└── PLAN.md
```

---

## Renderer API

The game-facing API lives in `renderer/api.hpp`. Game code includes only this header. The API is submission-based: game code describes *what* to render, the backend decides *how*.

```cpp
namespace renderer {

    // lifecycle
    void init();
    void shutdown();

    // per-frame
    void begin_frame(const mat4& view, const mat4& projection);
    void end_frame(); // sorts, batches, executes all passes, presents

    // 3D submissions (queued, drawn during end_frame)
    void submit_mesh(MeshHandle mesh, const mat4& transform, const Material& material);
    void submit_light(const vec3& position, const vec4& color, f32 radius, f32 intensity);
    void set_sun(const vec3& direction, const vec4& color);

    // environment
    void draw_sky(CubemapHandle cubemap);

    // particles
    ParticleEmitter create_emitter(const ParticleDesc& desc);
    void submit_particles(ParticleEmitter emitter, const vec3& position);

    // 2D / HUD (drawn after 3D, no depth)
    void draw_2d_rect(f32 x, f32 y, f32 w, f32 h, const vec4& color);
    void draw_2d_line(f32 x0, f32 y0, f32 x1, f32 y1, const vec4& color);
    void draw_text(FontHandle font, const char* str, f32 x, f32 y, f32 scale, const vec4& color);

    // resource loading
    MeshHandle load_mesh(const char* path);         // glTF
    MeshHandle create_mesh(Vertex* verts, u32 vert_count, u32* indices, u32 idx_count);
    CubemapHandle load_cubemap(const char* path);
    TextureHandle load_texture(const char* path);
    FontHandle load_font(const char* path, f32 size);

    // debug
    void debug_toggle();
    void draw_debug_line(const vec3& a, const vec3& b, const vec4& color);
}
```

### Material

```cpp
struct Material {
    vec4 base_color;            // flat color fallback
    TextureHandle albedo;       // PBR albedo map
    TextureHandle normal;       // normal map
    TextureHandle metallic_roughness; // PBR metallic/roughness (future)
    f32 metallic;               // scalar fallback
    f32 roughness;              // scalar fallback
};
```

Handle types (`MeshHandle`, `TextureHandle`, etc.) are opaque `u32` indices into internal arrays managed by the backend.

---

## Vulkan Deferred Rendering Pipeline

Built as deferred from day one. The pipeline per frame:

```
1. Shadow Pass
   └── render shadow-casting geometry into depth-only shadow maps (CSM)

2. G-Buffer Pass (geometry pass)
   ├── render all opaque meshes
   └── output to G-buffer attachments:
       ├── RT0: albedo (RGBA8)
       ├── RT1: normals (RGB16F, world-space)
       ├── RT2: metallic + roughness (RG8)
       └── depth (D32F)

3. Lighting Pass (fullscreen quad)
   ├── read G-buffer + shadow maps
   ├── evaluate directional sun light + CSM shadows
   ├── evaluate point lights (submitted via submit_light)
   └── output: HDR color (RGBA16F)

4. Sky Pass
   └── render cubemap sky where depth == 1.0

5. Particle Pass (forward, alpha-blended)
   └── GPU-instanced particles, read depth for soft particles

6. Post-Processing
   ├── bloom (extract → blur → composite) [future]
   └── tonemapping (HDR → LDR)

7. 2D / HUD Pass
   ├── draw2d overlays
   ├── SDF text
   └── debug lines / debug view

8. Present
   └── swapchain present
```

### G-Buffer Layout

| Attachment | Format     | Content                        |
|-----------|------------|--------------------------------|
| RT0       | RGBA8      | albedo.rgb, ao (future)        |
| RT1       | RGB16F     | world-space normal             |
| RT2       | RG8        | metallic, roughness            |
| Depth     | D32F       | depth                          |

---

## Implementation Phases

### Phase 1 — Foundation (Current Priority)
**Goal: Vulkan triangle on screen + project restructure**

- [x] Move old OpenGL renderer files into `renderer/opengl/`
- [x] Clear `game/` folder, move camera there
- [ ] Create `GameInterface` struct and wire up platform layer
- [ ] Set up Vulkan instance + device selection
- [ ] Create win32 Vulkan surface (`vkCreateWin32SurfaceKHR`)
- [ ] Swapchain creation + recreation on resize
- [ ] Command buffer setup (per-frame, double-buffered)
- [ ] VMA integration for memory allocation
- [ ] SPIR-V shader loading
- [ ] Render a hardcoded triangle (validation that the pipeline works)
- [ ] Add SPIR-V compilation pre-build step to vcxproj

### Phase 2 — Core Rendering
**Goal: Render a textured mesh with the deferred pipeline**

- [ ] Vertex/index buffer upload via VMA
- [ ] G-buffer render pass + framebuffer setup (RT0, RT1, RT2, depth)
- [ ] Geometry pass pipeline (vertex input, descriptors, push constants)
- [ ] Fullscreen lighting pass (directional sun, read G-buffer)
- [ ] Depth-only shadow pass (single cascade to start)
- [ ] Basic PBR material struct + descriptor sets
- [ ] glTF mesh loading → Vulkan buffers
- [ ] Texture loading → Vulkan images + samplers
- [ ] Implement `renderer::api` stubs wired to Vulkan backend
- [ ] Render a textured glTF model lit by a single directional light

### Phase 3 — Test Scene
**Goal: Complete test scene for debugging**

- [ ] Free-fly camera in `game/camera.cpp`
- [ ] Cubemap sky rendering
- [ ] Multiple point lights (`submit_light`)
- [ ] Cascaded shadow maps (3 cascades)
- [ ] Debug view (` key cycling): wireframe, G-buffer visualization, depth
- [ ] FPS / frame time text overlay (SDF text pipeline)
- [ ] On-screen camera position / orientation
- [ ] Draw debug lines (3D, useful for physics later)
- [ ] 2D overlay rendering (draw2d pipeline)

### Phase 4 — Effects & Polish
**Goal: Feature parity with old renderer + PBR**

- [ ] PBR lighting (metallic/roughness workflow, Cook-Torrance BRDF)
- [ ] Normal mapping in G-buffer pass
- [ ] Particle system (GPU-instanced, soft particles via depth read)
- [ ] Particle factory API (game defines presets, renderer spawns)
- [ ] Bloom post-process (extract → separable Gaussian blur → composite)
- [ ] HDR → LDR tonemapping (ACES or Reinhard)
- [ ] Font atlas + SDF text rendering

### Phase 5 — Future
**Goal: Expand the engine beyond rendering**

- [ ] Physics engine integration
- [ ] Linux platform layer (`platform/linux/`)
- [ ] CMake build system
- [ ] Remove old OpenGL reference code
- [ ] Audio system
- [ ] Scene graph / ECS (evaluate need)

---

## Code Style & Conventions

### Naming

| Element         | Convention       | Example                          |
|----------------|------------------|----------------------------------|
| functions       | `snake_case`     | `create_mesh`, `begin_frame`     |
| variables       | `snake_case`     | `vertex_count`, `sun_dir`        |
| types / structs | `PascalCase`     | `Material`, `Camera`, `Vertex`   |
| namespaces      | `snake_case`     | `renderer::`, `mesh::`, `vk::`   |
| constants       | `UPPER_SNAKE`    | `MAX_LIGHTS`, `PI`               |
| macros          | `UPPER_SNAKE`    | `KILOBYTES(n)`, `MEGABYTES(n)`   |
| enum values     | `UPPER_SNAKE`    | `MODE_NONE`, `MODE_WIREFRAME`    |
| file names      | `snake_case`     | `vk_backend.cpp`, `draw2d.hpp`   |

### Code Structure

- **Namespaces over classes.** Prefer `namespace mesh { Mesh create(...); }` over `class MeshManager`.
- **POD structs.** No constructors, destructors, or RAII wrappers. Explicit `init`/`create` and `destroy`/`shutdown` functions.
- **No STL.** Use `arr::Array<T>` for dynamic arrays, `str::` for string operations, `memory::` for allocation.
- **No exceptions, no RTTI.** Error handling via return values and logging.
- **C-style memory.** `memory::malloc/realloc/free` for heap, `memory::Arena` for bump allocation.
- **`#pragma once`** for all headers.
- **Flat includes.** Headers include each other by name without path prefixes (include dirs are configured in the project).
- **`static`** for file-local variables and functions. No anonymous namespaces.
- **Tabs for indentation.** Match existing codebase.

### Vulkan Conventions

- **`vk_` prefix** for all Vulkan backend source files.
- **No Vulkan types in `api.hpp`.** The game-facing API uses opaque handles (`u32`) and engine-defined structs only.
- **VMA for all GPU memory.** No manual `vkAllocateMemory`.
- **SPIR-V compiled at build time.** Source GLSL in `shaders/glsl/`, compiled `.spv` in `shaders/spv/`.
- **Double-buffered frames.** Two sets of command buffers, semaphores, and fences for pipelining.
- **Synchronization explicit.** Pipeline barriers and semaphores, no implicit sync.

### File Organization

- One header + one source file per system (e.g., `vk_backend.hpp` + `vk_backend.cpp`).
- Headers declare the public interface. Implementation details stay in the `.cpp`.
- Third-party `#define IMPLEMENTATION` macros go in exactly one `.cpp` file each.

### Comments

- Only where logic isn't self-evident.
- No boilerplate file headers, no docstrings on obvious functions.
- `// TODO:` for known incomplete work.
- `// NOTE:` for non-obvious design decisions.

---

## Dependencies

| Library           | Version   | Purpose                        | Location              |
|-------------------|-----------|--------------------------------|-----------------------|
| Vulkan SDK        | 1.4.341.1 | Rendering API                  | System install        |
| VMA               | latest    | Vulkan memory allocation       | `dependencies/`       |
| stb_image         | latest    | Image loading (PNG, JPG, etc.) | `dependencies/`       |
| stb_truetype      | latest    | TTF parsing + SDF generation   | `dependencies/`       |
| cgltf             | latest    | glTF 2.0 model loading         | `dependencies/`       |

No other external libraries. No package managers. Everything self-contained.

---

## Build

### Windows (Visual Studio)

- MSVC v143 toolset, x64
- Link: `vulkan-1.lib`
- Include: Vulkan SDK headers, `dependencies/`, `src/` subdirectories
- Pre-build step: compile `shaders/glsl/*.vert` and `*.frag` to `shaders/spv/` using `glslc`
- Subsystem: Windows

### Shader Compilation

```
glslc shaders/glsl/shader.vert -o shaders/spv/shader.vert.spv
glslc shaders/glsl/shader.frag -o shaders/spv/shader.frag.spv
```

Integrated as a pre-build event in the `.vcxproj`.

---

## Progress Log

*Update this section as milestones are completed.*

| Date | Milestone | Notes |
|------|-----------|-------|
| — | Project plan created | Architecture defined, phases outlined |
