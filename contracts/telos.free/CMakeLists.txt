add_contract(telos.free telos.free ${CMAKE_CURRENT_SOURCE_DIR}/src/telos.free.cpp)

target_compile_options(telos.free PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/resources)
target_compile_options(telos.free PUBLIC -Wunknown-pragmas)
target_include_directories(telos.free
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(telos.free
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
