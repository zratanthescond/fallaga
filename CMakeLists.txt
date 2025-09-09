cmake_minimum_required(VERSION 3.10)
project(RDR2_Prototype)

set(CMAKE_CXX_STANDARD 17)

# Find packages
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)   # <- Switch to GLFW
find_package(glut REQUIRED)  # FreeGLUT
# Add source files
add_executable(RDR2_Prototype
    src/main.cpp
    src/Game.cpp
    src/Character.cpp
    src/Horse.cpp
    src/Terrain.cpp
    src/Camera.cpp
    src/ObjectModel.cpp
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
)
