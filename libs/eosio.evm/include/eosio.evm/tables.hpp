// Copyright (c) Microsoft Corporation. All rights reserved.
// Copyright (c) 2020 Syed Jafri. All rights reserved.
// Licensed under the MIT License.

#pragma once

namespace eosio_evm {
  struct [[eosio::table, eosio::contract("eosio.evm")]] Account {
    uint64_t index;
    eosio::checksum160 address;
    eosio::name account;
    uint64_t nonce;
    std::vector<uint8_t> code;
    eosio::checksum256 balance;

    static const std::array<uint8_t, 32u> fromChecksum160(const eosio::checksum160 input)
    {
      std::array<uint8_t, 32U> output = {};
      auto input_bytes = input.extract_as_byte_array();
      std::copy(std::begin(input_bytes), std::end(input_bytes), std::begin(output) + 12);
      return output;
    }

    static eosio::checksum256 pad160(const eosio::checksum160 input)
    {
      return eosio::checksum256( fromChecksum160(input) );
    }

    Account () = default;

    uint64_t primary_key() const { return index; };

    uint64_t get_account_value() const { return account.value; };
    eosio::checksum256 by_address() const { return pad160(address); };

    EOSLIB_SERIALIZE(Account, (index)(address)(account)(nonce)(code)(balance));
  };

  struct [[eosio::table, eosio::contract("eosio.evm")]] AccountState {
    uint64_t index;
    eosio::checksum256 key;
    eosio::checksum256 value;

    uint64_t primary_key() const { return index; };
    eosio::checksum256 by_key() const { return key; };

    EOSLIB_SERIALIZE(AccountState, (index)(key)(value));
  };

  typedef eosio::multi_index<"account"_n, Account,
    eosio::indexed_by<eosio::name("byaddress"), eosio::const_mem_fun<Account, eosio::checksum256, &Account::by_address>>,
    eosio::indexed_by<eosio::name("byaccount"), eosio::const_mem_fun<Account, uint64_t, &Account::get_account_value>>
  > account_table;
  typedef eosio::multi_index<"accountstate"_n, AccountState,
    eosio::indexed_by<eosio::name("bykey"), eosio::const_mem_fun<AccountState, eosio::checksum256, &AccountState::by_key>>
  > account_state_table;
}
