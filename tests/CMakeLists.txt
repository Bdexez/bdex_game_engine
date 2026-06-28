find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(WARNING "bdex: GoogleTest not found, tests are disabled")
    return()
endif()
include(GoogleTest)

add_executable(bdex_tests
    core/ConfigTests.cpp
    core/EventBusTests.cpp
    core/HandleTests.cpp
    core/SchedulerTests.cpp
    core/ServiceRegistryTests.cpp
    core/TypeIdTests.cpp)
target_include_directories(bdex_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bdex_tests PRIVATE
    bdex::core
    GTest::gtest
    GTest::gtest_main)
bdex_configure_target(bdex_tests)

gtest_discover_tests(bdex_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    DISCOVERY_TIMEOUT 30)
