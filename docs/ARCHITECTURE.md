# Architecture

This document explains how the engine is put together and how to extend it. The [README](../README.md) covers building and running.

## The idea in one paragraph

The engine is a small **kernel** plus **modules**. The kernel knows nothing about windows, GPUs, or scenes: it only offers four mechanisms (modules, services, events, scheduled tasks) and a loop that runs the phases of a frame. Everything visible - the window, the Vulkan device, the renderer - is a module that implements an interface declared in an engine library. Modules never link against each other; they meet through interfaces registered in the kernel. Replacing a backend is therefore a configuration change, not a code change.

```text
                 ┌──────────────────────────────────────────────┐
  application    │ sandbox (bdex::app::Application)             │
                 └──────────────────────┬───────────────────────┘
                                        │ uses interfaces only
                 ┌──────────────────────▼───────────────────────┐
  engine         │ app                                          │
  libraries      │ render    scene    assets                    │
  (interfaces    │ rhi       platform ecs                       │
   + shared      │ core  ← kernel: modules, services, events,   │
   logic)        │         scheduler, config, jobs, filesystem  │
                 └──────────────────────▲───────────────────────┘
                                        │ implement interfaces
                 ┌──────────────────────┴───────────────────────┐
  modules        │ window_glfw   rhi_vulkan   renderer_forward  │
  (plugins)      │ window_null   rhi_null                       │
                 └──────────────────────────────────────────────┘
```

## Engine libraries

Each library is one CMake target (`bdex::<name>`) with its public headers under `include/bdex/<name>/`. A library only depends on the ones listed for it; there are no cycles.

| Library | Contents | Depends on |
| --- | --- | --- |
| `core` | `Engine`, `ModuleManager`, `ServiceRegistry`, `EventBus`, `Scheduler`, `Config`, `JobSystem`, `VirtualFileSystem`, handles, logging, math aliases | GLM |
| `ecs` | `World`, sparse-set component pools, views | `core` |
| `platform` | `IWindowSystem`, `IWindow`, input events, `InputState` | `core` |
| `rhi` | `IDevice`, `ICommandList`, resource descriptions | `core`, `platform` |
| `assets` | `MeshData`, `ImageData`, loaders, `AssetManager`, procedural primitives | `core` |
| `scene` | `Scene`, `SceneManager`, `Transform`, hierarchy | `core`, `ecs` |
| `render` | `IRenderer`, `IRenderFeature`, render components, shared GLSL headers | `core`, `ecs`, `rhi`, `assets`, `scene` |
| `app` | `Application`: configuration, services, module loading, main loop | all of the above |

## The kernel

`bdex::Engine` (in `core`) owns the four mechanisms modules are built from.

### Modules

A module implements `IModule`: a description (`info()`) and two lifecycle hooks (`onInit`, `onShutdown`). `ModuleManager::load(name)` finds a module by name, either in the static registry or as a plugin `bdex_module_<name>` next to the executable; application code is identical in both cases.

Initialization order is computed from what modules declare, not from the order in which they are listed. Dependencies are expressed on **capabilities** rather than module names:

| Field | Meaning |
| --- | --- |
| `provides` | Capabilities the module offers, e.g. `"rhi"`. A module always provides its own name. |
| `dependencies` | Capabilities that must be provided by another loaded module. Missing one is a startup error. |
| `after` | Capabilities to be initialized after *if present*. Not an error when absent. |

