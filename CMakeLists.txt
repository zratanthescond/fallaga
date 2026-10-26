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
find_package(Bullet CONFIG REQUIRED)
# Add source files
add_executable(RDR2_Prototype
    src/main.cpp
    src/Game.cpp
    src/Character.cpp
    src/Horse.cpp
    src/Terrain.cpp
    src/EnvironmentMap.cpp
    src/vegetation/BiomeMasks.cpp
    src/vegetation/VegetationMasks.cpp
    src/RiverSystem.cpp
    src/HeightmapTerrain.cpp
    src/Camera.cpp
    src/ObjectModel.cpp
    src/AnimatedModel.cpp
    src/Sky.cpp
    src/TerrainData.cpp
    src/TerrainManager.cpp
    src/TerrainChunk.cpp
    src/SculptTools.cpp
    src/TerrainTextureLayer.cpp
    src/TextureEngine.cpp
    src/stb_impl.cpp
    src/AudioSystem.cpp
    src/Car.cpp
    src/CarControls.cpp
    src/CarPhysics.cpp
    src/vehicle/VehicleDynamics.cpp
    src/vehicle/VehicleBody.cpp
    src/vehicle/VehicleConfig.cpp
    src/vehicle/VehicleSuspension.cpp
    src/vehicle/VehicleBrakes.cpp
    src/vehicle/VehicleDrivetrain.cpp
    src/vehicle/VehicleStability.cpp
    src/vehicle/VehicleSurface.cpp
    src/RoadSystem.cpp
    src/SceneTarget.cpp
    src/SSAO.cpp
    src/terrain/TerrainSystem.cpp
    src/terrain/Heightfield.cpp
    )

# Include directories
target_include_directories(RDR2_Prototype
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
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
        BulletDynamics
        BulletCollision
        LinearMath
)

include(CTest)
if(BUILD_TESTING)
    add_executable(car_handling_test car_handling_test.cpp)
    target_include_directories(car_handling_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME car_handling_test COMMAND car_handling_test)

    add_executable(vehicle_dynamics_test
        vehicle_dynamics_test.cpp
        src/vehicle/VehicleDynamics.cpp
    )
    target_include_directories(vehicle_dynamics_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GLEW_INCLUDE_DIRS})
    target_link_libraries(vehicle_dynamics_test PRIVATE assimp::assimp)
    add_test(NAME vehicle_dynamics_test COMMAND vehicle_dynamics_test)

    add_executable(vehicle_body_test
        vehicle_body_test.cpp
        src/vehicle/VehicleBody.cpp
    )
    target_include_directories(vehicle_body_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GLEW_INCLUDE_DIRS})
    target_link_libraries(vehicle_body_test PRIVATE BulletDynamics BulletCollision LinearMath)
    add_test(NAME vehicle_body_test COMMAND vehicle_body_test)

    add_executable(vehicle_config_test
        vehicle_config_test.cpp
        src/vehicle/VehicleConfig.cpp
    )
    target_include_directories(vehicle_config_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME vehicle_config_test COMMAND vehicle_config_test)

    add_executable(vehicle_suspension_test
        vehicle_suspension_test.cpp
        src/vehicle/VehicleSuspension.cpp
    )
    target_include_directories(vehicle_suspension_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME vehicle_suspension_test COMMAND vehicle_suspension_test)

    add_executable(vehicle_assists_test
        vehicle_assists_test.cpp
        src/vehicle/VehicleBrakes.cpp
        src/vehicle/VehicleDrivetrain.cpp
        src/vehicle/VehicleStability.cpp
        src/vehicle/VehicleSurface.cpp
    )
    target_include_directories(vehicle_assists_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(vehicle_assists_test PRIVATE assimp::assimp)
    add_test(NAME vehicle_assists_test COMMAND vehicle_assists_test)

    add_executable(road_bed_random_test road_bed_random_test.cpp)
    target_include_directories(road_bed_random_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GLEW_INCLUDE_DIRS})
    target_link_libraries(road_bed_random_test PRIVATE ${OPENGL_LIBRARIES} GLEW::GLEW glfw GLUT::GLUT assimp::assimp)
    add_test(NAME road_bed_random_test COMMAND road_bed_random_test)

    # Heights only, no GL context: covers the sculpt "Add Geometry" brush maths.
    # stb_image's implementation is compiled into the test itself, so stb_impl.cpp
    # is deliberately not linked here.
    add_executable(sculpt_refine_test sculpt_refine_test.cpp)
    target_include_directories(sculpt_refine_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GLEW_INCLUDE_DIRS})
    target_link_libraries(sculpt_refine_test PRIVATE ${OPENGL_LIBRARIES} GLEW::GLEW glfw GLUT::GLUT assimp::assimp)
    add_test(NAME sculpt_refine_test COMMAND sculpt_refine_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Heights only, no GL context: covers the flat-surface brushes (Flatten/Level)
    # in SculptTools.cpp, which is compiled directly into the test.
    add_executable(sculpt_flat_test sculpt_flat_test.cpp)
    target_include_directories(sculpt_flat_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GLEW_INCLUDE_DIRS})
    target_link_libraries(sculpt_flat_test PRIVATE ${OPENGL_LIBRARIES} GLEW::GLEW glfw GLUT::GLUT assimp::assimp)
    add_test(NAME sculpt_flat_test COMMAND sculpt_flat_test)

    # The road must sit on the rendered terrain. Includes RoadSystem.cpp directly, so
    # no GL context and no Terrain instance is needed.
    add_executable(road_surface_test road_surface_test.cpp)
    target_include_directories(road_surface_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GLEW_INCLUDE_DIRS})
    target_link_libraries(road_surface_test PRIVATE ${OPENGL_LIBRARIES} GLEW::GLEW glfw GLUT::GLUT assimp::assimp)
    add_test(NAME road_surface_test COMMAND road_surface_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Junction invariant: the arms at a junction must arrive at the same height.
    add_executable(road_junction_test road_junction_test.cpp)
    target_include_directories(road_junction_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GLEW_INCLUDE_DIRS})
    target_link_libraries(road_junction_test PRIVATE ${OPENGL_LIBRARIES} GLEW::GLEW glfw GLUT::GLUT assimp::assimp)
    add_test(NAME road_junction_test COMMAND road_junction_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # The carriageway surface texture must follow the road direction, not the world
    # axes, or it shears across every bend. Includes RoadSystem.cpp directly, so no
    # GL context is needed.
    add_executable(road_uv_test road_uv_test.cpp)
    target_include_directories(road_uv_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GLEW_INCLUDE_DIRS})
    target_link_libraries(road_uv_test PRIVATE ${OPENGL_LIBRARIES} GLEW::GLEW glfw GLUT::GLUT assimp::assimp)
    add_test(NAME road_uv_test COMMAND road_uv_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Heights only, no GL context: covers the sculpt undo/redo journal primitives
    # (TerrainUndo.h) and the TerrainRefinement hooks they need.
    add_executable(terrain_undo_test terrain_undo_test.cpp)
    target_include_directories(terrain_undo_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME terrain_undo_test COMMAND terrain_undo_test)

    add_executable(vegetation_masks_test
        vegetation_masks_test.cpp
        src/vegetation/VegetationMasks.cpp
    )
    target_include_directories(vegetation_masks_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(vegetation_masks_test PRIVATE assimp::assimp)
    add_test(NAME vegetation_masks_test COMMAND vegetation_masks_test)
endif()
