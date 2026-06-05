cmake_minimum_required(VERSION 3.25)

project(BdexEngine
    VERSION 0.1.0
    DESCRIPTION "Modular Vulkan game engine"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BDEX_SHARED_MODULES "Build engine libraries as shared objects and modules as runtime-loaded plugins" ON)
option(BDEX_BUILD_TESTS "Build the unit and integration tests" ON)
option(BDEX_BUILD_SAMPLES "Build the sample applications" ON)
option(BDEX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Every module can be switched off individually: nothing in the engine links
# against a module, so removing one never breaks the build of the others.
option(BDEX_MODULE_WINDOW_GLFW "Build the GLFW window module" ON)
option(BDEX_MODULE_WINDOW_NULL "Build the headless window module" ON)
option(BDEX_MODULE_RHI_VULKAN "Build the Vulkan RHI module" ON)
option(BDEX_MODULE_RHI_NULL "Build the no-op RHI module" ON)
option(BDEX_MODULE_RENDERER_FORWARD "Build the forward renderer module" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

# Executables, shared libraries and plugins all land in the same directory so
# the module loader can find plugins next to the executable.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(BDEX_ASSET_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/assets)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(BdexEngine)

find_package(glm REQUIRED)

add_subdirectory(engine)
add_subdirectory(modules)

if(BDEX_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(BDEX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
