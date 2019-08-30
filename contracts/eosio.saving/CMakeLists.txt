add_contract(eosio.saving eosio.saving ${CMAKE_CURRENT_SOURCE_DIR}/src/worker.proposals.cpp) #NOTE: this path needs to change

target_compile_options(eosio.saving PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/resources)
target_include_directories(eosio.saving
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../eosio.trail/include)

# TODO: might need to include path to trail contract

set_target_properties(eosio.saving
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
