// End-to-end tests: the real engine loop with real modules, selected by name
// exactly like an application would. The window and RHI backends are the
// "null" ones, so these run anywhere; the last test repeats the exercise on a
// real GPU when one is available.

#include <bdex/assets/AssetManager.hpp>
#include <bdex/core/Engine.hpp>
#include <bdex/core/FileSystem.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/platform/Events.hpp>
#include <bdex/platform/Input.hpp>
#include <bdex/platform/Window.hpp>
#include <bdex/render/Renderer.hpp>
#include <bdex/rhi/Device.hpp>
#include <bdex/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace bdex;

namespace {

/// An engine wired like app::Application does it, minus the main loop.
struct TestEngine {
    explicit TestEngine(const std::vector<std::string>& modules, Config config = {}) {
        config.setDefault("engine.forced_delta", "0.016");
        config.setDefault("window.width", "320");
        config.setDefault("window.height", "200");
        engine = std::make_unique<Engine>(config);

        auto vfs = std::make_shared<VirtualFileSystem>();
        vfs->mount("engine", fs::executableDirectory() / "assets" / "engine");
        engine->services().provide<VirtualFileSystem>(vfs);
        engine->services().provide<assets::AssetManager>(std::make_shared<assets::AssetManager>(*vfs));
        auto sceneManager = std::make_shared<scene::SceneManager>();
        scenes = sceneManager.get();
        scenes->create("main");
        engine->services().provide<scene::SceneManager>(sceneManager);

        engine->scheduler().add(Phase::PostUpdate, "world_transforms", [this](const FrameTime&) {
            scene::updateWorldTransforms(world());
        });

        engine->modules().addSearchPath(fs::executableDirectory());
        for (const std::string& module : modules) {
            if (const auto loaded = engine->modules().load(module); !loaded) {
                loadError = loaded.error().message;
                return;
            }
        }
        if (const auto initialized = engine->initialize(); !initialized) {
            initError = initialized.error().message;
        }
    }

    [[nodiscard]] bool ready() const { return loadError.empty() && initError.empty(); }
    [[nodiscard]] ecs::World& world() { return scenes->active()->world(); }
    [[nodiscard]] scene::Scene& scene() { return *scenes->active(); }
    [[nodiscard]] rhi::IDevice& device() { return engine->services().get<rhi::IDevice>(); }
    [[nodiscard]] render::IRenderer& renderer() {
        return engine->services().get<render::IRenderer>();
    }

    /// Adds a camera looking at the origin and `count` cubes in front of it.
    std::vector<ecs::Entity> populate(u32 count) {
        const ecs::Entity camera = scene().createEntity("camera");
        world().emplace<render::Camera>(camera);
        world().get<scene::Transform>(camera).position = Vec3(0.0f, 0.0f, 6.0f);

        const render::MeshHandle cube = renderer().createMesh(assets::makeCube());
        render::MaterialDesc red;
        red.baseColor = Vec4(1.0f, 0.2f, 0.1f, 1.0f);
        const render::MaterialHandle material = renderer().createMaterial(red);
        std::vector<ecs::Entity> entities;
        for (u32 i = 0; i < count; ++i) {
            const ecs::Entity entity = scene().createEntity("cube");
            world().get<scene::Transform>(entity).position =
                Vec3(static_cast<f32>(i) * 1.5f - 1.5f, 0.0f, 0.0f);
            world().emplace<render::MeshRenderer>(entity, cube, material);
            entities.push_back(entity);
        }
        return entities;
    }

    std::unique_ptr<Engine> engine;
    scene::SceneManager* scenes = nullptr;
    std::string loadError;
    std::string initError;
};

/// Render feature recording when it is invoked.
class ProbeFeature final : public render::IRenderFeature {
public:
    ProbeFeature(std::string name, i32 order, std::vector<std::string>& trace)
        : name_(std::move(name)), order_(order), trace_(&trace) {}

    [[nodiscard]] std::string_view name() const override { return name_; }
    [[nodiscard]] i32 order() const override { return order_; }
    Result<> onAttach(const render::RenderSetupContext&) override {
        trace_->push_back("attach:" + name_);
        return {};
    }
    void onDetach(rhi::IDevice&) override { trace_->push_back("detach:" + name_); }
    void render(const render::RenderFrameContext& context) override {
        trace_->push_back("render:" + name_);
        lastExtent = context.extent;
    }

