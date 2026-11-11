# bdex Game Engine

A modular 3D game engine written in **C++23**, featuring a **Vulkan 1.3** backend, an entity component system, and a sandbox for exploring rendering and simulation.

The project separates engine interfaces from their implementations: windowing, graphics backends, and rendering are modules selected at startup. The same application can display a scene in a window, render an image offscreen, or run its simulation without a GPU.

**Project status:** in development, currently version `0.1.0`. The repository includes the engine, its modules, a sample application, and unit and integration tests.

## Features

- **Modular core:** plugin loading, module dependency resolution, a service registry, an event bus, and INI configuration.
- **Simulation loop:** ordered execution phases, fixed timestep updates, and a multithreaded job system.
- **ECS and scenes:** entities with generational handles, application-defined components, typed views, and hierarchical transforms.
- **3D rendering:** perspective cameras, meshes, textures, materials, and Blinn-Phong directional lighting through an extensible forward renderer.
- **Vulkan backend:** dynamic rendering, synchronization2, memory management with Vulkan Memory Allocator, and PNG screenshots.
- **Assets:** a virtual file system, asset caching, OBJ, TGA, and SPIR-V loaders, plus procedural cubes, planes, spheres, and textures.
- **Headless execution:** offscreen Vulkan rendering or a `null` graphics backend for tests without a GPU.

## Architecture

Libraries in `engine/` expose the APIs. Modules in `modules/` implement them and communicate through engine services. Their initialization order is determined by their declared dependencies.

```text
engine/
├── core/       Modules, services, events, configuration, and jobs
├── ecs/        Entities, components, and views
├── platform/   Window interfaces and input handling
├── rhi/        Render hardware abstraction
├── assets/     Asset loading, caching, and generation
├── scene/      Scenes and transform hierarchies
├── render/     Rendering interfaces, cameras, and graphics components
└── app/        Configuration, initialization, and application loop
modules/        Implementations of engine interfaces
samples/        3D sandbox and sample shaders
tests/          Unit and integration tests
cmake/          Helpers for building libraries, modules, and shaders
```

| Module | Purpose |
| --- | --- |
| `window_glfw` | Interactive window and keyboard/mouse input through GLFW. |
| `window_null` | Virtual window with no display. |
| `rhi_vulkan` | Vulkan 1.3 graphics backend. |
| `rhi_null` | Backend with no GPU execution, used for testing. |
| `renderer_forward` | Scene rendering and support for custom render features. |

By default, modules are shared libraries loaded at runtime. Set the CMake option `BDEX_SHARED_MODULES=OFF` to link them statically and register their factories in the executable.

## Requirements

To build the default configuration:

| Dependency | Purpose |
| --- | --- |
| C++23 compiler and standard library | Including `std::expected` and `std::format`. |
| CMake ≥ 3.25 | Project configuration. |
| Make or Ninja | Building, depending on the selected CMake generator. |
| GLM | Engine mathematics. |
| GLFW ≥ 3.3 | Interactive window module. |
| Vulkan headers and loader library | Building the Vulkan and GLFW modules. |
| `glslc` | Compiling GLSL shaders to SPIR-V. |
| GoogleTest, optional | Building the tests. |

CMake first looks for a system installation of **Vulkan Memory Allocator**. If none is found, it downloads version `3.3.0`, so the initial configuration requires network access in that case.

Vulkan rendering requires a **Vulkan 1.3** compatible driver with dynamic rendering and synchronization2 support. Install `VK_LAYER_KHRONOS_validation` to enable Vulkan validation diagnostics.

