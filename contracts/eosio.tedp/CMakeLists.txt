add_contract(eosio.tedp eosio.tedp ${CMAKE_CURRENT_SOURCE_DIR}/src/eosio.tedp.cpp)

target_compile_options(eosio.tedp PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/resources)
target_compile_options(eosio.tedp PUBLIC -Wunknown-pragmas)
target_include_directories(eosio.tedp
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(eosio.tedp
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