    Extent2D lastExtent;

private:
    std::string name_;
    i32 order_;
    std::vector<std::string>* trace_;
};

const std::vector<std::string> kNullStack = {"renderer_forward", "rhi_null", "window_null"};

u32 readBigEndian(const std::vector<u8>& bytes, usize offset) {
    return (static_cast<u32>(bytes[offset]) << 24) | (static_cast<u32>(bytes[offset + 1]) << 16) |
           (static_cast<u32>(bytes[offset + 2]) << 8) | bytes[offset + 3];
}

/// Decodes a PNG produced by assets::encodePng (single IDAT, stored deflate
/// blocks, filter 0), which is all the engine ever writes.
assets::ImageData decodeEnginePng(const std::vector<u8>& png) {
    assets::ImageData image;
    image.width = readBigEndian(png, 16);
    image.height = readBigEndian(png, 20);

    const usize idatLength = readBigEndian(png, 33);
    usize cursor = 41 + 2;  // IDAT payload, after the 2-byte zlib header
    const usize end = 41 + idatLength - 4;  // before the adler32
    std::vector<u8> raw;
    while (cursor < end) {
        const usize blockSize = png[cursor + 1] | (static_cast<usize>(png[cursor + 2]) << 8);
        cursor += 5;
        raw.insert(raw.end(), png.begin() + static_cast<std::ptrdiff_t>(cursor),
                   png.begin() + static_cast<std::ptrdiff_t>(cursor + blockSize));
        cursor += blockSize;
    }

    const usize rowBytes = static_cast<usize>(image.width) * 4;
    for (u32 y = 0; y < image.height; ++y) {
        const auto row = raw.begin() + static_cast<std::ptrdiff_t>(y * (rowBytes + 1) + 1);
        image.pixels.insert(image.pixels.end(), row, row + static_cast<std::ptrdiff_t>(rowBytes));
    }
    return image;
}

}  // namespace

TEST(HeadlessEngine, ModulesAreOrderedByCapabilitiesNotByLoadOrder) {
    TestEngine test(kNullStack);  // deliberately listed backwards
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;

    EXPECT_EQ(test.engine->modules().initializationOrder(),
              (std::vector<std::string>{"window_null", "rhi_null", "renderer_forward"}));
    EXPECT_EQ(test.device().info().backend, "null");
    EXPECT_TRUE(test.engine->services().has<render::IRenderer>());
    EXPECT_TRUE(test.engine->services().has<platform::InputState>());
}

TEST(HeadlessEngine, RendererRefusesToStartWithoutAnRhi) {
    TestEngine test({"window_null", "renderer_forward"});
    EXPECT_TRUE(test.loadError.empty());
    EXPECT_NE(test.initError.find("'rhi'"), std::string::npos) << test.initError;
}

TEST(HeadlessEngine, RendererWorksWithoutAnyWindowModule) {
    TestEngine test({"rhi_null", "renderer_forward"});
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    test.populate(2);
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().drawCalls, 2u);
    EXPECT_EQ(test.renderer().renderExtent(), (Extent2D{1280, 720}));  // renderer.* defaults
}

TEST(HeadlessEngine, DrawsOneCallPerVisibleMeshEntity) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    const std::vector<ecs::Entity> cubes = test.populate(3);

    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().drawCalls, 3u);
    EXPECT_EQ(test.device().lastFrameStats().triangles, 36u);
    EXPECT_EQ(test.renderer().renderExtent(), (Extent2D{320, 200}));

    test.world().get<render::MeshRenderer>(cubes[0]).visible = false;
    test.world().destroy(cubes[1]);
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().drawCalls, 1u);
}

TEST(HeadlessEngine, DestroyedRenderResourcesAreSkippedNotDereferenced) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    const std::vector<ecs::Entity> cubes = test.populate(2);

    const render::MeshRenderer renderable = test.world().get<render::MeshRenderer>(cubes[0]);
    test.renderer().destroyMaterial(renderable.material);  // falls back to the default material
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().drawCalls, 2u);

    test.renderer().destroyMesh(renderable.mesh);  // stale mesh handles are ignored
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().drawCalls, 0u);
}

