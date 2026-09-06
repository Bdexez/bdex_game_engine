bdex_add_library(assets
    SOURCES
        src/AssetManager.cpp
        src/Image.cpp
        src/Mesh.cpp
        src/ObjLoader.cpp
    PUBLIC_DEPS
        bdex::core)
