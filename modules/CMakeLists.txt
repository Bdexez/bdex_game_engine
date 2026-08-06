# Modules are implementations of engine interfaces. They only link against
# engine libraries - never against each other - and are selected at runtime
# through the `engine.modules` configuration key.

if(BDEX_MODULE_WINDOW_NULL)
    add_subdirectory(window_null)
endif()
