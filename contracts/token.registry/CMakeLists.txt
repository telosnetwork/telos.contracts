add_contract(token.registry token.registry ${CMAKE_CURRENT_SOURCE_DIR}/src/token.registry.cpp)

target_compile_options(token.registry PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/resources)

target_include_directories(token.registry
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/../token.registry/include
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(token.registry
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
