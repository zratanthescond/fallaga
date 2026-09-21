cmake_minimum_required(VERSION 3.10)
project(RDR2_Prototype)
set(CMAKE_CXX_STANDARD 17)

# Default to Release mode for high performance
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

if(MSVC)
    add_compile_options(/O2 /Oi /Ot /GL)
    add_link_options(/LTCG)
endif()

add_definitions(-DHAS_GLUT)

# Find packages
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)   # <- Switch to GLFW
find_package(glut REQUIRED)  # FreeGLUT
find_package(assimp CONFIG REQUIRED)
# Add source files
add_executable(RDR2_Prototype
    src/main.cpp
    src/Game.cpp
    src/Character.cpp
    src/Horse.cpp
    src/Terrain.cpp
    src/HeightmapTerrain.cpp
    src/Camera.cpp
    src/ObjectModel.cpp
    src/AnimatedModel.cpp
    src/Sky.cpp
    src/TerrainData.cpp
    src/TerrainManager.cpp
    src/TerrainChunk.cpp
    src/TerrainTextureLayer.cpp
    src/TextureEngine.cpp
    src/stb_impl.cpp
    src/AudioSystem.cpp
    src/Car.cpp
    src/RoadSystem.cpp
)

# Include directories
target_include_directories(RDR2_Prototype
    PRIVATE
        ${GLEW_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(RDR2_Prototype
    PRIVATE
        ${OPENGL_LIBRARIES}
        GLEW::GLEW
        glfw
         GLUT::GLUT
        assimp::assimp
)

