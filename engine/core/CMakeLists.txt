bdex_add_library(core
    SOURCES
        src/Assert.cpp
        src/Config.cpp
        src/EventBus.cpp
        src/FileSystem.cpp
        src/Log.cpp
    PUBLIC_DEPS
        glm::glm
    PRIVATE_DEPS
        ${CMAKE_DL_LIBS})

# One projection / angle convention for the whole engine.
target_compile_definitions(bdex_core PUBLIC
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL)

find_package(Threads REQUIRED)
target_link_libraries(bdex_core PUBLIC Threads::Threads)