TEST(HeadlessEngine, ApplicationsExtendTheRendererWithFeatures) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    test.populate(1);

    std::vector<std::string> trace;
    auto overlay = std::make_unique<ProbeFeature>("overlay", 50, trace);
    ProbeFeature* overlayPtr = overlay.get();
    ASSERT_TRUE(test.renderer().addFeature(std::move(overlay)).has_value());
    ASSERT_TRUE(
        test.renderer().addFeature(std::make_unique<ProbeFeature>("background", -50, trace)).has_value());
    // Names are unique.
    EXPECT_FALSE(
        test.renderer().addFeature(std::make_unique<ProbeFeature>("overlay", 0, trace)).has_value());

    trace.clear();
    test.engine->tick();
    EXPECT_EQ(trace, (std::vector<std::string>{"render:background", "render:overlay"}));
    EXPECT_EQ(overlayPtr->lastExtent, (Extent2D{320, 200}));

    EXPECT_TRUE(test.renderer().removeFeature("background"));
    EXPECT_FALSE(test.renderer().removeFeature("background"));
    trace.clear();
    test.engine->tick();
    EXPECT_EQ(trace, (std::vector<std::string>{"render:overlay"}));

    // Remaining features are detached when the renderer module shuts down.
    trace.clear();
    test.engine->shutdown();
    EXPECT_EQ(trace, (std::vector<std::string>{"detach:overlay"}));
}

TEST(HeadlessEngine, InputStateIsDrivenByEvents) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    auto& input = test.engine->services().get<platform::InputState>();
    using platform::Key;

    // Events arriving during a frame are visible to that frame's Update.
    std::vector<std::string> seen;
    test.engine->scheduler().add(Phase::Update, "probe", [&](const FrameTime&) {
        seen.push_back(std::string(input.keyPressed(Key::W) ? "pressed " : "") +
                       (input.keyDown(Key::W) ? "down" : "up"));
    });
    test.engine->scheduler().add(
        Phase::Input, "inject",
        [&](const FrameTime& time) {
            if (time.frame == 0) {
                test.engine->events().publish(platform::KeyEvent{Key::W, platform::KeyAction::Press});
                test.engine->events().publish(platform::MouseMoved{10.0, 10.0});
                test.engine->events().publish(platform::MouseMoved{14.0, 7.0});
            } else if (time.frame == 2) {
                test.engine->events().publish(
                    platform::KeyEvent{Key::W, platform::KeyAction::Release});
            }
        },
        100);  // after the window module's own Input task

    test.engine->tick();
    EXPECT_EQ(input.mouseDelta(), Vec2(4.0f, -3.0f));
    test.engine->tick();
    EXPECT_EQ(input.mouseDelta(), Vec2(0.0f));
    test.engine->tick();
    EXPECT_EQ(seen, (std::vector<std::string>{"pressed down", "down", "up"}));
}

TEST(HeadlessEngine, WindowCloseRequestReachesSubscribers) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;

    const Subscription subscription =
        test.engine->events().subscribe<platform::WindowCloseRequested>(
            [&](const platform::WindowCloseRequested&) { test.engine->requestExit(); });

    auto* window = test.engine->services().get<platform::IWindowSystem>().primaryWindow();
    ASSERT_NE(window, nullptr);
    window->requestClose();
    EXPECT_FALSE(test.engine->tick());  // the queued event is delivered during the frame
}

TEST(HeadlessEngine, ScreenshotIsWrittenAndAnnounced) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    test.populate(1);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "bdex_tests_null_capture.png";
    std::filesystem::remove(path);

    bool announced = false;
    const Subscription subscription = test.engine->events().subscribe<render::ScreenshotSaved>(
        [&](const render::ScreenshotSaved& event) { announced = event.success; });

    test.renderer().requestScreenshot(path);
    test.engine->tick();

    EXPECT_TRUE(announced);
    const auto bytes = fs::readBinaryFile(path);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_GT(bytes->size(), 8u);
    EXPECT_EQ(std::string(bytes->begin() + 1, bytes->begin() + 4), "PNG");
    std::filesystem::remove(path);
}

