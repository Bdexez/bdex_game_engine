add_executable(sandbox
    src/Sandbox.cpp
    src/SkyFeature.cpp)
target_link_libraries(sandbox PRIVATE bdex::app)
bdex_configure_target(sandbox)

# The executable does not link against any module: these are the ones it can
# load at runtime (plugins), or that get registered statically when
# BDEX_SHARED_MODULES is OFF. Which ones actually run is decided by
# `engine.modules` in sandbox.ini / on the command line.
bdex_target_modules(sandbox
    window_glfw
    window_null
    rhi_vulkan
    rhi_null
    renderer_forward)

# Application shaders, loaded from "app:/shaders/...".
bdex_compile_shaders(sandbox_shaders
    OUTPUT_DIR ${BDEX_ASSET_OUTPUT_DIR}/sandbox/shaders
    SOURCES
        shaders/sky.vert
        shaders/sky.frag)
add_dependencies(sandbox sandbox_shaders)

# Default configuration, picked up next to the executable.
configure_file(sandbox.ini ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/sandbox.ini COPYONLY)
