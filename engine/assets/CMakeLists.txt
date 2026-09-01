bdex_add_library(assets
    SOURCES
        src/Image.cpp
        src/Mesh.cpp
        src/ObjLoader.cpp
    PUBLIC_DEPS
        bdex::core)