// --- Real GPU -------------------------------------------------------------------------
// Same engine, same renderer module, same scene: only the RHI module name
// changes. Skipped on machines without a usable Vulkan 1.3 device.

TEST(VulkanHeadless, RendersTheSceneOffscreenWithoutValidationErrors) {
    Config config;
    config.set("rhi.validation", "true");
    log::setLevel(log::Level::Warn);  // keep the test output focused
    TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
    log::setLevel(log::Level::Info);
    if (!test.ready()) {
        GTEST_SKIP() << "Vulkan is not available here: " << test.loadError << test.initError;
    }
    EXPECT_EQ(test.device().info().backend, "vulkan");

    test.renderer().setClearColor(Vec4(0.0f, 0.0f, 1.0f, 1.0f));
    test.populate(3);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "bdex_tests_vulkan_capture.png";
    std::filesystem::remove(path);

    // A few frames so both frame slots and deferred releases are exercised.
    for (int i = 0; i < 4; ++i) {
        test.engine->tick();
    }
    EXPECT_EQ(test.device().lastFrameStats().drawCalls, 3u);

    test.renderer().requestScreenshot(path);
    test.engine->tick();
    const auto png = fs::readBinaryFile(path);
    ASSERT_TRUE(png.has_value());
    std::filesystem::remove(path);

    // Check what was actually rendered: the corner shows the clear color, the
    // center shows the middle cube (red-ish material, lit).
    const assets::ImageData frame = decodeEnginePng(*png);
    ASSERT_EQ(frame.width, 320u);
    ASSERT_EQ(frame.height, 200u);
    ASSERT_TRUE(frame.valid());
    const auto pixel = [&](u32 x, u32 y) { return frame.pixels.data() + (y * frame.width + x) * 4; };

    const u8* corner = pixel(2, 2);
    EXPECT_EQ(corner[0], 0);
    EXPECT_EQ(corner[1], 0);
    EXPECT_EQ(corner[2], 255);
    EXPECT_EQ(corner[3], 255);

    const u8* center = pixel(frame.width / 2, frame.height / 2);
    EXPECT_GT(center[0], 60);          // lit
    EXPECT_GT(center[0], center[1]);   // red dominates
    EXPECT_GT(center[0], center[2]);

    EXPECT_EQ(test.device().validationErrorCount(), 0u);

    test.engine->shutdown();  // must release every GPU object cleanly
}

TEST(VulkanHeadless, RhiRoundTripsTextureData) {
    Config config;
    config.set("rhi.validation", "true");
    log::setLevel(log::Level::Warn);
    TestEngine test({"rhi_vulkan"}, config);
    log::setLevel(log::Level::Info);
    if (!test.ready()) {
        GTEST_SKIP() << "Vulkan is not available here: " << test.loadError << test.initError;
    }
    rhi::IDevice& device = test.device();

    // Upload a known image, read it back, compare byte for byte.
    const assets::ImageData image =
        assets::makeCheckerboard(64, 4, {255, 0, 0, 255}, {0, 64, 255, 128});
    rhi::TextureDesc desc;
    desc.width = image.width;
    desc.height = image.height;
    desc.format = rhi::Format::RGBA8Unorm;
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc;
    desc.mipLevels = 0;  // full chain: exercises mip generation as well
    desc.debugName = "round trip";
    const rhi::TextureHandle texture = device.createTexture(desc);
    device.writeTexture(texture, rhi::asBytes(image.pixels));

    EXPECT_EQ(device.readTexture(texture), image.pixels);  // level 0 is untouched
    EXPECT_EQ(device.textureDesc(texture).width, 64u);
    EXPECT_EQ(device.textureDesc(texture).mipLevels, 7u);  // 64, 32, 16, 8, 4, 2, 1

    device.destroyTexture(texture);
    EXPECT_EQ(device.textureDesc(texture).width, 0u);  // stale handle
    EXPECT_EQ(device.validationErrorCount(), 0u);
}
