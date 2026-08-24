bdex_add_module(rhi_null
    SOURCES
        src/NullRhiModule.cpp
    DEPS
        bdex::core
        bdex::platform
        bdex::rhi)
