# ============================================================================
# Waggle - Engine Application Layer for HiveEngine
# ============================================================================

add_library(Waggle ${HIVE_LIB_TYPE})

hive_configure_target(Waggle)

target_sources(Waggle
    PRIVATE
        src/waggle/app.cpp
        src/waggle/gizmo_state.cpp
        src/waggle/play_state.cpp
        src/waggle/registermodule.cpp
        src/waggle/systems/transform_system.cpp
        src/waggle/systems/propolis_system.cpp
        src/waggle/systems/mesh_render_system.cpp
        src/waggle/systems/editor_camera_system.cpp
        src/waggle/systems/debug_freecam_system.cpp
        src/waggle/systems/gizmo_render_system.cpp
        src/waggle/math/ray_intersect.cpp
        src/waggle/render/render_module.cpp
        src/waggle/engine_runner.cpp
        src/waggle/profiling/frame_bench.cpp
        src/waggle/project/project_manager.cpp
        src/waggle/project/gameplay_module.cpp
        src/waggle/project/cmake_generator.cpp
        src/waggle/project/project_scaffolder.cpp
        src/waggle/scene/gltf_scene_loader.cpp
        src/waggle/scene/scene_io.cpp
        src/waggle/disabled_propagation.cpp
)

target_include_directories(Waggle
    PUBLIC
        include
    SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../Nectar/extern/cgltf
)

target_precompile_headers(Waggle
    PRIVATE
        include/waggle/precomp.h
)

target_link_libraries(Waggle
    PUBLIC
        Queen
        Hive
        Nectar
        Propolis
)

target_link_libraries(Waggle PUBLIC Terra Antennae Swarm)

if(TARGET Forge)
    target_link_libraries(Waggle PRIVATE Forge)
endif()
