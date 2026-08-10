find_package(glfw3 3.3 REQUIRED)
# Only the Vulkan headers are needed (for the VkSurfaceKHR type); the module
# does not link against the Vulkan loader.
find_package(Vulkan REQUIRED)

bdex_add_module(window_glfw
    SOURCES
        src/GlfwWindowModule.cpp
    DEPS
        bdex::core
        bdex::platform
        glfw
        Vulkan::Headers)
