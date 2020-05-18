/**
 * Supports creation of free Telos accounts
 * 
 * @author Marlon Williams
 * @copyright defined in telos/LICENSE.txt
 */

#include <telos.free/telos.free.hpp>

using eosio::print;

freeaccounts::freeaccounts(name self, name code, datastream<const char *> ds) : contract(self, code, ds),
                                                                                configuration(self, self.value),
                                                                                freeacctslogtable(self, self.value),
                                                                                whitelistedtable(self, self.value),
                                                                                conflistedtable(self, self.value)
{
    if (!configuration.exists())
    {
        configuration.set(freeacctcfg{
                              self,          // publisher
                              int16_t(50),   // max_accounts_per_hour
                              int16_t(9000), // stake_cpu_tlos_amount
                              int16_t(1000), // stake_net_tlos_amount
                          },
                          self);
    }
    else
    {
        configuration.get();
    }
}

freeaccounts::~freeaccounts() {}

void freeaccounts::createby(name account_creator, name account_name, public_key owner_pubkey, public_key active_pubkey)
{
    createpriv(account_creator, account_name, owner_pubkey, active_pubkey, true);
}

void freeaccounts::create(name account_creator, name account_name, public_key owner_pubkey, public_key active_pubkey, string key_prefix)
{
    createpriv(account_creator, account_name, owner_pubkey, active_pubkey, false);
}

// auth_creator tells us if we should use the authority of account_creator or if we should use get_self()...
// if true, then account_creator must have given get_self()@eosio.code permission to call eosio::newaccount and
// this is required for premium/short names to use this contract to create namespaced accounts.  It also allows explorers to
// show account_creator as the creator as opposed to the historical pattern of all accounts showing as created by get_self()
void freeaccounts::createpriv(name account_creator, name account_name, public_key owner_pubkey, public_key active_pubkey, bool auth_creator)
{
    require_auth(account_creator);
    auto config = getconfig();
    uint64_t net = config.stake_net_tlos_amount;
    uint64_t cpu = config.stake_cpu_tlos_amount;
    uint32_t ram_bytes = 4096;

    auto w = whitelistedtable.find(account_creator.value);
    check(w != whitelistedtable.end(), "Account doesn't have permission to create accounts");

    // verify that account creator is within its per-account threshold
    if (w->max_accounts > 0)
    {
        uint32_t total_accounts = w->total_accounts + 1;
        check(total_accounts <= w->max_accounts, "You have exceeded the maximum number of accounts allowed for your account");

        whitelistedtable.modify(w, same_payer, [&](auto &a) {
            a.total_accounts = total_accounts;
        });
    }
    else // verify that we're within our account creation per hour threshold
    {
        uint64_t accounts_created = 0;
        for (const auto &account : freeacctslogtable)
        {
            uint64_t secs_since_created = current_time_point().sec_since_epoch() - account.created_on;
            if (secs_since_created <= 3600)
            {
                accounts_created++;
            }
        }

        check(accounts_created < config.max_accounts_per_hour, "You have exceeded the maximum number of accounts per hour");
    }

    makeacct(account_creator, account_name, auth_creator, owner_pubkey, active_pubkey, net, cpu, ram_bytes);
}

void freeaccounts::createconf(name account_creator, name account_name, bool auth_creator, public_key owner_key, public_key active_key)
{
    require_auth(account_creator);
    auto config = getconfig();

    auto c = conflistedtable.find(account_creator.value);
    check(c != conflistedtable.end(), "Account doesn't have permission to create accounts");

    // verify that account creator is within its per-account threshold
    uint32_t total_accounts = c->total_accounts + 1;
    check(total_accounts <= c->max_accounts, "You have exceeded the maximum number of accounts allowed for your account");

    conflistedtable.modify(c, same_payer, [&](auto &a) {
        a.total_accounts = total_accounts;
    });

    makeacct(account_creator, account_name, auth_creator, owner_key, active_key, c->stake_net_tlos_amount, c->stake_cpu_tlos_amount, c->ram_bytes);
}

