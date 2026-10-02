find_package(Vulkan REQUIRED)

# Vulkan Memory Allocator (header-only). A system package is used when
# available; otherwise a pinned release is downloaded.
find_package(VulkanMemoryAllocator CONFIG QUIET)
if(TARGET GPUOpen::VulkanMemoryAllocator)
    add_library(bdex_vma ALIAS GPUOpen::VulkanMemoryAllocator)
else()
    include(FetchContent)
    FetchContent_Declare(VulkanMemoryAllocator
        URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.3.0.tar.gz
        # Only the header is needed: point SOURCE_SUBDIR at a directory without
        # a CMakeLists.txt so the upstream build is not pulled into ours.
        SOURCE_SUBDIR bdex-header-only)
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
    add_library(bdex_vma INTERFACE)
    target_include_directories(bdex_vma SYSTEM INTERFACE
        ${vulkanmemoryallocator_SOURCE_DIR}/include)
endif()

bdex_add_module(rhi_vulkan
    SOURCES
        src/VmaImplementation.cpp
        src/VulkanCommandList.cpp
        src/VulkanCommon.cpp
        src/VulkanDevice.cpp
        src/VulkanModule.cpp
        src/VulkanSwapchain.cpp
    DEPS
        bdex::core
        bdex::platform
        bdex::rhi
        Vulkan::Vulkan
        bdex_vma)

# Third-party implementation unit: not held to the engine's warning level.
if(MSVC)
    set_source_files_properties(src/VmaImplementation.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(src/VmaImplementation.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