`renderer_forward` depends on `"rhi"`, which `rhi_vulkan` and `rhi_null` both provide. `rhi_vulkan` declares `after = {"window"}`: with a window module it initializes second (it needs the window system's instance extensions), without one it simply renders offscreen. Cycles and missing capabilities are reported with the names involved. Shutdown runs in reverse order.

### Services

`ServiceRegistry` maps an **interface type** to an implementation:

```cpp
engine.services().provide<rhi::IDevice>(device);         // in rhi_vulkan
rhi::IDevice& device = engine.services().get<rhi::IDevice>();  // anywhere else
```

Consumers see the interface only. `get` aborts with the missing interface's name; `tryGet` returns null for optional services.

### Events

`EventBus` delivers plain structs to subscribers. `subscribe` returns an RAII `Subscription`; `publish` dispatches immediately, `enqueue` defers until the middle of the frame. The window module publishes `KeyEvent`, `WindowResized`...; `InputState` and the application subscribe without knowing which module emitted them.

### Scheduled tasks

The main loop does nothing but run phases. Behaviour is added as tasks:

```cpp
engine.scheduler().add(Phase::Update, "game.ai", [](const FrameTime& time) { ... });
```

| Phase | Typical use | Registered by |
| --- | --- | --- |
| `BeginFrame` | | |
| `Input` | Reset input transitions, poll OS events | window module |
| `PreUpdate` | | |
| `FixedUpdate` | Runs 0..n times with a constant delta (`engine.fixed_timestep`) | application |
| `Update` | Gameplay | application |
| `PostUpdate` | Derived data; world transforms run here with order 1000 | `app` |
| *(queued events are dispatched)* | | |
| `PreRender` | | |
| `Render` | Draw the active scene | renderer module |
| `PostRender` | | |
| `EndFrame` | | |

Within a phase, tasks run by ascending `order`, then by registration order.

### Identity across plugin boundaries

Services, events, and ECS components are keyed by `bdex::typeId<T>()`, a hash of the type's **name** computed at compile time. The same type therefore has the same id in the executable and in every plugin, without relying on RTTI or on static variables being shared between shared libraries. A plugin also exports its ABI version (`kModuleAbiVersion`); a mismatch is rejected at load time.

## Writing a module

A module is a directory under `modules/` with one `CMakeLists.txt` and its sources. The example below adds a module providing a new capability.

**1. Declare the interface** in an engine library (or reuse an existing one), so that consumers can include it without depending on the module:

```cpp
// engine/audio/include/bdex/audio/Audio.hpp
#include <string_view>

namespace bdex::audio {
class IAudio {
public:
    virtual ~IAudio() = default;
    virtual void play(std::string_view sound) = 0;
};
}
```

**2. Implement the module:**

```cpp
// modules/audio_null/src/AudioNullModule.cpp
#include <bdex/audio/Audio.hpp>
#include <bdex/core/Engine.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/core/Module.hpp>

#include <memory>

namespace bdex {
namespace {

class NullAudio final : public audio::IAudio {
public:
    void play(std::string_view sound) override { log::debug("audio", "play {}", sound); }
};

class AudioNullModule final : public IModule {
public:
    ModuleInfo info() const override {
        return ModuleInfo{
            .name = BDEX_MODULE_NAME_STRING,   // injected by bdex_add_module()
            .description = "Silent audio backend",
            .provides = {"audio"},
            .dependencies = {},
            .after = {},
        };
    }

    Result<> onInit(Engine& engine) override {
        if (!engine.config().getBool("audio.enabled", true)) {
            return fail("audio is disabled");   // startup stops with this message
        }
        audio_ = std::make_shared<NullAudio>();
        engine.services().provide<audio::IAudio>(audio_);
        return {};
    }

    void onShutdown(Engine& engine) override {
        engine.services().remove<audio::IAudio>();
        audio_.reset();
    }

private:
    std::shared_ptr<NullAudio> audio_;
};

}  // namespace
}  // namespace bdex

BDEX_DEFINE_MODULE(bdex::AudioNullModule)
```

**3. Build it:**

```cmake
# modules/audio_null/CMakeLists.txt
bdex_add_module(audio_null
    SOURCES src/AudioNullModule.cpp
    DEPS bdex::core bdex::audio)
```

Add `add_subdirectory(audio_null)` to `modules/CMakeLists.txt`.

**4. Make it available to an application and load it:**

```cmake
bdex_target_modules(sandbox window_glfw rhi_vulkan renderer_forward audio_null)
```

```ini
[engine]
modules = window_glfw, rhi_vulkan, renderer_forward, audio_null
```

`bdex_target_modules` adds a build dependency in plugin builds, and links and registers the module in static builds. The executable itself needs no change.

### Rules for modules

- Link against engine libraries only, never against another module.
- Talk to the rest of the engine through `Engine`: services, events, tasks, configuration.
- Release in `onShutdown` everything registered in `onInit` (services, tasks, subscriptions). `ScopedTask` and `Subscription` make this automatic.
- Read settings from your own configuration section and document them at the top of the module's source file.
- Recoverable failures return `Result` with a message; `BDEX_ASSERT` is for programming errors.

[`tests/plugins/TestPlugin.cpp`](../tests/plugins/TestPlugin.cpp) is a complete minimal module, and [`modules/rhi_null`](../modules/rhi_null/src/NullRhiModule.cpp) is the smallest real backend.

## Rendering

### RHI

`rhi::IDevice` is a small, explicit description of a GPU: buffers, textures, samplers, shaders, bind groups (descriptor sets), graphics pipelines, and swapchains, all referred to by typed generational handles. A renderer is written against it once and runs on any backend module.

Frame protocol:

```cpp
rhi::ICommandList& commands = device.beginFrame();
rhi::TextureHandle target = device.acquireSwapchainTexture(swapchain);
if (target) {                       // invalid while minimized or resizing
    commands.beginRendering({colorAttachments, &depthAttachment});
    ...
    commands.endRendering();
}
device.endFrame();                  // submits and presents
```

What the backend takes care of, so that renderers do not have to:

- **Resource states.** Image layouts are tracked per texture; attachments, sampled textures, and presented images are transitioned automatically.
- **Lifetimes.** `destroy*` is always safe: the object is released once no in-flight frame can reference it.
- **Resizing.** The swapchain follows its window and is rebuilt when out of date.

The Vulkan backend uses Vulkan 1.3 dynamic rendering (no render pass or framebuffer objects), synchronization2, and Vulkan Memory Allocator. Clip space follows Vulkan conventions (Y down, depth in `[0, 1]`); `render::makeCameraData` flips the projection so the world stays Y-up with counter-clockwise front faces.

The window backend, not the RHI, creates the presentation surface (`IWindow::createVulkanSurface`), because it is the only component that knows the native windowing API. `rhi_vulkan` therefore has no dependency on GLFW, X11, or Wayland.

### Renderer and features

`render::IRenderer` owns meshes, textures, and materials (returned as handles that ECS components store) and draws the active scene during `Phase::Render`. `renderer_forward` renders one color + depth pass; **what** is drawn in that pass is decided by render features, sorted by `order()`:

| Order | Feature | Defined in |
| --- | --- | --- |
| -100 | `sandbox.sky` | the sandbox application |
| 0 | `forward.meshes` | `renderer_forward` |

A feature implements `IRenderFeature` (`onAttach`, `render`, `onDetach`) and is added with `IRenderer::addFeature`. Every feature receives the per-frame constants as bind group 0; shaders get the matching declaration with `#include "bdex/frame_uniforms.glsl"`.

Without a presentable window the renderer draws into an offscreen target; screenshots read that target back.

## Build system

`cmake/BdexEngine.cmake` provides the building blocks:

| Function | Purpose |
| --- | --- |
| `bdex_add_library(name ...)` | Engine library `bdex::<name>`. |
| `bdex_add_module(name ...)` | Module `bdex_module_<name>`: plugin or static library depending on `BDEX_SHARED_MODULES`. |
| `bdex_target_modules(target modules...)` | Makes modules available to an executable (build dependency, or link + static registration). |
| `bdex_compile_shaders(target ...)` | GLSL to SPIR-V with `glslc`, with `#include` dependency tracking. |

Runtime layout, relative to the executable: plugins sit next to it, `assets/engine/` is mounted as `engine:/`, and `assets/<app.name>/` as `app:/`.

## Current limits

Known gaps, listed so they are not mistaken for design decisions:

- One window; `IWindowSystem` exposes a primary window only.
- The forward renderer has a single pass: no shadows, post-processing, transparency sorting, culling, or instancing.
- The RHI is main-thread only, and bind groups come from one fixed-size descriptor pool.
- Image loading supports TGA only; the PNG writer stores data uncompressed.
- No audio, physics, or scripting modules yet.
- Only built and run on Linux (GCC and Clang). Windows and macOS code paths exist (library loading, executable path, export macros) but are untested.
