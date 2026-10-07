# Compile the mesh feature before linking the renderer module.
add_library(bdex_forward_objects OBJECT src/MeshFeature.cpp)
target_include_directories(bdex_forward_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bdex_forward_objects PRIVATE
    bdex::core bdex::ecs bdex::platform bdex::rhi bdex::assets bdex::scene bdex::render)
bdex_configure_target(bdex_forward_objects)

# Shaders are loaded at runtime from "engine:/shaders/...".
bdex_compile_shaders(bdex_renderer_forward_shaders
    OUTPUT_DIR ${BDEX_ASSET_OUTPUT_DIR}/engine/shaders
    SOURCES
        shaders/mesh.vert
        shaders/mesh.frag)
add_dependencies(bdex_forward_objects bdex_renderer_forward_shaders)
