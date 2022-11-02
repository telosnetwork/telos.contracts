# `telos` :heart: `reference-contracts`

## build instructions:
    # from repo root directory
    mkdir build

    # build from source
    docker run -it --rm \
        --mount type=bind,source="$(pwd)",target=/root/target \
        guilledk/py-eosio:cdt-3.0.1 \
        bash -c 'cd build && cmake .. -DBUILD_TESTS=yes && make'

    # run tests
    docker run -it --rm --mount \
        type=bind,source="$(pwd)",target=/root/target \
        guilledk/py-eosio:cdt-3.0.1 \
        bash -c 'cd build/tests && ctest -j 4'

Manually went over the links and picked the changes we need to keep our custom telos behaviors in this `telos-changes` branch.
Down below an accounting off  all changes:

### `.github/workflows/ubuntu-2004.yml`
- Changed contract development docker image, and run commands to use my version.

### `contracts/eosio.system/include/eosio.system/eosio.system.hpp`
- Include cmath header
- Add `producer_location_pair` type
- Add telos constants
- Add `payment_info`, `schedule_metrics_state`, `rotation_state`, `payrate` tables and `kick_type` enum.
- Edit global state table to add a bunch of fields, also decrease `max_ram_size`
- Edit `producer_info` table to add a bunch of fields related to rotation, kick & `unregreason` mechanics
- Add `kick` auxiliary
- Add `exrsrv,tf`, `telos.decide`, `works.decide` & `amend.decide` account names as constants
- Add `unregreason`, `votebpout`, `setpayrates` & `distviarex` action declarations
- Add auxiliary declarations for claimrewards, voting calculations and scheduling

### `contracts/eosio.system/include/eosio.system/native.hpp`
- Add `producer_metric` struct

### `contracts/eosio.system/src/delegate_bandwidth.cpp`
- Delete b1 vesting validation function
- Notify `telos.decide` on stake change on `delegatebw` and `undelegatebw`
- Switch all network activation conditions

### `contracts/eosio.system/src/eosio.system.cpp`
- Init all custom singletons
- Add `votebpout`, `setpayrates` & `distviarex` action implementations

### `contracts/eosio.system/src/producer_pay.cpp`
- Include `system_kick.cpp` which has all the producer kick machinery
- Edit `onblock` handler:
    - Change network activation condition
    - Call `check_missed_blocks` & schedule metrics machinery
    - `recalculate_votes`
    - Call `claimrewards_snapshot` once per day
- Hook our custom producer payment machinery in `claimrewards` action implementation
- Add `claimrewards_snapshot` implementation

### `contracts/eosio.system/src/rex.cpp`
- Remove voting requirements for staking

### `contracts/eosio.system/src/system_kick.cpp`
- Add schedule metrics machinery implementations

### `contracts/eosio.system/src/system_rotation.cpp`
- Add rotation modification machinery implementation

### `contracts/eosio.system/src/voting.cpp`
- Add `unregreason` action implementation
- Change logic to take in account bigger producer pool
- Sort producers by location
- Use custom schedule macinery on `update_elected_producers`
- Add custom `inverse_vote_weight` implementation used to tally votes
- Remove staking requirements to vote for a producer
- Remove whitespace
- Disable proxy voting
- Custom `propagate_weight_change` implementation

### `tests/contracts.hpp.in`
- Add `telos.decide` contract wasm
- Swap old system contracts for our versions and some are not needed any more due to tests not applying to our codebase

### `tests/eosio.powerup_tests.cpp`
- Remove

### `tests/eosio.system_tester.hpp`
- Add dump trace helper
- Create custom telos system acconts needed to avoid onblock failures on payments
- Add telos.decide deployment to setup code
- Custom onblock caller to debug errors
- Auxiliaries to get our custom singletons like payment info and payrates
- Disable proxy setup
- Add `printMetrics` helper
- Add new `active_and_vote_producers2` needed for some tests

### `tests/eosio.system_tests.cpp`
- Minor tweaks on the last decimal place of some tests results, due to slight diferences on math between reference and telos
- Drop `cross_15_percent_threshold` calls for our `activate_network` call
- Disable all proxy related tests
- Move `producer_pay`, `multi_producer_pay` & `producer_onblock_check` to custom telos tests file and disable on this file
- Change producer rotation expected results and machinery were aplicable

### `tests/telos.system_tests.cpp`
- Add custom `producer_onblock_check`, `producer_pay`, `multi_producer_pay`

### `tests/test_contracts/old_versions/telos.contracts/eosio.msig/README.txt`
- Add old msig contract with readme indicating commit and cdt used to compile

### `tests/test_contracts/old_versions/telos.contracts/eosio.msig/eosio.msig.abi`
- Add old msig abi

### `tests/test_contracts/old_versions/telos.contracts/eosio.msig/eosio.msig.wasm`
- Add old msig artifact

### `tests/test_contracts/old_versions/telos.contracts/eosio.system/README.txt`
- Add old system contract with readme indicating commit and cdt used to compile

### `tests/test_contracts/old_versions/telos.contracts/eosio.system/eosio.system.abi`
- Add old system abi

### `tests/test_contracts/old_versions/telos.contracts/eosio.system/eosio.system.wasm`
- Add old system artifact

### `tests/test_contracts/old_versions/v1.2.1/eosio.msig/Readme.txt`
- Remove old reference contract

### `tests/test_contracts/old_versions/v1.2.1/eosio.msig/eosio.msig.abi`
- Remove old reference contract

### `tests/test_contracts/old_versions/v1.2.1/eosio.msig/eosio.msig.wasm`
- Remove old reference contract

### `tests/test_contracts/old_versions/v1.2.1/eosio.system/README.txt`
- Remove old reference contract

### `tests/test_contracts/old_versions/v1.2.1/eosio.system/eosio.system.abi`
- Remove old reference contract

### `tests/test_contracts/old_versions/v1.2.1/eosio.system/eosio.system.wasm`
- Remove old reference contract

### `tests/test_contracts/old_versions/v1.8.3/eosio.system/README.txt`
- Remove old reference contract

### `tests/test_contracts/old_versions/v1.8.3/eosio.system/eosio.system.abi`
- Remove old reference contract

### `tests/test_contracts/old_versions/v1.8.3/eosio.system/eosio.system.wasm`
- Remove old reference contract

### `tests/test_contracts/telos.decide.abi`
- Add `telos.decide` abi

### `tests/test_contracts/telos.decide.wasm`
- Add `telos.decide` artifact

### `tests/test_symbol.hpp`
- Change testing suite symbol from `TST` to `TLOS`