void freeaccounts::makeacct(name account_creator, name account_name, bool auth_creator, public_key owner_pubkey, public_key active_pubkey, uint64_t net, uint64_t cpu, uint32_t ram_bytes)
{

    name newaccount_creator = auth_creator ? account_creator : get_self();

    key_weight owner_pubkey_weight = {
        .key = owner_pubkey,
        .weight = 1,
    };
    key_weight active_pubkey_weight = {
        .key = active_pubkey,
        .weight = 1,
    };
    authority owner = authority{
        .threshold = 1,
        .keys = {owner_pubkey_weight},
        .accounts = {},
        .waits = {}};
    authority active = authority{
        .threshold = 1,
        .keys = {active_pubkey_weight},
        .accounts = {},
        .waits = {}};
    newaccount new_account = newaccount{
        .creator = newaccount_creator,
        .name = account_name,
        .owner = owner,
        .active = active};

    asset stake_net(net, TLOS_symbol);
    asset stake_cpu(cpu, TLOS_symbol);

    action(
        permission_level{
            newaccount_creator,
            "active"_n,
        },
        "eosio"_n,
        "newaccount"_n,
        new_account)
        .send();

    action(
        permission_level{_self, "active"_n},
        "eosio"_n,
        "buyrambytes"_n,
        make_tuple(_self, account_name, ram_bytes))
        .send();

    if (net > 0 || cpu > 0)
    {
        action(
            permission_level{_self, "active"_n},
            "eosio"_n,
            "delegatebw"_n,
            make_tuple(_self, account_name, stake_net, stake_cpu, false))
            .send();
    }

    // record entry for audit purposes
    freeacctslogtable.emplace(_self, [&](freeacctlog &entry) {
        entry.account_name = account_name;
        entry.created_on = current_time_point().sec_since_epoch();
    });
}

void freeaccounts::setwhitelist(name account_name, uint32_t total_accounts, uint32_t max_accounts)
{
    require_auth(_self);

    auto w = whitelistedtable.find(account_name.value);

    if (w != whitelistedtable.end())
    {
        whitelistedtable.modify(w, same_payer, [&](auto &list) {
            list.max_accounts = max_accounts;
        });
    }
    else
    {
        whitelistedtable.emplace(_self, [&](whitelisted &list) {
            list.account_name = account_name;
            list.total_accounts = total_accounts;
            list.max_accounts = max_accounts;
        });
    }
}

void freeaccounts::removewlist(name account_name)
{
    require_auth(_self);

    auto w = whitelistedtable.find(account_name.value);
    check(w != whitelistedtable.end(), "Account does not exist in the whitelist");

    whitelistedtable.erase(w);
}

void freeaccounts::setconflist(name account_name, uint32_t total_accounts, uint32_t max_accounts, uint64_t stake_cpu_tlos_amount, uint64_t stake_net_tlos_amount, uint32_t ram_bytes)
{
    require_auth(_self);

    auto w = conflistedtable.find(account_name.value);

    if (w != conflistedtable.end())
    {
        conflistedtable.modify(w, same_payer, [&](auto &list) {
            list.max_accounts = max_accounts;
            list.stake_cpu_tlos_amount = stake_cpu_tlos_amount;
            list.stake_net_tlos_amount = stake_net_tlos_amount;
            list.ram_bytes = ram_bytes;
        });
    }
    else
    {
        conflistedtable.emplace(_self, [&](conflisted &list) {
            list.account_name = account_name;
            list.total_accounts = total_accounts;
            list.max_accounts = max_accounts;
            list.stake_cpu_tlos_amount = stake_cpu_tlos_amount;
            list.stake_net_tlos_amount = stake_net_tlos_amount;
            list.ram_bytes = ram_bytes;
        });
    }
}

void freeaccounts::rmvconflist(name account_name)
{
    require_auth(_self);

    auto w = conflistedtable.find(account_name.value);
    check(w != conflistedtable.end(), "Account does not exist in the conflist");

    conflistedtable.erase(w);
}

void freeaccounts::configure(int16_t max_accounts_per_hour, int64_t stake_cpu_tlos_amount, int64_t stake_net_tlos_amount)
{
    require_auth(_self);
    check(max_accounts_per_hour >= 0 && max_accounts_per_hour <= 1000, "Max accounts per hour outside of the range allowed");
    check(stake_cpu_tlos_amount >= 100 && stake_cpu_tlos_amount <= 50000, "Staked TLOS for CPU outside of the range allowed");
    check(stake_net_tlos_amount >= 100 && stake_net_tlos_amount <= 50000, "Staked TLOS for NET outside of the range allowed");

    auto config = getconfig();
    config.max_accounts_per_hour = max_accounts_per_hour;
    config.stake_cpu_tlos_amount = stake_cpu_tlos_amount;
    config.stake_net_tlos_amount = stake_net_tlos_amount;
    configuration.set(config, _self);
}

freeaccounts::freeacctcfg freeaccounts::getconfig()
{
    return configuration.get_or_create(_self, freeacctcfg{});
}