// TELOS BEGIN
#include <eosio/eosio.hpp>
#include <eosio/system.hpp>
#include <eosio/asset.hpp>

#include <eosio.token/eosio.token.hpp>

#include <eosio.tedp/tedp.constants.hpp>


using namespace std;
using namespace eosio;

class [[eosio::contract("eosio.tedp")]] tedp : public contract
{
public:
    using contract::contract;
    tedp(name receiver, name code, datastream<const char *> ds) : contract(receiver, code, ds), payouts(receiver, receiver.value),  configuration(receiver, receiver.value) {}

    [[eosio::action]]
    void setratio(uint64_t ratio_value);

    [[eosio::action]]
    void setevmconfig(string stlos_contract, eosio::checksum256 storage_key, uint64_t wtlos_index);

    [[eosio::action]]
    void settf(uint64_t amount);

    [[eosio::action]]
    void setecondev(uint64_t amount);

    [[eosio::action]]
    void setcoredev(uint64_t amount);

    [[eosio::action]]
    void setrex(uint64_t amount);

    [[eosio::action]]
    void setignite(uint64_t amount);

    [[eosio::action]]
    void setfuel(uint64_t amount);

    [[eosio::action]]
    void delpayout(name to);

    [[eosio::action]]
    void pay();

    TABLE payout {
        name to;
        uint64_t amount;
        uint64_t interval;
        uint64_t last_payout;
        uint64_t primary_key() const { return to.value; }
    };

    typedef multi_index<name("payouts"), payout> payout_table;

private:
    static constexpr name CORE_SYM_ACCOUNT = name("eosio.token");
    static constexpr symbol CORE_SYM = symbol("TLOS", 4);
    static constexpr name EVM_CONTRACT = name("eosio.evm");
    static constexpr name REX_CONTRACT = name("eosio.rex");
    static constexpr name IGNITE_CONTRACT = name("ignitegrants");
    static constexpr name FUEL_CONTRACT = name("telosfuelfund");
    static constexpr name SYSTEM_ACCOUNT = name("eosio");
    void setpayout(name to, uint64_t amount, uint64_t interval);
    double getbalanceratio();

    TABLE config {
        uint64_t ratio;
        uint64_t wtlos_index;
        eosio::checksum256 storage_key;
        string stlos_contract;

        EOSLIB_SERIALIZE(config, (ratio)(wtlos_index)(storage_key)(stlos_contract))
    } config_row;

   struct [[eosio::table,eosio::contract("eosio.system")]] rex_pool {
      uint8_t    version = 0;
      asset      total_lent;
      asset      total_unlent;
      asset      total_rent;
      asset      total_lendable;
      asset      total_rex;
      asset      namebid_proceeds;
      uint64_t   loan_num = 0;

      uint64_t primary_key()const { return 0; }
   };

    typedef eosio::multi_index< "rexpool"_n, rex_pool > rex_pool_table;

    typedef eosio::singleton<"config"_n, config> config_table;

    using setpayout_action = action_wrapper<name("setpayout"), &tedp::setpayout>;
    using delpayout_action = action_wrapper<name("delpayout"), &tedp::delpayout>;
    using pay_action = action_wrapper<name("payout"), &tedp::pay>;
    using setevmconfig_action = action_wrapper<"setevmconfig"_n, &tedp::setevmconfig>;
    using setratio_action = action_wrapper<"setratio"_n, &tedp::setratio>;

    payout_table payouts;
    config_table configuration;
};

// TELOS END