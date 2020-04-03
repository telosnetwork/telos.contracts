/**
 * Supports creation of free Telos accounts
 * 
 * @author Marlon Williams
 * @copyright defined in telos/LICENSE.txt
 */

#pragma once

#include <eosio/crypto.hpp>
#include <eosio/action.hpp>
#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/permission.hpp>
#include <eosio/singleton.hpp>
#include <eosio/transaction.hpp>
#include "exchange_state.hpp"

#include <string>

using namespace std;
using namespace eosio;

class[[eosio::contract("telos.free")]] freeaccounts : public contract
{
public:
      using contract::contract;
      struct permission_level_weight
      {
            permission_level permission;
            uint16_t weight;
      };
      struct key_weight
      {
            public_key key;
            uint16_t weight;
      };
      struct wait_weight
      {
            uint32_t wait_sec;
            uint16_t weight;
      };
      struct authority
      {
            uint32_t threshold;
            vector<key_weight> keys;
            vector<permission_level_weight> accounts;
            vector<wait_weight> waits;
      };
      struct newaccount
      {
            name creator;
            name name;
            authority owner;
            authority active;
      };

      [[eosio::action]] void create(name account_creator, name account_name, public_key owner_key, public_key active_key, string key_prefix);

      [[eosio::action]] void createby(name account_creator, name account_name, public_key owner_key, public_key active_key);

      [[eosio::action]] void configure(int16_t max_accounts_per_hour, int64_t stake_cpu_tlos_amount, int64_t stake_net_tlos_amount);

      [[eosio::action]] void addwhitelist(name account_name, uint32_t total_accounts, uint32_t max_accounts);

      [[eosio::action]] void removewlist(name account_name);

      [[eosio::action]] void erasewlist(name account);

      struct [[eosio::table("config")]] freeacctcfg
      {
            name publisher;
            int16_t max_accounts_per_hour = 50;
            int64_t stake_cpu_tlos_amount = 9000;
            int64_t stake_net_tlos_amount = 1000;

            auto primary_key() const { return publisher.value; }

            EOSLIB_SERIALIZE(freeacctcfg, (publisher)(max_accounts_per_hour)(stake_cpu_tlos_amount)(stake_net_tlos_amount))
      };

      struct [[eosio::table]] freeacctlog
      {
            name account_name;
            uint32_t created_on = 0;

            auto primary_key() const { return account_name.value; }
            uint32_t by_created_on() const { return created_on; }

            EOSLIB_SERIALIZE(freeacctlog, (account_name)(created_on))
      };

      struct [[eosio::table]] whitelist
      {
            name account_name;

            auto primary_key() const { return account_name.value; }

            EOSLIB_SERIALIZE(whitelist, (account_name))
      };

      struct [[eosio::table]] whitelisted
      {
            name account_name;
            uint32_t total_accounts = 0;
            uint32_t max_accounts = 0;

            auto primary_key() const { return account_name.value; }

            EOSLIB_SERIALIZE(whitelisted, (account_name)(total_accounts)(max_accounts))
      };

      typedef multi_index<"freeacctlogs"_n, freeacctlog> t_freeaccountlogs;

      typedef multi_index<"whitelists"_n, whitelist> t_whitelist;

      typedef multi_index<"whitelstacts"_n, whitelisted> t_whitelisted;

      freeaccounts(name self, name code, datastream<const char *> ds);

      ~freeaccounts();

protected:
      typedef singleton<"config"_n, freeacctcfg> config_singleton;
      config_singleton configuration;
      t_freeaccountlogs freeacctslogtable;
      t_whitelisted whitelistedtable;
      t_whitelist whitelisttable;

      rammarket rammarkettable;
      static constexpr eosio::name system_account{"eosio"_n};
      static constexpr symbol RAMCORE_symbol = symbol(symbol_code("RAMCORE"), 4);
      static constexpr symbol RAM_symbol = symbol(symbol_code("RAM"), 0);
      static constexpr symbol TLOS_symbol = symbol(symbol_code("TLOS"), 4);

      freeacctcfg getconfig();

      void createpriv(name account_creator, name account_name, public_key owner_pubkey, public_key active_pubkey, bool auth_creator);
};