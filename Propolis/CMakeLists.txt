add_library(Propolis ${HIVE_LIB_TYPE})

hive_configure_target(Propolis)

target_sources(Propolis
    PRIVATE
        src/propolis/graph/graph.cpp
        src/propolis/graph/graph_json.cpp
        src/propolis/types/ptype.cpp
        src/propolis/types/type_solver.cpp
        src/propolis/nodes/node_descriptor.cpp
        src/propolis/compiler/graph_validator.cpp
        src/propolis/compiler/codegen.cpp
        src/propolis/compiler/codegen_data_flow.cpp
        src/propolis/compiler/codegen_exec_flow.cpp
        src/propolis/compiler/cpp_type_mapper.cpp
        src/propolis/runtime/propolis_executor.cpp
        src/propolis/runtime/function_registry.cpp
)

target_include_directories(Propolis
    PUBLIC
        include
)

target_link_libraries(Propolis
    PUBLIC
        Hive
        Queen
        Wax
)