## Build and run

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
./build/bin/sandbox
```

Executables, shared libraries, and modules are placed in `build/bin/`. Shaders are compiled automatically into `build/bin/assets/`, and the sandbox configuration is copied to `build/bin/sandbox.ini`.

### The sandbox

The demo features a textured ground plane, a sphere, animated cubes, and two moons attached to a rotating pivot. A procedural sky demonstrates how applications can add custom render features. The window title displays FPS and average frame time.

| Control | Action |
| --- | --- |
| Hold the right mouse button | Look around. |
| `W` / `A` / `S` / `D` | Move the camera. |
| `Space` / `Left Ctrl` | Move up / down. |
| `Left Shift` | Move faster. |
| `F2` | Save a screenshot as `screenshot_<frame>.png`. |
| `Escape` | Quit. |

Screenshots are saved in the current working directory.

### Run without a window

Capture an image using the offscreen Vulkan backend:

```sh
./build/bin/sandbox --headless --frames 3 --screenshot /tmp/bdex-sandbox.png
```

`--headless` replaces the window module with `window_null` and keeps the configured graphics backend. With the default configuration, this mode still requires Vulkan.

Run the same application with the `null` backend, without a GPU:

```sh
./build/bin/sandbox --modules window_null,rhi_null,renderer_forward --frames 120
```

To run the simulation alone, load only the virtual window module:

```sh
./build/bin/sandbox --modules window_null --frames 120
```

## Configuration

Configuration is applied in the following order, from lowest to highest priority: engine defaults, application defaults, the INI file, then command-line arguments.

The sandbox uses [sandbox.ini](samples/sandbox/sandbox.ini), which CMake copies next to the executable during configuration:

```ini
[engine]
modules = window_glfw, rhi_vulkan, renderer_forward

[window]
title = bdex sandbox
width = 1280
height = 720

[renderer]
vsync = true
```

Override any key at startup with `--set`:

```sh
./build/bin/sandbox --set window.width=1920 --set window.height=1080 --set renderer.vsync=false
```

| Option | Effect |
| --- | --- |
| `--config <file>` | Load a different INI file. |
| `--set <key>=<value>` | Override a configuration key; may be repeated. |
| `--modules <a,b,c>` | Select the modules to load. |
| `--headless` | Use the virtual window module. |
| `--frames <n>` | Stop after the given number of frames. |
| `--screenshot <file>` | Save a PNG screenshot; limits the run to three frames if no frame limit is set. |
| `--log-level <level>` | Choose from `trace`, `debug`, `info`, `warn`, `error`, and `off`. |
| `--strict` | Return exit code `3` if the graphics backend reports validation errors. |
| `--help` | Show usage information. |

Request Vulkan diagnostics during a headless run:

```sh
./build/bin/sandbox --headless --frames 120 --set rhi.validation=true --strict
```

The validation layer must be installed for these diagnostics to be active.

## Build options

Pass options to CMake as `-DOPTION=ON` or `-DOPTION=OFF`.

| Option | Default | Effect |
| --- | --- | --- |
| `BDEX_SHARED_MODULES` | `ON` | Build shared libraries and modules; use `OFF` for static linking. |
| `BDEX_BUILD_SAMPLES` | `ON` | Build the sandbox. |
| `BDEX_BUILD_TESTS` | `ON` | Build tests if GoogleTest is available. |
| `BDEX_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors. |

Each module can also be disabled independently: `BDEX_MODULE_WINDOW_GLFW`, `BDEX_MODULE_WINDOW_NULL`, `BDEX_MODULE_RHI_VULKAN`, `BDEX_MODULE_RHI_NULL`, and `BDEX_MODULE_RENDERER_FORWARD`. All are enabled by default. The modules selected at runtime must be available in the build.

## Tests

With GoogleTest installed and `BDEX_BUILD_TESTS=ON`:

```sh
ctest --test-dir build --output-on-failure
```

Tests cover the core, plugin loading, ECS, assets, scenes, and renderer integration with the `null` and Vulkan backends. Vulkan tests run offscreen and are skipped if no usable Vulkan device is available.

If GoogleTest is missing, CMake prints a warning and continues configuration without building the tests.

## Extending the engine

- **Create an application:** derive from [`bdex::app::Application`](engine/app/include/bdex/app/Application.hpp), implement the callbacks you need (`onStart`, `onUpdate`, `onFixedUpdate`, `onStop`), and define the entry point with `BDEX_MAIN`.
- **Add gameplay:** define components and iterate over [`ecs::World`](engine/ecs/include/bdex/ecs/World.hpp) views, then register your systems with the scheduler.
- **Add a render feature:** implement `IRenderFeature` and register it with the renderer. The [sandbox sky](samples/sandbox/src/SkyFeature.cpp) provides a complete example.
- **Add an asset format:** register a loader with [`AssetManager`](engine/assets/include/bdex/assets/AssetManager.hpp).

The [sandbox application](samples/sandbox/src/Sandbox.cpp) brings these mechanisms together in a working example.
