#include <eosio/eosio.hpp>
#include <eosio/system.hpp>
#include <eosio/asset.hpp>

using namespace std;
using namespace eosio;

class[[eosio::contract("eosio.tedp")]] tedp : public contract
{
public:
   using contract::contract;
   tedp(name receiver, name code, datastream<const char *> ds)
       : contract(receiver, code, ds), payouts(receiver, receiver.value) {}

   ACTION settf(uint64_t amount);
   ACTION setecondev(uint64_t amount);
   ACTION setcoredev(uint64_t amount);
   ACTION setrex(uint64_t amount);
   ACTION delpayout(name to);
   ACTION pay();

private:
   void setpayout(name to, uint64_t amount, uint64_t interval);
   
   TABLE payout
   {
      name to;
      uint64_t amount;
      uint64_t interval;
      uint64_t last_payout;
      uint64_t primary_key() const { return to.value; }
   };

   typedef multi_index<name("payouts"), payout> payout_table;

   using setpayout_action = action_wrapper<name("setpayout"), &tedp::setpayout>;
   using delpayout_action = action_wrapper<name("delpayout"), &tedp::delpayout>;
   using pay_action = action_wrapper<name("payout"), &tedp::pay>;
   payout_table payouts;
};