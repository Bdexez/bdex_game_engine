find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(WARNING "bdex: GoogleTest not found, tests are disabled")
    return()
endif()
include(GoogleTest)

# A minimal module used to exercise the module system end to end (as a real
# plugin in shared builds, statically registered otherwise).
bdex_add_module(test_plugin
    SOURCES plugins/TestPlugin.cpp
    DEPS bdex::core)
target_include_directories(bdex_module_test_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(bdex_tests
    core/ConfigTests.cpp
    core/EngineTests.cpp
    core/EventBusTests.cpp
    core/HandleTests.cpp
    core/JobSystemTests.cpp
    core/ModuleManagerTests.cpp
    core/SchedulerTests.cpp
    core/ServiceRegistryTests.cpp
    core/TypeIdTests.cpp
    ecs/WorldTests.cpp)
target_include_directories(bdex_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bdex_tests PRIVATE
    bdex::core
    bdex::ecs
    bdex::platform
    GTest::gtest
    GTest::gtest_main)
bdex_configure_target(bdex_tests)
bdex_target_modules(bdex_tests test_plugin)

gtest_discover_tests(bdex_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    DISCOVERY_TIMEOUT 30)
