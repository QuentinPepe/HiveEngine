# PropolisCompile.cmake
# Provides propolis_compile() to generate C++ from .propolis blueprint files at build time.
#
# Usage:
#   propolis_compile(
#       TARGET    my_gameplay_dll
#       SCRIPTS   ${CMAKE_CURRENT_SOURCE_DIR}/scripts/PlayerController.propolis
#                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/EnemyAI.propolis
#       NAMESPACE game
#       OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
#   )
#
# This adds custom commands that run propolis_compiler on each .propolis file
# and adds the generated .cpp files to the target's sources.

function(propolis_compile)
    cmake_parse_arguments(PC "" "TARGET;NAMESPACE;OUTPUT_DIR" "SCRIPTS" ${ARGN})

    if(NOT PC_TARGET)
        message(FATAL_ERROR "propolis_compile: TARGET is required")
    endif()

    if(NOT PC_OUTPUT_DIR)
        set(PC_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/propolis_generated")
    endif()

    if(NOT PC_NAMESPACE)
        set(PC_NAMESPACE "propolis::generated")
    endif()

    file(MAKE_DIRECTORY ${PC_OUTPUT_DIR})

    set(_generated_sources)

    foreach(_script IN LISTS PC_SCRIPTS)
        get_filename_component(_stem ${_script} NAME_WE)
        set(_output "${PC_OUTPUT_DIR}/${_stem}.propolis.cpp")

        add_custom_command(
            OUTPUT ${_output}
            COMMAND propolis_compiler
                "${_script}" "${_output}"
                --name "${_stem}"
                --namespace "${PC_NAMESPACE}"
            DEPENDS ${_script} propolis_compiler
            COMMENT "Propolis: ${_stem}.propolis -> ${_stem}.propolis.cpp"
            VERBATIM
        )

        list(APPEND _generated_sources ${_output})
    endforeach()

    target_sources(${PC_TARGET} PRIVATE ${_generated_sources})
endfunction()
