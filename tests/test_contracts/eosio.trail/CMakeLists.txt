cmake_minimum_required( VERSION 3.5 )

set(EOSIO_WASM_OLD_BEHAVIOR "Off")
find_package(eosio.cdt)

project(eosio_trail)

add_contract(eosio.trail eosio.trail ${CMAKE_CURRENT_SOURCE_DIR}/src/eosio.trail.cpp) #NOTE: this path needs to change

target_compile_options(eosio.trail PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/resources)
target_compile_options(eosio.trail PUBLIC -Wunknown-pragmas)
target_include_directories(eosio.trail
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(eosio.trail
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
