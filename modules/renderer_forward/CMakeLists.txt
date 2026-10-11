bdex_add_module(renderer_forward
    SOURCES
        src/ForwardRenderer.cpp
        src/ForwardRendererModule.cpp
        src/MeshFeature.cpp
    DEPS
        bdex::core
        bdex::ecs
        bdex::platform
        bdex::rhi
        bdex::assets
        bdex::scene
        bdex::render)

# Shaders are loaded at runtime from "engine:/shaders/...".
bdex_compile_shaders(bdex_renderer_forward_shaders
    OUTPUT_DIR ${BDEX_ASSET_OUTPUT_DIR}/engine/shaders
    SOURCES
        shaders/mesh.vert
        shaders/mesh.frag)
add_dependencies(bdex_module_renderer_forward bdex_renderer_forward_shaders)
