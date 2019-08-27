#include <boost/test/unit_test.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/wast_to_wasm.hpp>

// #include <cstdlib>
#include <iostream>
#include <sstream>
#include <fc/log/logger.hpp>
#include <eosio/chain/exceptions.hpp>
#include <Runtime/Runtime.h>

#include "eosio.system_tester.hpp"
struct _abi_hash {
   name owner;
   fc::sha256 hash;
};
FC_REFLECT( _abi_hash, (owner)(hash) );

struct connector {
   asset balance;
   double weight = .5;
};
FC_REFLECT( connector, (balance)(weight) );

using namespace eosio_system;
using namespace std;

auto dump_trace = [](transaction_trace_ptr trace_ptr) -> transaction_trace_ptr {
   cout << endl << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<" << endl;
   for(auto trace : trace_ptr->action_traces) {
      cout << "action_name trace: " << trace.act.name.to_string() << endl;
      //TODO: split by new line character, loop and indent output
      cout << trace.console << endl << endl;
   }
   cout << endl << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>" << endl << endl;
   return trace_ptr;
};

bool within_one(int64_t a, int64_t b) { return std::abs(a - b) <= 1; }

BOOST_AUTO_TEST_SUITE(eosio_system_tests)

BOOST_FIXTURE_TEST_CASE( bp_rotations, eosio_system_tester ) try {
   const asset net = core_sym::from_string("80.0000");
   const asset cpu = core_sym::from_string("80.0000");
   const std::vector<account_name> voters = { N(producvotera), N(producvoterb), N(producvoterc), N(producvoterd) };
   for (const auto& v: voters) {
      create_account_with_resources( v, config::system_account_name, core_sym::from_string("1.0000"), false, net, cpu );
      transfer( config::system_account_name, v, core_sym::from_string("100000000.0000"), config::system_account_name );
      BOOST_REQUIRE_EQUAL(success(), stake(v, core_sym::from_string("30000000.0000"), core_sym::from_string("30000000.0000")) );
   }

   std::vector<account_name> producer_names;
   {
      producer_names.reserve('z' - 'a' + 1);
      {
         const std::string root("produceridx");
         for ( char c = 'a'; c <= 'z'; ++c ) {
            producer_names.emplace_back(root + std::string(1, c));
         }
      }
      {
         const std::string root("zidproducer");
         for ( char c = 'a'; c <= 'z'; ++c ) {
            producer_names.emplace_back(root + std::string(1, c));
         }
      }
      setup_producer_accounts(producer_names);
      for (const auto& p: producer_names) {
         BOOST_REQUIRE_EQUAL( success(), regproducer(p) );
         produce_blocks(1);
         BOOST_TEST(0 == get_producer_info(p)["total_votes"].as<double>());
      }
   }

   activate_network();

   // base votes to make sure anyone can be switched around
   {
      // top 21 !!! << voting 21 will not trigger rotation , but will increase rotation timer >> !!!
      BOOST_REQUIRE_EQUAL(success(), vote(N(producvotera), vector<account_name>(producer_names.begin(), producer_names.begin()+21)));
      produce_blocks(2);

      // high stand-bys
      BOOST_REQUIRE_EQUAL(success(), vote(N(producvoterb), vector<account_name>(producer_names.begin()+21, producer_names.begin()+41)));
      // low stand-bys
      BOOST_REQUIRE_EQUAL(success(), vote(N(producvoterc), vector<account_name>(producer_names.begin()+41, producer_names.end())));
   }
   produce_blocks(2);

   //apply next rotation
   auto doRotation = [&](int offset = 6) {
      produce_block(fc::minutes(12*60 - offset));  // propose schedule with rotation
      produce_blocks(360);                         // give time for schedule to become pending (3 min)
      produce_blocks(360);                         // give time for schedule to become active (3 min)
   };

   //check active schedule to match expected active bps and rotation
   auto checkSchedule = [&](const vector<account_name>active, const vector<account_name>standby, const vector<producer_key>producers, const account_name& bp_out, const account_name& sbp_in) -> bool{
      bool isExpectedSchedule = true;
      for(int i = 0; i < producers.size() && isExpectedSchedule; i++){
         auto p = std::find(active.begin(), active.end(), producers[i].producer_name);

         if(p == active.end()){
            p = std::find(standby.begin(), standby.end(), producers[i].producer_name);

            // the producer from the schedule not in top 21[active] needs to be in 21-51[standby] and to be the rotated standby bp
            isExpectedSchedule = isExpectedSchedule && p != standby.end() && *p == sbp_in;
            continue;
         }

         // the producer from the schedule needs to be in top 21[active] and different from rotated bp
         isExpectedSchedule = isExpectedSchedule && *p != bp_out;
      }
      return isExpectedSchedule;
   };

   std::vector<account_name> active, standby;
   std::vector<producer_key> producers;
   int bp_rotated_out, sbp_rotated_in;
   account_name bp_out, sbp_in;

   active.reserve(21);
   standby.reserve(30);
   active.insert(active.end(), producer_names.begin(), producer_names.begin()+21);
   standby.insert(standby.end(), producer_names.begin()+21, producer_names.begin()+51);

   // produceridxa should be rotated with produceridxv

   std::cout<<"before rotation"<<std::endl;
   doRotation(0);
   std::cout<<"after rotation"<<std::endl;
   bp_rotated_out = 0;
   sbp_rotated_in = 21;
   bp_out = active[bp_rotated_out];
   sbp_in = standby[sbp_rotated_in - 21];
   active[bp_rotated_out] = sbp_in;
   standby[sbp_rotated_in - 21] = bp_out;

   producers = control->head_block_state()->active_schedule.producers;
   bool isExpectedSchedule = checkSchedule(active, standby, producers, bp_out, sbp_in);
   BOOST_REQUIRE(isExpectedSchedule);

   auto rotation = get_rotation_state();
   BOOST_REQUIRE(rotation["bp_currently_out"].as_string() == bp_out);
   BOOST_REQUIRE(rotation["sbp_currently_in"].as_string() == sbp_in);
   BOOST_REQUIRE(rotation["bp_out_index"].as_uint64() == bp_rotated_out);
   BOOST_REQUIRE(rotation["sbp_in_index"].as_uint64() == sbp_rotated_in);

   // produceridxb should be rotated with produceridxw
   std::cout<<"before rotation"<<std::endl;
   doRotation();
   std::cout<<"after rotation"<<std::endl;
   // undo last rotation
   active[bp_rotated_out] = bp_out;
   standby[sbp_rotated_in - 21] = sbp_in;

   // do next rotation
   bp_rotated_out = 1;
   sbp_rotated_in = 22;
   bp_out = active[ bp_rotated_out ];
   sbp_in = standby[ sbp_rotated_in - 21 ];
   active[bp_rotated_out] = sbp_in;
   standby[sbp_rotated_in - 21] = bp_out;

   producers = control->head_block_state()->active_schedule.producers;
   isExpectedSchedule = checkSchedule(active, standby, producers, bp_out, sbp_in);
   BOOST_REQUIRE(isExpectedSchedule);

   rotation = get_rotation_state();
   BOOST_REQUIRE(rotation["bp_currently_out"].as_string() == bp_out);
   BOOST_REQUIRE(rotation["sbp_currently_in"].as_string() == sbp_in);
   BOOST_REQUIRE(rotation["bp_out_index"].as_uint64() == bp_rotated_out);
   BOOST_REQUIRE(rotation["sbp_in_index"].as_uint64() == sbp_rotated_in);

   std::vector<account_name> tmpv;
   {
      // zidproducern + zidproducero will be voted in top 21
      // move the last 2 high-standby to the top, changing the order of sbp-in and bp-out , but still keeping them in their respective lists
      tmpv = vector<account_name>(producer_names.begin()+39, producer_names.end());

      // keep low standbys + last 2 high standby
      BOOST_REQUIRE_EQUAL(success(), vote(N(producvoterc), tmpv));

      // produceridxt + produceridxu will fall from top 21 to standby
      active.erase(active.begin()+19, active.begin()+21);
      standby.insert(standby.begin(), producer_names.begin()+19, producer_names.begin()+21);

      // zidproducern + zidproducero will be voted in top 21
      active.insert(active.begin(), producer_names.begin()+39, producer_names.begin()+41);
      standby.erase(standby.begin()+18, standby.begin()+20);
   }

   produce_blocks(360); // give time for schedule to become pending (3 min)
   produce_blocks(360); // give time for schedule to become active (3 min)

   producers = control->head_block_state()->active_schedule.producers;
   isExpectedSchedule = checkSchedule(active, standby, producers, bp_out, sbp_in);
   BOOST_REQUIRE(isExpectedSchedule);

   rotation = get_rotation_state();
   // local rotation info is correct and names stick after voting
   BOOST_REQUIRE(rotation["bp_currently_out"].as_string() == bp_out);
   BOOST_REQUIRE(rotation["sbp_currently_in"].as_string() == sbp_in);
   BOOST_REQUIRE(rotation["bp_out_index"].as_uint64() == bp_rotated_out);
   BOOST_REQUIRE(rotation["sbp_in_index"].as_uint64() == sbp_rotated_in);

   // indexes are off by 2 after voting
   BOOST_REQUIRE(active[bp_rotated_out + 2] == sbp_in);
   BOOST_REQUIRE(standby[sbp_rotated_in + 2 - 21] == bp_out);

   {
      // last state
      tmpv = vector<account_name>(producer_names.begin()+39, producer_names.end());
      // produceridxw will be added to the vote
      tmpv.insert(tmpv.begin(), sbp_in);

      // move produceridxw to top 21
      BOOST_REQUIRE_EQUAL(success(), vote(N(producvoterc), tmpv));

      // undo last rotation
      active[bp_rotated_out + 2] = bp_out;
      standby[sbp_rotated_in + 2 - 21] = sbp_in;

      // produceridxs will fall from top 21 to standby
      active.pop_back();
      standby.insert(standby.begin(), producer_names.begin()+18, producer_names.begin()+19);

      // produceridxw will be voted in top 21
      active.insert(active.begin(), sbp_in);
      standby.erase(standby.begin() + (sbp_rotated_in + 2 - 21) );

      bp_out = "";
      sbp_in = "";
   }

   produce_blocks(360); // give time for schedule to become pending (3 min)
   produce_blocks(360); // give time for schedule to become active (3 min)

   // after reset caused by sbp going out-of-list (promoted to top21 or under 51)
   // the rotaion indexes should stick [1, 22] but names should be ""
   rotation = get_rotation_state();
   BOOST_REQUIRE(rotation["bp_currently_out"].as_string() == bp_out);
   BOOST_REQUIRE(rotation["sbp_currently_in"].as_string() == sbp_in);
   BOOST_REQUIRE(rotation["bp_out_index"].as_uint64() == bp_rotated_out);
   BOOST_REQUIRE(rotation["sbp_in_index"].as_uint64() == sbp_rotated_in);

   producers = control->head_block_state()->active_schedule.producers;
   isExpectedSchedule = checkSchedule(active, standby, producers, bp_out, sbp_in);
   BOOST_REQUIRE(isExpectedSchedule);

   doRotation(6);

   // do new rotation as per usual
   // zidproducero should be rotated with produceridxu
   bp_rotated_out = 2;
   sbp_rotated_in = 23;
   bp_out = active[ bp_rotated_out ];
   sbp_in = standby[ sbp_rotated_in - 21 ];
   active[bp_rotated_out] = sbp_in;
   standby[sbp_rotated_in - 21] = bp_out;

   rotation = get_rotation_state();
   BOOST_REQUIRE(rotation["bp_currently_out"].as_string() == bp_out);
   BOOST_REQUIRE(rotation["sbp_currently_in"].as_string() == sbp_in);
   BOOST_REQUIRE(rotation["bp_out_index"].as_uint64() == bp_rotated_out);
   BOOST_REQUIRE(rotation["sbp_in_index"].as_uint64() == sbp_rotated_in);
   producers = control->head_block_state()->active_schedule.producers;
   isExpectedSchedule = checkSchedule(active, standby, producers, bp_out, sbp_in);
   BOOST_REQUIRE(isExpectedSchedule);

   //unregister producer zidproducero <rotated bp>
   BOOST_REQUIRE_EQUAL( success(), push_action(N(zidproducero), N(unregprod), mvo()
                                               ("producer",  "zidproducero")
                        )
   );

   produce_blocks(360); // give time for schedule to become pending (3 min)
   produce_blocks(360); // give time for schedule to become active (3 min)

   // undo last rotation
   active[bp_rotated_out] = bp_out;
   standby[sbp_rotated_in - 21] = sbp_in;

   // produceridxs will rise to top 21 from standby
   active.emplace_back(producer_names[18]);
   standby.erase(standby.begin());

   // zidproducerp will rise from candidate to standby
   standby.emplace_back();

   // rotations resets
   bp_out = "";
   sbp_in = "";

   rotation = get_rotation_state();
   BOOST_REQUIRE(rotation["bp_currently_out"].as_string() == bp_out);
   BOOST_REQUIRE(rotation["sbp_currently_in"].as_string() == sbp_in);
   BOOST_REQUIRE(rotation["bp_out_index"].as_uint64() == bp_rotated_out);
   BOOST_REQUIRE(rotation["sbp_in_index"].as_uint64() == sbp_rotated_in);
   producers = control->head_block_state()->active_schedule.producers;
   isExpectedSchedule = checkSchedule(active, standby, producers, bp_out, sbp_in);
   BOOST_REQUIRE(isExpectedSchedule);

   // after reset, it will go back to normal rotation case
   // this was tested above the unregister case
   // standby promotion reset the rotation and went back to normal
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( buysell, eosio_system_tester ) try {

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );

   transfer( "eosio", "alice1111111", core_sym::from_string("1000.0000"), "eosio" );
   BOOST_REQUIRE_EQUAL( success(), stake( "eosio", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake( "alice1111111" );
   auto init_bytes =  total["ram_bytes"].as_uint64();

   const asset initial_ram_balance = get_balance(N(eosio.ram));
   const asset initial_ramfee_balance = get_balance(N(eosio.ramfee));
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("200.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("800.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( initial_ram_balance + core_sym::from_string("199.0000"), get_balance(N(eosio.ram)) );
   BOOST_REQUIRE_EQUAL( initial_ramfee_balance + core_sym::from_string("1.0000"), get_balance(N(eosio.ramfee)) );

   total = get_total_stake( "alice1111111" );
   auto bytes = total["ram_bytes"].as_uint64();
   auto bought_bytes = bytes - init_bytes;
   wdump((init_bytes)(bought_bytes)(bytes) );

   BOOST_REQUIRE_EQUAL( true, 0 < bought_bytes );

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", bought_bytes ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("998.0049"), get_balance( "alice1111111" ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( true, total["ram_bytes"].as_uint64() == init_bytes );

   transfer( "eosio", "alice1111111", core_sym::from_string("100000000.0000"), "eosio" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100000998.0049"), get_balance( "alice1111111" ) );
   // alice buys ram for 10000000.0000, 0.5% = 50000.0000 go to ramfee
   // after fee 9950000.0000 go to bought bytes
   // when selling back bought bytes, pay 0.5% fee and get back 99.5% of 9950000.0000 = 9900250.0000
   // expected account after that is 90000998.0049 + 9900250.0000 = 99901248.0049 with a difference
   // of order 0.0001 due to rounding errors
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("90000998.0049"), get_balance( "alice1111111" ) );

   total = get_total_stake( "alice1111111" );
   bytes = total["ram_bytes"].as_uint64();
   bought_bytes = bytes - init_bytes;
   wdump((init_bytes)(bought_bytes)(bytes) );

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", bought_bytes ) );
   total = get_total_stake( "alice1111111" );

   bytes = total["ram_bytes"].as_uint64();
   bought_bytes = bytes - init_bytes;
   wdump((init_bytes)(bought_bytes)(bytes) );

   BOOST_REQUIRE_EQUAL( true, total["ram_bytes"].as_uint64() == init_bytes );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("99901248.0040"), get_balance( "alice1111111" ) );

   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("30.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("99900688.0040"), get_balance( "alice1111111" ) );

   auto newtotal = get_total_stake( "alice1111111" );

   auto newbytes = newtotal["ram_bytes"].as_uint64();
   bought_bytes = newbytes - bytes;
   wdump((newbytes)(bytes)(bought_bytes) );

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", bought_bytes ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("99901242.4175"), get_balance( "alice1111111" ) );

   newtotal = get_total_stake( "alice1111111" );
   auto startbytes = newtotal["ram_bytes"].as_uint64();

   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("300000.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("49301242.4175"), get_balance( "alice1111111" ) );

   auto finaltotal = get_total_stake( "alice1111111" );
   auto endbytes = finaltotal["ram_bytes"].as_uint64();

   bought_bytes = endbytes - startbytes;
   wdump((startbytes)(endbytes)(bought_bytes) );

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", bought_bytes ) );

   BOOST_REQUIRE_EQUAL( false, get_row_by_account( config::system_account_name, config::system_account_name,
                                                   N(rammarket), symbol{SY(4,RAMCORE)}.value() ).empty() );

   auto get_ram_market = [this]() -> fc::variant {
      vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name,
                                              N(rammarket), symbol{SY(4,RAMCORE)}.value() );
      BOOST_REQUIRE( !data.empty() );
      return abi_ser.binary_to_variant("exchange_state", data, abi_serializer_max_time);
   };

   {
      transfer( config::system_account_name, "alice1111111", core_sym::from_string("10000000.0000"), config::system_account_name );
      uint64_t bytes0 = get_total_stake( "alice1111111" )["ram_bytes"].as_uint64();

      auto market = get_ram_market();
      const asset r0 = market["base"].as<connector>().balance;
      const asset e0 = market["quote"].as<connector>().balance;
      BOOST_REQUIRE_EQUAL( asset::from_string("0 RAM").get_symbol(),     r0.get_symbol() );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000").get_symbol(), e0.get_symbol() );

      const asset payment = core_sym::from_string("10000000.0000");
      BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", payment ) );
      uint64_t bytes1 = get_total_stake( "alice1111111" )["ram_bytes"].as_uint64();

      const int64_t fee = (payment.get_amount() + 199) / 200;
      const double net_payment = payment.get_amount() - fee;
      const int64_t expected_delta = net_payment * r0.get_amount() / ( net_payment + e0.get_amount() );

      BOOST_REQUIRE_EQUAL( expected_delta, bytes1 -  bytes0 );
   }

   {
      transfer( config::system_account_name, "bob111111111", core_sym::from_string("100000.0000"), config::system_account_name );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("must reserve a positive amount"),
                           buyrambytes( "bob111111111", "bob111111111", 1 ) );

      uint64_t bytes0 = get_total_stake( "bob111111111" )["ram_bytes"].as_uint64();
      BOOST_REQUIRE_EQUAL( success(), buyrambytes( "bob111111111", "bob111111111", 1024 ) );
      uint64_t bytes1 = get_total_stake( "bob111111111" )["ram_bytes"].as_uint64();
      BOOST_REQUIRE( within_one( 1024, bytes1 - bytes0 ) );

      BOOST_REQUIRE_EQUAL( success(), buyrambytes( "bob111111111", "bob111111111", 1024 * 1024) );
      uint64_t bytes2 = get_total_stake( "bob111111111" )["ram_bytes"].as_uint64();
      BOOST_REQUIRE( within_one( 1024 * 1024, bytes2 - bytes1 ) );
   }

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_unstake, eosio_system_tester ) try {
   activate_network();

   produce_blocks( 10 );
   produce_block( fc::hours(3*24) );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );
   transfer( "eosio", "alice1111111", core_sym::from_string("1000.0000"), "eosio" );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( success(), stake( "eosio", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());

   const auto init_eosio_stake_balance = get_balance( N(eosio.stake) );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( init_eosio_stake_balance + core_sym::from_string("300.0000"), get_balance( N(eosio.stake) ) );
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   produce_block( fc::hours(3*24-1) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( init_eosio_stake_balance + core_sym::from_string("300.0000"), get_balance( N(eosio.stake) ) );
   //after 3 days funds should be released
   produce_block( fc::hours(1) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( init_eosio_stake_balance, get_balance( N(eosio.stake) ) );

   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   total = get_total_stake("bob111111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());

   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000").get_amount(), total["net_weight"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000").get_amount(), total["cpu_weight"].as<asset>().get_amount() );

   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000")), get_voter_info( "alice1111111" ) );

   auto bytes = total["ram_bytes"].as_uint64();
   BOOST_REQUIRE_EQUAL( true, 0 < bytes );

   //unstake from bob111111111
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "bob111111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   total = get_total_stake("bob111111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["cpu_weight"].as<asset>());
   produce_block( fc::hours(3*24-1) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   //after 3 days funds should be released
   produce_block( fc::hours(1) );
   produce_blocks(1);

   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("0.0000") ), get_voter_info( "alice1111111" ) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance( "alice1111111" ) );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_unstake_with_transfer, eosio_system_tester ) try {
   activate_network();

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );

   //eosio stakes for alice with transfer flag

   transfer( "eosio", "bob111111111", core_sym::from_string("1000.0000"), "eosio" );
   BOOST_REQUIRE_EQUAL( success(), stake_with_transfer( "bob111111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   //check that alice has both bandwidth and voting power
   auto total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000")), get_voter_info( "alice1111111" ) );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );

   //alice stakes for herself
   transfer( "eosio", "alice1111111", core_sym::from_string("1000.0000"), "eosio" );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   //now alice's stake should be equal to transfered from eosio + own stake
   total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("410.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("600.0000")), get_voter_info( "alice1111111" ) );

   //alice can unstake everything (including what was transfered)
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "alice1111111", core_sym::from_string("400.0000"), core_sym::from_string("200.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   produce_block( fc::hours(3*24-1) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   //after 3 days funds should be released

   produce_block( fc::hours(1) );
   produce_blocks(1);

   BOOST_REQUIRE_EQUAL( core_sym::from_string("1300.0000"), get_balance( "alice1111111" ) );

   //stake should be equal to what was staked in constructor, voting power should be 0
   total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("0.0000")), get_voter_info( "alice1111111" ) );

   // Now alice stakes to bob with transfer flag
   BOOST_REQUIRE_EQUAL( success(), stake_with_transfer( "alice1111111", "bob111111111", core_sym::from_string("100.0000"), core_sym::from_string("100.0000") ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_to_self_with_transfer, eosio_system_tester ) try {
   activate_network();

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );
   transfer( "eosio", "alice1111111", core_sym::from_string("1000.0000"), "eosio" );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("cannot use transfer flag if delegating to self"),
                        stake_with_transfer( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") )
   );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_while_pending_refund, eosio_system_tester ) try {
   activate_network();

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );

   //eosio stakes for alice with transfer flag
   transfer( "eosio", "bob111111111", core_sym::from_string("1000.0000"), "eosio" );
   BOOST_REQUIRE_EQUAL( success(), stake_with_transfer( "bob111111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   //check that alice has both bandwidth and voting power
   auto total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000")), get_voter_info( "alice1111111" ) );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );

   //alice stakes for herself
   transfer( "eosio", "alice1111111", core_sym::from_string("1000.0000"), "eosio" );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   //now alice's stake should be equal to transfered from eosio + own stake
   total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("410.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("600.0000")), get_voter_info( "alice1111111" ) );

   //alice can unstake everything (including what was transfered)
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "alice1111111", core_sym::from_string("400.0000"), core_sym::from_string("200.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   produce_block( fc::hours(3*24-1) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   //after 3 days funds should be released

   produce_block( fc::hours(1) );
   produce_blocks(1);

   BOOST_REQUIRE_EQUAL( core_sym::from_string("1300.0000"), get_balance( "alice1111111" ) );

   //stake should be equal to what was staked in constructor, voting power should be 0
   total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("0.0000")), get_voter_info( "alice1111111" ) );

   // Now alice stakes to bob with transfer flag
   BOOST_REQUIRE_EQUAL( success(), stake_with_transfer( "alice1111111", "bob111111111", core_sym::from_string("100.0000"), core_sym::from_string("100.0000") ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( fail_without_auth, eosio_system_tester ) try {
   activate_network();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( success(), stake( "eosio", "alice1111111", core_sym::from_string("2000.0000"), core_sym::from_string("1000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("10.0000"), core_sym::from_string("10.0000") ) );

   BOOST_REQUIRE_EQUAL( error("missing authority of alice1111111"),
                        push_action( N(alice1111111), N(delegatebw), mvo()
                                    ("from",     "alice1111111")
                                    ("receiver", "bob111111111")
                                    ("stake_net_quantity", core_sym::from_string("10.0000"))
                                    ("stake_cpu_quantity", core_sym::from_string("10.0000"))
                                    ("transfer", 0 )
                                    ,false
                        )
   );

   BOOST_REQUIRE_EQUAL( error("missing authority of alice1111111"),
                        push_action(N(alice1111111), N(undelegatebw), mvo()
                                    ("from",     "alice1111111")
                                    ("receiver", "bob111111111")
                                    ("unstake_net_quantity", core_sym::from_string("200.0000"))
                                    ("unstake_cpu_quantity", core_sym::from_string("100.0000"))
                                    ("transfer", 0 )
                                    ,false
                        )
   );
   //REQUIRE_MATCHING_OBJECT( , get_voter_info( "alice1111111" ) );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( stake_negative, eosio_system_tester ) try {
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must stake a positive amount"),
                        stake( "alice1111111", core_sym::from_string("-0.0001"), core_sym::from_string("0.0000") )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must stake a positive amount"),
                        stake( "alice1111111", core_sym::from_string("0.0000"), core_sym::from_string("-0.0001") )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must stake a positive amount"),
                        stake( "alice1111111", core_sym::from_string("00.0000"), core_sym::from_string("00.0000") )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must stake a positive amount"),
                        stake( "alice1111111", core_sym::from_string("0.0000"), core_sym::from_string("00.0000") )

   );

   BOOST_REQUIRE_EQUAL( true, get_voter_info( "alice1111111" ).is_null() );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( unstake_negative, eosio_system_tester ) try {
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("200.0001"), core_sym::from_string("100.0001") ) );

   auto total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0001"), total["net_weight"].as<asset>());
   auto vinfo = get_voter_info("alice1111111" );
   wdump((vinfo));
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0002") ), get_voter_info( "alice1111111" ) );


   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must unstake a positive amount"),
                        unstake( "alice1111111", "bob111111111", core_sym::from_string("-1.0000"), core_sym::from_string("0.0000") )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must unstake a positive amount"),
                        unstake( "alice1111111", "bob111111111", core_sym::from_string("0.0000"), core_sym::from_string("-1.0000") )
   );

   //unstake all zeros
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must unstake a positive amount"),
                        unstake( "alice1111111", "bob111111111", core_sym::from_string("0.0000"), core_sym::from_string("0.0000") )

   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( unstake_more_than_at_stake, eosio_system_tester ) try {
   activate_network();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());

   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   //trying to unstake more net bandwith than at stake

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked net bandwidth"),
                        unstake( "alice1111111", core_sym::from_string("200.0001"), core_sym::from_string("0.0000") )
   );

   //trying to unstake more cpu bandwith than at stake
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked cpu bandwidth"),
                        unstake( "alice1111111", core_sym::from_string("0.0000"), core_sym::from_string("100.0001") )

   );

   //check that nothing has changed
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( delegate_to_another_user, eosio_system_tester ) try {
   activate_network();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( success(), stake ( "alice1111111", "bob111111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   //all voting power goes to alice1111111
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000") ), get_voter_info( "alice1111111" ) );
   //but not to bob111111111
   BOOST_REQUIRE_EQUAL( true, get_voter_info( "bob111111111" ).is_null() );

   //bob111111111 should not be able to unstake what was staked by alice1111111
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked cpu bandwidth"),
                        unstake( "bob111111111", core_sym::from_string("0.0000"), core_sym::from_string("10.0000") )

   );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked net bandwidth"),
                        unstake( "bob111111111", core_sym::from_string("10.0000"),  core_sym::from_string("0.0000") )
   );

   issue_and_transfer( "carol1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "carol1111111", "bob111111111", core_sym::from_string("20.0000"), core_sym::from_string("10.0000") ) );
   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("230.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("120.0000"), total["cpu_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("970.0000"), get_balance( "carol1111111" ) );
   REQUIRE_MATCHING_OBJECT( voter( "carol1111111", core_sym::from_string("30.0000") ), get_voter_info( "carol1111111" ) );

   //alice1111111 should not be able to unstake money staked by carol1111111

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked net bandwidth"),
                        unstake( "alice1111111", "bob111111111", core_sym::from_string("2001.0000"), core_sym::from_string("1.0000") )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked cpu bandwidth"),
                        unstake( "alice1111111", "bob111111111", core_sym::from_string("1.0000"), core_sym::from_string("101.0000") )

   );

   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("230.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("120.0000"), total["cpu_weight"].as<asset>());
   //balance should not change after unsuccessfull attempts to unstake
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   //voting power too
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000") ), get_voter_info( "alice1111111" ) );
   REQUIRE_MATCHING_OBJECT( voter( "carol1111111", core_sym::from_string("30.0000") ), get_voter_info( "carol1111111" ) );
   BOOST_REQUIRE_EQUAL( true, get_voter_info( "bob111111111" ).is_null() );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( stake_unstake_separate, eosio_system_tester ) try {
   activate_network();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance( "alice1111111" ) );

   //everything at once
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("10.0000"), core_sym::from_string("20.0000") ) );
   auto total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("20.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("30.0000"), total["cpu_weight"].as<asset>());

   //cpu
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("100.0000"), core_sym::from_string("0.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("120.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("30.0000"), total["cpu_weight"].as<asset>());

   //net
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("0.0000"), core_sym::from_string("200.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("120.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("230.0000"), total["cpu_weight"].as<asset>());

   //unstake cpu
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", core_sym::from_string("100.0000"), core_sym::from_string("0.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("20.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("230.0000"), total["cpu_weight"].as<asset>());

   //unstake net
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", core_sym::from_string("0.0000"), core_sym::from_string("200.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("20.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("30.0000"), total["cpu_weight"].as<asset>());
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( adding_stake_partial_unstake, eosio_system_tester ) try {
   activate_network();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000") ), get_voter_info( "alice1111111" ) );

   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("100.0000"), core_sym::from_string("50.0000") ) );

   auto total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("310.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("160.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("450.0000") ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("550.0000"), get_balance( "alice1111111" ) );

   //unstake a share
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "bob111111111", core_sym::from_string("150.0000"), core_sym::from_string("75.0000") ) );

   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("160.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("85.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("225.0000") ), get_voter_info( "alice1111111" ) );

   //unstake more
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "bob111111111", core_sym::from_string("50.0000"), core_sym::from_string("25.0000") ) );
   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("60.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("150.0000") ), get_voter_info( "alice1111111" ) );

   //combined amount should be available only in 3 days
   produce_block( fc::days(2) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("550.0000"), get_balance( "alice1111111" ) );
   produce_block( fc::days(1) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("850.0000"), get_balance( "alice1111111" ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_from_refund, eosio_system_tester ) try {
   activate_network();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());

   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );

   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("60.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("60.0000"), total["cpu_weight"].as<asset>());

   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("400.0000") ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("600.0000"), get_balance( "alice1111111" ) );

   //unstake a share
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "alice1111111", core_sym::from_string("100.0000"), core_sym::from_string("50.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("60.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("250.0000") ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("600.0000"), get_balance( "alice1111111" ) );
   auto refund = get_refund_request( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "50.0000"), refund["cpu_amount"].as<asset>() );
   //XXX auto request_time = refund["request_time"].as_int64();

   //alice delegates to bob, should pull from liquid balance not refund
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("60.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("350.0000") ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("500.0000"), get_balance( "alice1111111" ) );
   refund = get_refund_request( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "50.0000"), refund["cpu_amount"].as<asset>() );

   //stake less than pending refund, entire amount should be taken from refund
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("50.0000"), core_sym::from_string("25.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("160.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("85.0000"), total["cpu_weight"].as<asset>());
   refund = get_refund_request( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("50.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("25.0000"), refund["cpu_amount"].as<asset>() );
   //request time should stay the same
   //BOOST_REQUIRE_EQUAL( request_time, refund["request_time"].as_int64() );
   //balance should stay the same
   BOOST_REQUIRE_EQUAL( core_sym::from_string("500.0000"), get_balance( "alice1111111" ) );

   //stake exactly pending refund amount
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("50.0000"), core_sym::from_string("25.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   //pending refund should be removed
   refund = get_refund_request( "alice1111111" );
   BOOST_TEST_REQUIRE( refund.is_null() );
   //balance should stay the same
   BOOST_REQUIRE_EQUAL( core_sym::from_string("500.0000"), get_balance( "alice1111111" ) );

   //create pending refund again
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["cpu_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("500.0000"), get_balance( "alice1111111" ) );
   refund = get_refund_request( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("200.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100.0000"), refund["cpu_amount"].as<asset>() );

   //stake more than pending refund
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("300.0000"), core_sym::from_string("200.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("310.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["cpu_weight"].as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("700.0000") ), get_voter_info( "alice1111111" ) );
   refund = get_refund_request( "alice1111111" );
   BOOST_TEST_REQUIRE( refund.is_null() );
   //200 core tokens should be taken from alice's account
   BOOST_REQUIRE_EQUAL( core_sym::from_string("300.0000"), get_balance( "alice1111111" ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_to_another_user_not_from_refund, eosio_system_tester ) try {
   activate_network();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000") ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   //unstake
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   auto refund = get_refund_request( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("200.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100.0000"), refund["cpu_amount"].as<asset>() );
   //auto orig_request_time = refund["request_time"].as_int64();

   //stake to another user
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   //stake should be taken from alices' balance, and refund request should stay the same
   BOOST_REQUIRE_EQUAL( core_sym::from_string("400.0000"), get_balance( "alice1111111" ) );
   refund = get_refund_request( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("200.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100.0000"), refund["cpu_amount"].as<asset>() );
   //BOOST_REQUIRE_EQUAL( orig_request_time, refund["request_time"].as_int64() );

} FC_LOG_AND_RETHROW()

// Tests for voting
BOOST_FIXTURE_TEST_CASE( producer_register_unregister, eosio_system_tester ) try {
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   //fc::variant params = producer_parameters_example(1);
   auto key =  fc::crypto::public_key( std::string("EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV") );
   BOOST_REQUIRE_EQUAL( success(), push_action(N(alice1111111), N(regproducer), mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", key )
                                               ("url", "http://block.one")
                                               ("location", 1)
                        )
   );

   auto info = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( 0, info["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "http://block.one", info["url"].as_string() );

   //change parameters one by one to check for things like #3783
   //fc::variant params2 = producer_parameters_example(2);
   BOOST_REQUIRE_EQUAL( success(), push_action(N(alice1111111), N(regproducer), mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", key )
                                               ("url", "http://block.two")
                                               ("location", 1)
                        )
   );
   info = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( key, fc::crypto::public_key(info["producer_key"].as_string()) );
   BOOST_REQUIRE_EQUAL( "http://block.two", info["url"].as_string() );
   BOOST_REQUIRE_EQUAL( 1, info["location"].as_int64() );

   auto key2 =  fc::crypto::public_key( std::string("EOS5eVr9TVnqwnUBNwf9kwMTbrHvX5aPyyEG97dz2b2TNeqWRzbJf") );
   BOOST_REQUIRE_EQUAL( success(), push_action(N(alice1111111), N(regproducer), mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", key2 )
                                               ("url", "http://block.two")
                                               ("location", 2)
                        )
   );
   info = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( key2, fc::crypto::public_key(info["producer_key"].as_string()) );
   BOOST_REQUIRE_EQUAL( "http://block.two", info["url"].as_string() );
   BOOST_REQUIRE_EQUAL( 2, info["location"].as_int64() );

   //unregister producer
   BOOST_REQUIRE_EQUAL( success(), push_action(N(alice1111111), N(unregprod), mvo()
                                               ("producer",  "alice1111111")
                        )
   );
   info = get_producer_info( "alice1111111" );
   //key should be empty
   BOOST_REQUIRE_EQUAL( fc::crypto::public_key(), fc::crypto::public_key(info["producer_key"].as_string()) );
   //everything else should stay the same
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( 0, info["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "http://block.two", info["url"].as_string() );

   //unregister bob111111111 who is not a producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "producer not found" ),
                        push_action( N(bob111111111), N(unregprod), mvo()
                                     ("producer",  "bob111111111")
                        )
   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( vote_for_producer, eosio_system_tester, * boost::unit_test::tolerance(1e-4) ) try {
   activate_network();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   fc::variant params = producer_parameters_example(1);
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproducer), mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( N(alice1111111), "active") )
                                               ("url", "http://block.one")
                                               ("location", 0 )
                        )
   );
   // ALICE is the SINGLE producer in this test case, so we can also test the total_producer_vote_weight for all the actions

   auto prod = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( "alice1111111", prod["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( 0, prod["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "http://block.one", prod["url"].as_string() );

   issue_and_transfer( "bob111111111", core_sym::from_string("2000.0000"),  config::system_account_name );
   issue_and_transfer( "carol1111111", core_sym::from_string("3000.0000"),  config::system_account_name );

   //bob111111111 makes stake
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("11.0000"), core_sym::from_string("0.1111") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1988.8889"), get_balance( "bob111111111" ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("11.1111") ), get_voter_info( "bob111111111" ) );

   BOOST_REQUIRE_EQUAL( get_global_state()["total_producer_vote_weight"].as<double>(), get_producer_info( "alice1111111" )["total_votes"].as_double() );

	auto total = get_total_stake("bob111111111");
   //bob111111111 votes for alice1111111
   BOOST_REQUIRE_EQUAL( success(), vote( N(bob111111111), { N(alice1111111) } ) );

   //check that producer parameters stay the same after voting
   prod = get_producer_info( "alice1111111" );
   BOOST_TEST( stake2votes(core_sym::from_string("11.1111"), 1, 1) == prod["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "alice1111111", prod["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( "http://block.one", prod["url"].as_string() );
   BOOST_REQUIRE_EQUAL( get_global_state()["total_producer_vote_weight"].as<double>(), get_producer_info( "alice1111111" )["total_votes"].as_double() );

   //carol1111111 makes stake
   BOOST_REQUIRE_EQUAL( success(), stake( "carol1111111", core_sym::from_string("22.0000"), core_sym::from_string("0.2222") ) );
   REQUIRE_MATCHING_OBJECT( voter( "carol1111111", core_sym::from_string("22.2222") ), get_voter_info( "carol1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("2977.7778"), get_balance( "carol1111111" ) );

	total = get_total_stake("carol1111111");
	//carol1111111 votes for alice1111111
   BOOST_REQUIRE_EQUAL( success(), vote( N(carol1111111), { N(alice1111111) } ) );

   //new stake votes be added to alice1111111's total_votes
   prod = get_producer_info( "alice1111111" );
   BOOST_TEST( stake2votes(core_sym::from_string("22.2222"), 1, 1) + stake2votes(core_sym::from_string("11.1111"), 1, 1) == prod["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( get_global_state()["total_producer_vote_weight"].as<double>(), get_producer_info( "alice1111111" )["total_votes"].as_double() );

   //bob111111111 increases his stake
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("33.0000"), core_sym::from_string("0.3333") ) );

   prod = get_producer_info( "alice1111111" );
   BOOST_TEST( stake2votes(core_sym::from_string("22.2222"), 1, 1) + stake2votes(core_sym::from_string("44.4444"), 1, 1) == prod["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( get_global_state()["total_producer_vote_weight"].as<double>(), get_producer_info( "alice1111111" )["total_votes"].as_double() );

	total = get_total_stake("bob111111111");

   //alice1111111 stake with transfer to bob111111111
   BOOST_REQUIRE_EQUAL( success(), stake_with_transfer( "alice1111111", "bob111111111", core_sym::from_string("22.0000"), core_sym::from_string("0.2222") ) );

   total = get_total_stake("bob111111111");
	total = get_total_stake("carol1111111");

   //should increase alice1111111's total_votes
   prod = get_producer_info( "alice1111111" );
   BOOST_TEST( stake2votes(core_sym::from_string("22.2222"), 1, 1) + stake2votes(core_sym::from_string("66.6666"), 1, 1) == prod["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( get_global_state()["total_producer_vote_weight"].as<double>(), get_producer_info( "alice1111111" )["total_votes"].as_double() );

   //carol1111111 unstakes part of the stake
   BOOST_REQUIRE_EQUAL( success(), unstake( "carol1111111", core_sym::from_string("2.0000"), core_sym::from_string("0.0002")/*"2.0000 EOS", "0.0002 EOS"*/ ) );

	total = get_total_stake("carol1111111");

   //should decrease alice1111111's total_votes
   prod = get_producer_info( "alice1111111" );
   wdump((prod));
   BOOST_TEST( stake2votes(core_sym::from_string("20.2220"), 1, 1) + stake2votes(core_sym::from_string("66.6666"), 1, 1) == prod["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( get_global_state()["total_producer_vote_weight"].as<double>(), get_producer_info( "alice1111111" )["total_votes"].as_double() );

   //bob111111111 revokes his vote
   BOOST_REQUIRE_EQUAL( success(), vote( N(bob111111111), vector<account_name>() ) );

	total = get_total_stake("bob111111111");
   total = get_total_stake("alice1111111");

   //should decrease alice1111111's total_votes
   prod = get_producer_info( "alice1111111" );
   BOOST_TEST( stake2votes(core_sym::from_string("20.2220"), 1, 1) == prod["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( get_global_state()["total_producer_vote_weight"].as<double>(), get_producer_info( "alice1111111" )["total_votes"].as_double() );

   //but eos should still be at stake
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1955.5556"), get_balance( "bob111111111" ) );

   //carol1111111 unstakes rest of eos
   BOOST_REQUIRE_EQUAL( success(), unstake( "carol1111111", core_sym::from_string("20.0000"), core_sym::from_string("0.2220") ) );

	total = get_total_stake("carol1111111");

   //should decrease alice1111111's total_votes to zero
   prod = get_producer_info( "alice1111111" );
   BOOST_TEST( 0.0 == prod["total_votes"].as_double() );
   BOOST_TEST( get_global_state()["total_producer_vote_weight"].as<double>() == get_producer_info( "alice1111111" )["total_votes"].as_double() );

   //carol1111111 should receive funds in 3 days
   produce_block( fc::days(3) );
   produce_block();
   BOOST_REQUIRE_EQUAL( core_sym::from_string("3000.0000"), get_balance( "carol1111111" ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( unregistered_producer_voting, eosio_system_tester, * boost::unit_test::tolerance(1e+5) ) try {
   issue_and_transfer( "bob111111111", core_sym::from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("13.0000"), core_sym::from_string("0.5791") ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("13.5791") ), get_voter_info( "bob111111111" ) );

   //bob111111111 should not be able to vote for alice1111111 who is not a producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "producer alice1111111 is not registered" ),
                        vote( N(bob111111111), { N(alice1111111) } ) );

   //alice1111111 registers as a producer
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   fc::variant params = producer_parameters_example(1);
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproducer), mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( N(alice1111111), "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );
   //and then unregisters
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(unregprod), mvo()
                                               ("producer",  "alice1111111")
                        )
   );
   //key should be empty
   auto prod = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( fc::crypto::public_key(), fc::crypto::public_key(prod["producer_key"].as_string()) );

   //bob111111111 should not be able to vote for alice1111111 who is an unregistered producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "producer alice1111111 is not currently registered" ),
                        vote( N(bob111111111), { N(alice1111111) } ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( more_than_30_producer_voting, eosio_system_tester ) try {
   issue_and_transfer( "bob111111111", core_sym::from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("13.0000"), core_sym::from_string("0.5791") ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("13.5791") ), get_voter_info( "bob111111111" ) );

   //bob111111111 should not be able to vote for alice1111111 who is not a producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "attempt to vote for too many producers" ),
                        vote( N(bob111111111), vector<account_name>(31, N(alice1111111)) ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( vote_same_producer_30_times, eosio_system_tester ) try {
   issue_and_transfer( "bob111111111", core_sym::from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("100.0000") ), get_voter_info( "bob111111111" ) );

   //alice1111111 becomes a producer
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   fc::variant params = producer_parameters_example(1);
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproducer), mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key(N(alice1111111), "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );

   //bob111111111 should not be able to vote for alice1111111 who is not a producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "producer votes must be unique and sorted" ),
                        vote( N(bob111111111), vector<account_name>(30, N(alice1111111)) ) );

   auto prod = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( 0 == prod["total_votes"].as_double() );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( producer_keep_votes, eosio_system_tester, * boost::unit_test::tolerance(1e+5) ) try {
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   fc::variant params = producer_parameters_example(1);
   vector<char> key = fc::raw::pack( get_public_key( N(alice1111111), "active" ) );
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproducer), mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( N(alice1111111), "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );

   //bob111111111 makes stake
   issue_and_transfer( "bob111111111", core_sym::from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("13.0000"), core_sym::from_string("0.5791") ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("13.5791") ), get_voter_info( "bob111111111" ) );

   //bob111111111 votes for alice1111111
   BOOST_REQUIRE_EQUAL( success(), vote(N(bob111111111), { N(alice1111111) } ) );

   auto prod = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("13.5791"), 1, 1) == prod["total_votes"].as_double() );

   //unregister producer
   BOOST_REQUIRE_EQUAL( success(), push_action(N(alice1111111), N(unregprod), mvo()
                                               ("producer",  "alice1111111")
                        )
   );
   prod = get_producer_info( "alice1111111" );
   //key should be empty
   BOOST_REQUIRE_EQUAL( fc::crypto::public_key(), fc::crypto::public_key(prod["producer_key"].as_string()) );
   //check parameters just in case
   //REQUIRE_MATCHING_OBJECT( params, prod["prefs"]);
   //votes should stay the same
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("13.5791"), 1, 1), prod["total_votes"].as_double() );

   //regtister the same producer again
   params = producer_parameters_example(2);
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproducer), mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( N(alice1111111), "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );
   prod = get_producer_info( "alice1111111" );
   //votes should stay the same
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("13.5791"), 1, 1), prod["total_votes"].as_double() );

   //change parameters
   params = producer_parameters_example(3);
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproducer), mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( N(alice1111111), "active") )
                                               ("url","")
                                               ("location", 0)
                        )
   );
   prod = get_producer_info( "alice1111111" );
   //votes should stay the same
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("13.5791"), 1, 1), prod["total_votes"].as_double() );
   //check parameters just in case
   //REQUIRE_MATCHING_OBJECT( params, prod["prefs"]);

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( vote_for_two_producers, eosio_system_tester, * boost::unit_test::tolerance(1e-4) ) try {
   //alice1111111 becomes a producer
   fc::variant params = producer_parameters_example(1);
   auto key = get_public_key( N(alice1111111), "active" );
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproducer), mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( N(alice1111111), "active") )
                                               ("url","")
                                               ("location", 0)
                        )
   );
   //bob111111111 becomes a producer
   params = producer_parameters_example(2);
   key = get_public_key( N(bob111111111), "active" );
   BOOST_REQUIRE_EQUAL( success(), push_action( N(bob111111111), N(regproducer), mvo()
                                               ("producer",  "bob111111111")
                                               ("producer_key", get_public_key( N(alice1111111), "active") )
                                               ("url","")
                                               ("location", 0)
                        )
   );

   //carol1111111 votes for alice1111111 and bob111111111
   issue_and_transfer( "carol1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "carol1111111", core_sym::from_string("15.0005"), core_sym::from_string("5.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(carol1111111), { N(alice1111111), N(bob111111111) } ) );

   auto alice_info = get_producer_info( "alice1111111" );
   BOOST_TEST( stake2votes(core_sym::from_string("20.0005"), 2, 2) == alice_info["total_votes"].as_double() );
   auto bob_info = get_producer_info( "bob111111111" );
   BOOST_TEST( stake2votes(core_sym::from_string("20.0005"), 2, 2) == bob_info["total_votes"].as_double() );

   //carol1111111 votes for alice1111111 (but revokes vote for bob111111111)
   BOOST_REQUIRE_EQUAL( success(), vote( N(carol1111111), { N(alice1111111) } ) );

   alice_info = get_producer_info( "alice1111111" );
   BOOST_TEST( stake2votes(core_sym::from_string("20.0005"), 1, 2) == alice_info["total_votes"].as_double() );
   bob_info = get_producer_info( "bob111111111" );
   BOOST_TEST( 0 == bob_info["total_votes"].as_double() );

   //alice1111111 votes for herself and bob111111111
   issue_and_transfer( "alice1111111", core_sym::from_string("2.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("1.0000"), core_sym::from_string("1.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), vote(N(alice1111111), { N(alice1111111), N(bob111111111) } ) );

   alice_info = get_producer_info( "alice1111111" );
   BOOST_TEST( stake2votes(core_sym::from_string("20.0005"), 1, 2) + stake2votes(core_sym::from_string("2.0000"), 2, 2) == alice_info["total_votes"].as_double() );

   bob_info = get_producer_info( "bob111111111" );
   BOOST_TEST( stake2votes(core_sym::from_string("2.0000"), 2, 2) == bob_info["total_votes"].as_double() );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( proxy_register_unregister_keeps_stake, eosio_system_tester ) try {
   //register proxy by first action for this user ever
   BOOST_REQUIRE_EQUAL( success(), push_action(N(alice1111111), N(regproxy), mvo()
                                               ("proxy",  "alice1111111")
                                               ("isproxy", true )
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" ), get_voter_info( "alice1111111" ) );

   //unregister proxy
   BOOST_REQUIRE_EQUAL( success(), push_action(N(alice1111111), N(regproxy), mvo()
                                               ("proxy",  "alice1111111")
                                               ("isproxy", false)
                        )
   );
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111" ), get_voter_info( "alice1111111" ) );

   //stake and then register as a proxy
   issue_and_transfer( "bob111111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("200.0002"), core_sym::from_string("100.0001") ) );
   BOOST_REQUIRE_EQUAL( success(), push_action( N(bob111111111), N(regproxy), mvo()
                                               ("proxy",  "bob111111111")
                                               ("isproxy", true)
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "bob111111111" )( "staked", 3000003 ), get_voter_info( "bob111111111" ) );
   //unrgister and check that stake is still in place
   BOOST_REQUIRE_EQUAL( success(), push_action( N(bob111111111), N(regproxy), mvo()
                                               ("proxy",  "bob111111111")
                                               ("isproxy", false)
                        )
   );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("300.0003") ), get_voter_info( "bob111111111" ) );

   //register as a proxy and then stake
   BOOST_REQUIRE_EQUAL( success(), push_action( N(carol1111111), N(regproxy), mvo()
                                               ("proxy",  "carol1111111")
                                               ("isproxy", true)
                        )
   );
   issue_and_transfer( "carol1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "carol1111111", core_sym::from_string("246.0002"), core_sym::from_string("531.0001") ) );
   //check that both proxy flag and stake a correct
   REQUIRE_MATCHING_OBJECT( proxy( "carol1111111" )( "staked", 7770003 ), get_voter_info( "carol1111111" ) );

   //unregister
   BOOST_REQUIRE_EQUAL( success(), push_action( N(carol1111111), N(regproxy), mvo()
                                                ("proxy",  "carol1111111")
                                                ("isproxy", false)
                        )
   );
   REQUIRE_MATCHING_OBJECT( voter( "carol1111111", core_sym::from_string("777.0003") ), get_voter_info( "carol1111111" ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( proxy_stake_unstake_keeps_proxy_flag, eosio_system_tester ) try {
   activate_network();

   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproxy), mvo()
                                               ("proxy",  "alice1111111")
                                               ("isproxy", true)
                        )
   );
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" ), get_voter_info( "alice1111111" ) );

   //stake
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("100.0000"), core_sym::from_string("50.0000") ) );
   //check that account is still a proxy
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" )( "staked", 1500000 ), get_voter_info( "alice1111111" ) );

   //stake more
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("30.0000"), core_sym::from_string("20.0000") ) );
   //check that account is still a proxy
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" )("staked", 2000000 ), get_voter_info( "alice1111111" ) );

   //unstake more
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", core_sym::from_string("65.0000"), core_sym::from_string("35.0000") ) );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" )("staked", 1000000 ), get_voter_info( "alice1111111" ) );

   //unstake the rest
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", core_sym::from_string("65.0000"), core_sym::from_string("35.0000") ) );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" )( "staked", 0 ), get_voter_info( "alice1111111" ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( proxy_actions_affect_producers, eosio_system_tester, * boost::unit_test::tolerance(10) ) try {
   activate_network();

   create_accounts_with_resources( {  N(defproducer1), N(defproducer2), N(defproducer3) } );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer1", 1) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer2", 2) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer3", 3) );

   //register as a proxy
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproxy), mvo()
                                                ("proxy",  "alice1111111")
                                                ("isproxy", true)
                        )
   );

   //accumulate proxied votes
   issue_and_transfer( "bob111111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("100.0002"), core_sym::from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( success(), vote(N(bob111111111), vector<account_name>(), N(alice1111111) ) );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" )( "proxied_vote_weight", 1500003 ), get_voter_info( "alice1111111" ) );

   //vote for producers
   BOOST_REQUIRE_EQUAL( success(), vote(N(alice1111111), { N(defproducer1), N(defproducer2) } ) );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("150.0003"), 2, 3) == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("150.0003"), 2, 3) == get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( 0 == get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //vote for another producers
   BOOST_REQUIRE_EQUAL( success(), vote( N(alice1111111), { N(defproducer1), N(defproducer3) } ) );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("150.0003"), 2, 3) == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("150.0003"), 2, 3) == get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //unregister proxy
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproxy), mvo()
                                                ("proxy",  "alice1111111")
                                                ("isproxy", false)
                        )
   );
   //REQUIRE_MATCHING_OBJECT( voter( "alice1111111" )( "proxied_vote_weight", stake2votes(core_sym::from_string("150.0003")) ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //register proxy again
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproxy), mvo()
                                                ("proxy",  "alice1111111")
                                                ("isproxy", true)
                        )
   );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("150.0003"), 2, 3) == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("150.0003"), 2, 3) == get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //stake increase by proxy itself affects producers
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("30.0001"), core_sym::from_string("20.0001") ) );
   BOOST_REQUIRE_EQUAL( stake2votes(core_sym::from_string("200.0005"), 2, 3), get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( stake2votes(core_sym::from_string("200.0005"), 2, 3), get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //stake decrease by proxy itself affects producers
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", core_sym::from_string("10.0001"), core_sym::from_string("10.0001") ) );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("180.0003"), 2, 3) == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("180.0003"), 2, 3) == get_producer_info( "defproducer3" )["total_votes"].as_double() );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(producer_pay, eosio_system_tester, * boost::unit_test::tolerance(1e-10)) try {
   const double usecs_per_year  = 52 * 7 * 24 * 3600 * 1000000ll;
   const double secs_per_year   = 52 * 7 * 24 * 3600;

   const asset large_asset = core_sym::from_string("80.0000");
   create_account_with_resources( N(defproducera), config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( N(defproducerb), config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( N(defproducerc), config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );

   create_account_with_resources( N(producvotera), config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( N(producvoterb), config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );

   BOOST_REQUIRE_EQUAL(success(), regproducer(N(defproducera)));
   auto prod = get_producer_info( N(defproducera) );
   BOOST_REQUIRE_EQUAL("defproducera", prod["owner"].as_string());
   BOOST_REQUIRE_EQUAL(0, prod["total_votes"].as_double());                                                                               

   transfer(config::system_account_name, "producvotera", core_sym::from_string("400000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvotera", core_sym::from_string("100000000.0000"), core_sym::from_string("100000000.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote( N(producvotera), { N(defproducera) }));

   produce_blocks((1000 - get_global_state()["block_num"].as<uint32_t>()) + 1);
   transfer(name("eosio"), name("exrsrv.tf"), core_sym::from_string("400000000.0000"), config::system_account_name);
   {
      const uint32_t last_claim_time = get_global_state()["last_claimrewards"].as<uint32_t>();
      produce_blocks(3598);
      BOOST_REQUIRE_EQUAL(control->active_producers().version, 1);
      BOOST_REQUIRE_EQUAL(last_claim_time, get_global_state()["last_claimrewards"].as<uint32_t>());

      const fc::variant     initial_global_state      = get_global_state();
      const uint64_t initial_claim_time        = microseconds_since_epoch_of_iso_string( initial_global_state["last_pervote_bucket_fill"] );
      const int64_t  initial_pervote_bucket    = initial_global_state["pervote_bucket"].as<int64_t>();

      const int64_t  initial_perblock_bucket   = initial_global_state["perblock_bucket"].as<int64_t>();
      const int64_t  initial_savings           = get_balance(N(eosio.saving)).get_amount();
      const uint32_t initial_tot_unpaid_blocks = initial_global_state["total_unpaid_blocks"].as<uint32_t>();
      
      prod = get_producer_info("defproducera");
      const uint32_t unpaid_blocks = prod["unpaid_blocks"].as<uint32_t>();
      const bool is_active = prod["is_active"].as<bool>();
      
      BOOST_REQUIRE(is_active);
      BOOST_REQUIRE(1 < unpaid_blocks);

      BOOST_REQUIRE_EQUAL(initial_tot_unpaid_blocks, unpaid_blocks);

      const asset initial_supply  = get_token_supply();
      const asset initial_balance = get_balance(N(defproducera));
      const asset initial_wps_balance = get_balance(N(eosio.saving));
      const asset initial_bpay_balance = get_balance(N(eosio.bpay));
      const asset initial_tedp_balance = get_balance(N(exrsrv.tf));

      BOOST_REQUIRE_EQUAL(wasm_assert_msg("No payment exists for account"),
                          push_action(N(defproducera), N(claimrewards), mvo()("owner", "defproducera")));

      produce_blocks();

      const fc::variant pay_rate_info = get_payrate_info();
      const uint64_t bpay_rate = pay_rate_info["bpay_rate"].as<uint64_t>();;
      const uint64_t worker_amount = pay_rate_info["worker_amount"].as<uint64_t>();

      const auto     global_state                  = get_global_state();
      const uint64_t claim_time                    = microseconds_since_epoch_of_iso_string( global_state["last_pervote_bucket_fill"] );
      const int64_t  pervote_bucket                = global_state["pervote_bucket"].as<int64_t>();
      const int64_t  perblock_bucket               = global_state["perblock_bucket"].as<int64_t>();
      const time_point last_pervote_bucket_fill    = global_state["last_pervote_bucket_fill"].as<time_point>();
      const int64_t  savings                       = get_balance(N(eosio.saving)).get_amount();
      const uint32_t tot_unpaid_blocks             = global_state["total_unpaid_blocks"].as<uint32_t>();

      auto usecs_between_fills = claim_time - initial_claim_time;
      int32_t secs_between_fills = usecs_between_fills / 1000000;

      const double bpay_rate_percent = bpay_rate / double(100000);
      auto to_workers = static_cast<int64_t>((12 * double(worker_amount) * double(usecs_between_fills)) / double(usecs_per_year));
      auto to_producers = static_cast<int64_t>((bpay_rate_percent * double(initial_supply.get_amount()) * double(usecs_between_fills)) / double(usecs_per_year));

      prod = get_producer_info("defproducera");
      asset to_bpay = asset(to_producers, symbol{CORE_SYM});
      asset to_wps = asset(to_workers, symbol{CORE_SYM});
      asset new_tokens = asset(to_workers + to_producers, symbol{CORE_SYM});
      
      BOOST_REQUIRE_EQUAL(0, prod["unpaid_blocks"].as<uint32_t>());
      BOOST_REQUIRE_EQUAL(0, tot_unpaid_blocks);
      BOOST_REQUIRE_EQUAL(get_balance(N(eosio.bpay)), initial_bpay_balance + to_bpay);
      BOOST_REQUIRE_EQUAL(get_balance(N(eosio.saving)), initial_wps_balance + to_wps);
      const asset supply  = get_token_supply();
      const asset balance = get_balance(N(defproducera));
      BOOST_REQUIRE_EQUAL(supply, initial_supply);
      BOOST_REQUIRE_EQUAL(get_balance(N(exrsrv.tf)), initial_tedp_balance - new_tokens);
      const asset payment = get_payment_info(N(defproducera))["pay"].as<asset>();
      BOOST_REQUIRE_EQUAL(payment, to_bpay);
      const asset initial_prod_balance = get_balance(N(defproducera));
      push_action(N(defproducera), N(claimrewards), mvo()("owner", "defproducera"));

      BOOST_REQUIRE_EQUAL(get_balance(N(defproducera)), initial_prod_balance + payment);
      BOOST_REQUIRE_EQUAL(claim_time, microseconds_since_epoch_of_iso_string( prod["last_claim_time"] ));

      BOOST_REQUIRE(get_payment_info(N(defproducera)).is_null());
   }
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(multi_producer_pay, eosio_system_tester, * boost::unit_test::tolerance(1e-10)) try {
   const double usecs_per_year  = 52 * 7 * 24 * 3600 * 1000000ll;
   const double secs_per_year   = 52 * 7 * 24 * 3600;

   std::vector<account_name> producer_names;
   {
      producer_names.reserve(52);
      const std::string root("tprod");
      for(uint8_t i = 0; i < 52; i++) {
         name p = name(root + toBase31(i));
         producer_names.emplace_back(p);
      }

      setup_producer_accounts(producer_names);
      for ( const auto& p: producer_names ) {
         BOOST_REQUIRE_EQUAL( success(), regproducer(p) );
         BOOST_TEST_REQUIRE( 0 == get_producer_info(p)["total_votes"].as<double>() );
      }
      std::sort(producer_names.begin(), producer_names.end());
   }

   const asset large_asset = core_sym::from_string("80.0000");

   create_account_with_resources( N(producvotera), config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( N(producvoterb), config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );                                                                            

   auto prod = get_producer_info(producer_names[0]);
   BOOST_REQUIRE_EQUAL(producer_names[0], prod["owner"].as_string());
   BOOST_REQUIRE_EQUAL(0, prod["total_votes"].as_double());
   // TODO: INCREASE VOTER As stake
   transfer(config::system_account_name, "producvotera", core_sym::from_string("400000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvotera", core_sym::from_string("150000000.0000"), core_sym::from_string("150000000.0000")));
   transfer(config::system_account_name, "producvoterb", core_sym::from_string("400000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvoterb", core_sym::from_string("100000000.0000"), core_sym::from_string("100000000.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote( N(producvotera), vector<name>( producer_names.begin(), producer_names.begin() + 30 )));
   BOOST_REQUIRE_EQUAL(success(), vote( N(producvoterb), vector<name>( producer_names.begin() + 31, producer_names.begin() + 51 )));

   produce_blocks((1000 - get_global_state()["block_num"].as<uint32_t>()) + 1);\
   {
      const uint32_t last_claim_time = get_global_state()["last_claimrewards"].as<uint32_t>();
      produce_blocks(3598);
      BOOST_REQUIRE_EQUAL(control->active_producers().version, 1);
      BOOST_REQUIRE_EQUAL(last_claim_time, get_global_state()["last_claimrewards"].as<uint32_t>());

      const fc::variant initial_global_state = get_global_state();
      const uint64_t initial_claim_time        = microseconds_since_epoch_of_iso_string( initial_global_state["last_pervote_bucket_fill"] );
      const int64_t  initial_pervote_bucket    = initial_global_state["pervote_bucket"].as<int64_t>();

      const int64_t  initial_perblock_bucket   = initial_global_state["perblock_bucket"].as<int64_t>();
      const int64_t  initial_savings           = get_balance(N(eosio.saving)).get_amount();
      const uint32_t initial_tot_unpaid_blocks = initial_global_state["total_unpaid_blocks"].as<uint32_t>();
      
      // TODO: check why this is off by three
      BOOST_REQUIRE_EQUAL(initial_global_state["total_unpaid_blocks"], 3595); 

      const asset initial_supply  = get_token_supply();
      const asset initial_balance = get_balance(producer_names[0]);
      const asset initial_wps_balance = get_balance(N(eosio.saving));
      const asset initial_bpay_balance = get_balance(N(eosio.bpay));

      BOOST_REQUIRE_EQUAL(wasm_assert_msg("No payment exists for account"),
                          push_action(producer_names[0], N(claimrewards), mvo()("owner", producer_names[0])));

      auto comparator = [&](fc::variant a, fc::variant b) -> bool {
         return a["total_votes"].as<double>() > b["total_votes"].as<double>();
      };

      produce_blocks();

      const fc::variant pay_rate_info = get_payrate_info();
      const uint64_t bpay_rate = pay_rate_info["bpay_rate"].as<uint64_t>();;
      const uint64_t worker_amount = pay_rate_info["worker_amount"].as<uint64_t>();

      const auto     global_state                  = get_global_state();
      const uint64_t claim_time                    = microseconds_since_epoch_of_iso_string( global_state["last_pervote_bucket_fill"] );
      const int64_t  pervote_bucket                = global_state["pervote_bucket"].as<int64_t>();
      const int64_t  perblock_bucket               = global_state["perblock_bucket"].as<int64_t>();
      const time_point last_pervote_bucket_fill    = global_state["last_pervote_bucket_fill"].as<time_point>();
      const int64_t  savings                       = get_balance(N(eosio.saving)).get_amount();
      const uint32_t tot_unpaid_blocks             = global_state["total_unpaid_blocks"].as<uint32_t>();

      auto usecs_between_fills = claim_time - initial_claim_time;
      int32_t secs_between_fills = usecs_between_fills / 1000000;

      const double bpay_rate_percent = bpay_rate / double(100000);
      auto to_workers = static_cast<int64_t>((12 * double(worker_amount) * double(usecs_between_fills)) / double(usecs_per_year));
      auto to_producers = static_cast<int64_t>((bpay_rate_percent * double(initial_supply.get_amount()) * double(usecs_between_fills)) / double(usecs_per_year));

      asset to_bpay = asset(to_producers, symbol{CORE_SYM});
      asset to_wps = asset(to_workers, symbol{CORE_SYM});
      asset new_tokens = asset(to_workers + to_producers, symbol{CORE_SYM});

      vector<fc::variant> producer_infos;
      for(const name &p : producer_names) {
         producer_infos.emplace_back(get_producer_info(p));
         // cout << "producer pay info: " << get_payment_info(p) << endl << endl;
      }

      std::sort(producer_infos.begin(), producer_infos.end(), comparator);
      BOOST_REQUIRE_EQUAL(get_balance(N(eosio.bpay)), initial_bpay_balance + to_bpay);

      for(const fc::variant &prod_info : producer_infos) {
         BOOST_REQUIRE(!prod_info.is_null());
      }

      uint32_t sharecount = 0;
      for (const auto &prod : producer_infos)
      {
         if (prod["is_active"].as<bool>()) {
               if (sharecount <= 42) {
                  sharecount += 2;
               } else if (sharecount >= 43 && sharecount < 72) {
                  sharecount++;
               } else
                  break;
         }
      }
      
      auto shareValue = to_producers / sharecount;

      int producer_count = 0;
      for(const auto &prod : producer_infos) {
         // cout << producer_count << endl;
         // cout << "producer_info: " << prod << endl;
         if(producer_count < 51) {
            BOOST_REQUIRE_EQUAL(0, prod["unpaid_blocks"].as<uint32_t>());
            const asset balance = get_balance(prod["owner"].as<name>());
            const fc::variant payout_info = get_payment_info(prod["owner"].as<name>());
            BOOST_REQUIRE(!payout_info.is_null());
            const asset payment = payout_info["pay"].as<asset>();

            BOOST_REQUIRE_EQUAL(payment, asset(shareValue * ((producer_count < 21) ? 2 : 1), symbol{CORE_SYM}));
            push_action(prod["owner"].as<name>(), N(claimrewards), mvo()("owner", prod["owner"].as<name>()));
            BOOST_REQUIRE_EQUAL(get_balance(prod["owner"].as<name>()), balance + payment);
            BOOST_REQUIRE_EQUAL(claim_time, microseconds_since_epoch_of_iso_string( prod["last_claim_time"] ));
         } else {
            BOOST_REQUIRE_EQUAL(0, prod["unpaid_blocks"].as<uint32_t>());
            const asset balance = get_balance(prod["owner"].as<name>());
            BOOST_REQUIRE(get_payment_info(prod["owner"].as<name>()).is_null());
            BOOST_REQUIRE_EQUAL(wasm_assert_msg("No payment exists for account"),
                          push_action(prod["owner"].as<name>(), N(claimrewards), mvo()("owner", prod["owner"].as<name>())));
            BOOST_REQUIRE_EQUAL(get_balance(prod["owner"].as<name>()), balance);
         }
         producer_count++;
      }

      BOOST_REQUIRE_EQUAL(0, tot_unpaid_blocks);
      
      BOOST_REQUIRE_EQUAL(get_balance(N(eosio.saving)), initial_wps_balance + to_wps);
      const asset supply  = get_token_supply();
      BOOST_REQUIRE_EQUAL(supply, initial_supply + new_tokens);
   }
} FC_LOG_AND_RETHROW()

//NOTE: This test is failing with a boost::interprocess::bad_alloc exception with the telos version of the system contract.
//      This might be due to the fact that the telos system contract is 70Kb larger than the eosio version.0
BOOST_FIXTURE_TEST_CASE(producers_upgrade_system_contract, eosio_system_tester) try {
   //install multisig contract
   abi_serializer msig_abi_ser = initialize_multisig();
   auto producer_names = active_and_vote_producers();

   //helper function
   auto push_action_msig = [&]( const account_name& signer, const action_name &name, const variant_object &data, bool auth = true ) -> action_result {
         string action_type_name = msig_abi_ser.get_action_type(name);

         action act;
         act.account = N(eosio.msig);
         act.name = name;
         act.data = msig_abi_ser.variant_to_binary( action_type_name, data, abi_serializer_max_time );

         return base_tester::push_action( std::move(act), auth ? uint64_t(signer) : signer == N(bob111111111) ? N(alice1111111) : N(bob111111111) );
   };
   // test begins
   vector<permission_level> prod_perms;
   for ( auto& x : producer_names ) {
      prod_perms.push_back( { name(x), config::active_name } );
   }

   transaction trx;
   {
      //prepare system contract with different hash (contract differs in one byte)
      auto code = contracts::system_wasm();
      string msg = "producer votes must be unique and sorted";
      auto it = std::search( code.begin(), code.end(), msg.begin(), msg.end() );
      BOOST_REQUIRE( it != code.end() );
      msg[0] = 'P';
      std::copy( msg.begin(), msg.end(), it );

      fc::variant pretty_trx = fc::mutable_variant_object()
         ("expiration", "2020-01-01T00:30")
         ("ref_block_num", 2)
         ("ref_block_prefix", 3)
         ("net_usage_words", 0)
         ("max_cpu_usage_ms", 0)
         ("delay_sec", 0)
         ("actions", fc::variants({
               fc::mutable_variant_object()
                  ("account", name(config::system_account_name))
                  ("name", "setcode")
                  ("authorization", vector<permission_level>{ { config::system_account_name, config::active_name } })
                  ("data", fc::mutable_variant_object() ("account", name(config::system_account_name))
                   ("vmtype", 0)
                   ("vmversion", "0")
                   ("code", bytes( code.begin(), code.end() ))
                  )
                  })
         );
      abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer_max_time);
   }
   {
      BOOST_REQUIRE_EQUAL(success(), push_action_msig( N(alice1111111), N(propose), mvo()
                                                      ("proposer",      "alice1111111")
                                                      ("proposal_name", "upgrade1")
                                                      ("trx",           trx)
                                                      ("requested", prod_perms)
                        )
      );
   }

   // get 15 approvals
   {
      for ( size_t i = 0; i < 14; ++i ) {
         BOOST_REQUIRE_EQUAL(success(), push_action_msig( name(producer_names[i]), N(approve), mvo()
                                                         ("proposer",      "alice1111111")
                                                         ("proposal_name", "upgrade1")
                                                         ("level",         permission_level{ name(producer_names[i]), config::active_name })
                           )
         );
      }
   }

   //should fail
   {
      BOOST_REQUIRE_EQUAL(wasm_assert_msg("transaction authorization failed"),
                        push_action_msig( N(alice1111111), N(exec), mvo()
                                          ("proposer",      "alice1111111")
                                          ("proposal_name", "upgrade1")
                                          ("executer",      "alice1111111")
                        )
      );
   }

   // one more approval
   {
      BOOST_REQUIRE_EQUAL(success(), push_action_msig( name(producer_names[14]), N(approve), mvo()
                                 ("proposer",      "alice1111111")
                                 ("proposal_name", "upgrade1")
                                 ("level",         permission_level{ name(producer_names[14]), config::active_name })
                          )
      );
   }

   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );

   BOOST_REQUIRE_EQUAL(success(), push_action_msig( N(alice1111111), N(exec), mvo()
                                                    ("proposer",      "alice1111111")
                                                    ("proposal_name", "upgrade1")
                                                    ("executer",      "alice1111111")
                       )
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );

   produce_blocks( 250 );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE(producer_onblock_check, eosio_system_tester) try {

   // min_activated_stake_part = 28'570'987'0000;
   const asset half_min_activated_stake = core_sym::from_string("14285493.5000");
   const asset quarter_min_activated_stake = core_sym::from_string("7142746.7500");
   const asset eight_min_activated_stake = core_sym::from_string("3571373.3750");

   const asset large_asset = core_sym::from_string("80.0000");
   create_account_with_resources( N(producvotera), config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( N(producvoterb), config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( N(producvoterc), config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );

   // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
   std::vector<account_name> producer_names;
   producer_names.reserve('z' - 'a' + 1);
   const std::string root("defproducer");
   for ( char c = 'a'; c <= 'z'; ++c ) {
      producer_names.emplace_back(root + std::string(1, c));
   }
   setup_producer_accounts(producer_names);

   for (auto a:producer_names)
      regproducer(a);

   BOOST_REQUIRE_EQUAL(0, get_producer_info( producer_names.front() )["total_votes"].as<double>());
   BOOST_REQUIRE_EQUAL(0, get_producer_info( producer_names.back() )["total_votes"].as<double>());

   transfer(config::system_account_name, "producvotera", half_min_activated_stake, config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvotera", quarter_min_activated_stake, quarter_min_activated_stake ));
   BOOST_REQUIRE_EQUAL(success(), vote( N(producvotera), vector<account_name>(producer_names.begin(), producer_names.begin()+10)));

   BOOST_CHECK_EQUAL( wasm_assert_msg( "cannot undelegate bandwidth until the chain is activated (1,000,000 blocks produced)" ),
                      unstake( "producvotera", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );

   // give a chance for everyone to produce blocks
   {
      produce_blocks(21 * 12);

      bool all_21_produced = true;
      for (uint32_t i = 0; i < 21; ++i) {
         if (0 == get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            all_21_produced= false;
         }
      }
      bool rest_didnt_produce = true;
      for (uint32_t i = 21; i < producer_names.size(); ++i) {
         if (0 < get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE_EQUAL(false, all_21_produced);
      BOOST_REQUIRE_EQUAL(true, rest_didnt_produce);
   }

   {
      const char* claimrewards_activation_error_message = "cannot claim rewards until the chain is activated (1,000,000 blocks produced)";
      BOOST_CHECK_EQUAL(0, get_global_state()["total_unpaid_blocks"].as<uint32_t>());
      BOOST_REQUIRE_EQUAL(wasm_assert_msg( claimrewards_activation_error_message ),
                          push_action(producer_names.front(), N(claimrewards), mvo()("owner", producer_names.front())));
      BOOST_REQUIRE_EQUAL(0, get_balance(producer_names.front()).get_amount());
      BOOST_REQUIRE_EQUAL(wasm_assert_msg( claimrewards_activation_error_message ),
                          push_action(producer_names.back(), N(claimrewards), mvo()("owner", producer_names.back())));
      BOOST_REQUIRE_EQUAL(0, get_balance(producer_names.back()).get_amount());
   }


   // stake across 15% boundary
   transfer(config::system_account_name, "producvoterb", half_min_activated_stake, config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvoterb", quarter_min_activated_stake, quarter_min_activated_stake));
   transfer(config::system_account_name, "producvoterc", quarter_min_activated_stake, config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvoterc", eight_min_activated_stake, eight_min_activated_stake));

   // 21 => no rotation , 22 => yep, rotation
   BOOST_REQUIRE_EQUAL(success(), vote( N(producvoterb), vector<account_name>(producer_names.begin(), producer_names.begin()+22)));
   BOOST_REQUIRE_EQUAL(success(), vote( N(producvoterc), vector<account_name>(producer_names.begin(), producer_names.end())));

   activate_network();

   // give a chance for everyone to produce blocks
   {
      produce_blocks(21 * 12);
      for (uint32_t i = 0; i < producer_names.size(); ++i) {
         std::cout<<"["<<producer_names[i]<<"]: "<<get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()<<std::endl;
      }

      bool all_21_produced = 0 < get_producer_info(producer_names[21])["unpaid_blocks"].as<uint32_t>();
      for (uint32_t i = 1; i < 21; ++i) {
         if (0 == get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            all_21_produced= false;
         }
      }
      bool rest_didnt_produce = 0 == get_producer_info(producer_names[0])["unpaid_blocks"].as<uint32_t>();
      for (uint32_t i = 22; i < producer_names.size(); ++i) {
         if (0 < get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE_EQUAL(true, all_21_produced);
      BOOST_REQUIRE_EQUAL(true, rest_didnt_produce);


      const char* claimrewards_payment_missing = "No payment exists for account";
      BOOST_REQUIRE_EQUAL(wasm_assert_msg( claimrewards_payment_missing ),
                          push_action(producer_names.front(), N(claimrewards), mvo()("owner", producer_names.front())));

      //give time for snapshot
      produce_block(fc::minutes(30));
      produce_blocks(1);

      BOOST_REQUIRE_EQUAL(success(),
                          push_action(producer_names[1], N(claimrewards), mvo()("owner", producer_names[1])));
      BOOST_REQUIRE(0 < get_balance(producer_names[1]).get_amount());
   }

   BOOST_CHECK_EQUAL( success(), unstake( "producvotera", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( voter_and_proxy_actions_affect_producers_and_totals, eosio_system_tester, * boost::unit_test::tolerance(1e-4) ) try {
   create_accounts_with_resources( { N(donald111111), N(defproducer1), N(defproducer2), N(defproducer3) } );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer1", 1) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer2", 2) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer3", 3) );

   //alice1111111 becomes a producer
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproxy), mvo()
                                                ("proxy",  "alice1111111")
                                                ("isproxy", true)
                        )
   );

   //bob111111111 becomes a producer
   BOOST_REQUIRE_EQUAL( success(), push_action( N(bob111111111), N(regproxy), mvo()
                                                ("proxy",  "bob111111111")
                                                ("isproxy", true)
                        )
   );

   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" ), get_voter_info( "alice1111111" ) );
   REQUIRE_MATCHING_OBJECT( proxy( "bob111111111" ), get_voter_info( "bob111111111" ) );

   //alice1111111 makes stake and votes
   // issue( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   issue_and_transfer( name("alice1111111"), asset(10000000, symbol{CORE_SYM}));
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("30.0001"), core_sym::from_string("20.0001") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(alice1111111), { N(defproducer1), N(defproducer2) } ) );
   auto expected_value = stake2votes(core_sym::from_string("50.0002"),2,3);
   BOOST_TEST_REQUIRE( expected_value == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( expected_value == get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //donald111111 makes bob his proxy
   issue_and_transfer( name("donald111111"), asset(10000000, symbol{CORE_SYM}));
   BOOST_REQUIRE_EQUAL( success(), stake( "donald111111", core_sym::from_string("5.0000"), core_sym::from_string("5.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(donald111111), vector<account_name>(), "bob111111111" ) );

   //bob111111111 bob stakes and votes
   issue_and_transfer( name("bob111111111"), asset(10000000, symbol{CORE_SYM}));
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("20.0001"), core_sym::from_string("20.0001") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(bob111111111), { N(defproducer1), N(defproducer2) } ) );
   expected_value = stake2votes(core_sym::from_string("50.0002"),2,3) * 2;
   BOOST_TEST_REQUIRE( expected_value == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( expected_value == get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer3" )["total_votes"].as_double() );

   auto initial_activated_stake = get_global_state()["total_activated_stake"].as_int64();

   //donald111111 switches to alice as his proxy
   BOOST_REQUIRE_EQUAL( success(), vote( N(donald111111), vector<account_name>(), "alice1111111" ) );
   BOOST_TEST_REQUIRE( expected_value == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( expected_value == get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer3" )["total_votes"].as_double() );

   expected_value = expected_value * 2; // 2 producers with same weight
   BOOST_TEST_REQUIRE(get_global_state()["total_producer_vote_weight"].as_double() == expected_value);
   // switching between proxies should not increase the total activated stake forever
   BOOST_TEST_REQUIRE(get_global_state()["total_activated_stake"].as_int64() == initial_activated_stake);

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( voters_actions_affect_proxy_and_producers, eosio_system_tester, * boost::unit_test::tolerance(1e-4) ) try {
   activate_network();

   create_accounts_with_resources( { N(donald111111), N(defproducer1), N(defproducer2), N(defproducer3) } );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer1", 1) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer2", 2) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer3", 3) );

   //alice1111111 becomes a producer
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproxy), mvo()
                                                ("proxy",  "alice1111111")
                                                ("isproxy", true)
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" ), get_voter_info( "alice1111111" ) );

   //alice1111111 makes stake and votes
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("30.0001"), core_sym::from_string("20.0001") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(alice1111111), { N(defproducer1), N(defproducer2) } ) );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("50.0002"),2,3) == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("50.0002"),2,3) == get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer3" )["total_votes"].as_double() );

   BOOST_REQUIRE_EQUAL( success(), push_action( N(donald111111), N(regproxy), mvo()
                                                ("proxy",  "donald111111")
                                                ("isproxy", true)
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "donald111111" ), get_voter_info( "donald111111" ) );

   //bob111111111 chooses alice1111111 as a proxy
   issue_and_transfer( "bob111111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("100.0002"), core_sym::from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(bob111111111), vector<account_name>(), "alice1111111" ) );
   BOOST_TEST_REQUIRE( 1500003 == get_voter_info( "alice1111111" )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("200.0005"),2,3) == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("200.0005"),2,3) == get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //carol1111111 chooses alice1111111 as a proxy
   issue_and_transfer( "carol1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "carol1111111", core_sym::from_string("30.0001"), core_sym::from_string("20.0001") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(carol1111111), vector<account_name>(), "alice1111111" ) );
   BOOST_TEST_REQUIRE( 2000005 == get_voter_info( "alice1111111" )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("250.0007"),2,3) == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("250.0007"),2,3) == get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //proxied voter carol1111111 increases stake
   BOOST_REQUIRE_EQUAL( success(), stake( "carol1111111", core_sym::from_string("50.0000"), core_sym::from_string("70.0000") ) );
   BOOST_TEST_REQUIRE( 3200005 == get_voter_info( "alice1111111" )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("370.0007"),2,3) == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("370.0007"),2,3) == get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //proxied voter bob111111111 decreases stake
   BOOST_REQUIRE_EQUAL( success(), unstake( "bob111111111", core_sym::from_string("50.0001"), core_sym::from_string("50.0001") ) );
   BOOST_TEST_REQUIRE( 2200003 == get_voter_info( "alice1111111" )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("270.0005"),2,3) == get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("270.0005"),2,3) == get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //proxied voter carol1111111 chooses another proxy
   BOOST_REQUIRE_EQUAL( success(), vote( N(carol1111111), vector<account_name>(), "donald111111" ) );
   BOOST_TEST_REQUIRE( 500001, get_voter_info( "alice1111111" )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( 1700002, get_voter_info( "donald111111" )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("100.0003"),2,3), get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("100.0003"),2,3), get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, get_producer_info( "defproducer3" )["total_votes"].as_double() );

   //bob111111111 switches to direct voting and votes for one of the same producers, but not for another one
   BOOST_REQUIRE_EQUAL( success(), vote( N(bob111111111), { N(defproducer2) } ) );
   BOOST_TEST_REQUIRE( 0.0 == get_voter_info( "alice1111111" )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("50.0002"),2,3), get_producer_info( "defproducer1" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("100.0003"),1,3), get_producer_info( "defproducer2" )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( 0.0 == get_producer_info( "defproducer3" )["total_votes"].as_double() );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( vote_both_proxy_and_producers, eosio_system_tester ) try {
   //alice1111111 becomes a proxy
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproxy), mvo()
                                                ("proxy",  "alice1111111")
                                                ("isproxy", true)
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" ), get_voter_info( "alice1111111" ) );

   //carol1111111 becomes a producer
   BOOST_REQUIRE_EQUAL( success(), regproducer( "carol1111111", 1) );

   //bob111111111 chooses alice1111111 as a proxy

   issue_and_transfer( "bob111111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("100.0002"), core_sym::from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("cannot vote for producers and proxy at same time"),
                        vote( N(bob111111111), { N(carol1111111) }, "alice1111111" ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( select_invalid_proxy, eosio_system_tester ) try {
   //accumulate proxied votes
   issue_and_transfer( "bob111111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("100.0002"), core_sym::from_string("50.0001") ) );

   //selecting account not registered as a proxy
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "invalid proxy specified" ),
                        vote( N(bob111111111), vector<account_name>(), "alice1111111" ) );

   //selecting not existing account as a proxy
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "invalid proxy specified" ),
                        vote( N(bob111111111), vector<account_name>(), "notexist" ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( double_register_unregister_proxy_keeps_votes, eosio_system_tester ) try {
   //alice1111111 becomes a proxy
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproxy), mvo()
                                                ("proxy",  "alice1111111")
                                                ("isproxy",  1)
                        )
   );
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("5.0000"), core_sym::from_string("5.0000") ) );
   edump((get_voter_info("alice1111111")));
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" )( "staked", 100000 ), get_voter_info( "alice1111111" ) );

   //bob111111111 stakes and selects alice1111111 as a proxy
   issue_and_transfer( "bob111111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("100.0002"), core_sym::from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(bob111111111), vector<account_name>(), "alice1111111" ) );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" )( "proxied_vote_weight", 1500003 )( "staked", 100000 ), get_voter_info( "alice1111111" ) );

   //double regestering should fail without affecting total votes and stake
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "action has no effect" ),
                        push_action( N(alice1111111), N(regproxy), mvo()
                                     ("proxy",  "alice1111111")
                                     ("isproxy",  1)
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111" )( "proxied_vote_weight", 1500003 )( "staked", 100000 ), get_voter_info( "alice1111111" ) );

   //uregister
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproxy), mvo()
                                                ("proxy",  "alice1111111")
                                                ("isproxy",  0)
                        )
   );
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111" )( "proxied_vote_weight", 1500003 )( "staked", 100000 ), get_voter_info( "alice1111111" ) );

   //double unregistering should not affect proxied_votes and stake
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "action has no effect" ),
                        push_action( N(alice1111111), N(regproxy), mvo()
                                     ("proxy",  "alice1111111")
                                     ("isproxy",  0)
                        )
   );
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111" )( "proxied_vote_weight", 1500003 )( "staked", 100000 ), get_voter_info( "alice1111111" ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( proxy_cannot_use_another_proxy, eosio_system_tester ) try {
   //alice1111111 becomes a proxy
   BOOST_REQUIRE_EQUAL( success(), push_action( N(alice1111111), N(regproxy), mvo()
                                                ("proxy",  "alice1111111")
                                                ("isproxy",  1)
                        )
   );

   //bob111111111 becomes a proxy
   BOOST_REQUIRE_EQUAL( success(), push_action( N(bob111111111), N(regproxy), mvo()
                                                ("proxy",  "bob111111111")
                                                ("isproxy",  1)
                        )
   );

   //proxy should not be able to use a proxy
   issue_and_transfer( "bob111111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("100.0002"), core_sym::from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "account registered as a proxy is not allowed to use a proxy" ),
                        vote( N(bob111111111), vector<account_name>(), "alice1111111" ) );

   //voter that uses a proxy should not be allowed to become a proxy
   issue_and_transfer( "carol1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "carol1111111", core_sym::from_string("100.0002"), core_sym::from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(carol1111111), vector<account_name>(), "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "account that uses a proxy is not allowed to become a proxy" ),
                        push_action( N(carol1111111), N(regproxy), mvo()
                                     ("proxy",  "carol1111111")
                                     ("isproxy",  1)
                        )
   );

   //proxy should not be able to use itself as a proxy
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "cannot proxy to self" ),
                        vote( N(bob111111111), vector<account_name>(), "bob111111111" ) );

} FC_LOG_AND_RETHROW()

fc::mutable_variant_object config_to_variant( const eosio::chain::chain_config& config ) {
   return mutable_variant_object()
      ( "max_block_net_usage", config.max_block_net_usage )
      ( "target_block_net_usage_pct", config.target_block_net_usage_pct )
      ( "max_transaction_net_usage", config.max_transaction_net_usage )
      ( "base_per_transaction_net_usage", config.base_per_transaction_net_usage )
      ( "context_free_discount_net_usage_num", config.context_free_discount_net_usage_num )
      ( "context_free_discount_net_usage_den", config.context_free_discount_net_usage_den )
      ( "max_block_cpu_usage", config.max_block_cpu_usage )
      ( "target_block_cpu_usage_pct", config.target_block_cpu_usage_pct )
      ( "max_transaction_cpu_usage", config.max_transaction_cpu_usage )
      ( "min_transaction_cpu_usage", config.min_transaction_cpu_usage )
      ( "max_transaction_lifetime", config.max_transaction_lifetime )
      ( "deferred_trx_expiration_window", config.deferred_trx_expiration_window )
      ( "max_transaction_delay", config.max_transaction_delay )
      ( "max_inline_action_size", config.max_inline_action_size )
      ( "max_inline_action_depth", config.max_inline_action_depth )
      ( "max_authority_depth", config.max_authority_depth );
}

BOOST_FIXTURE_TEST_CASE( elect_producers /*_and_parameters*/, eosio_system_tester ) try {
   create_accounts_with_resources( {  N(defproducer1), N(defproducer2), N(defproducer3) } );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer1", 1) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer2", 2) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer3", 3) );

   //stake more than 15% of total EOS supply to activate chain
   transfer( "eosio", "alice1111111", core_sym::from_string("600000000.0000"), "eosio" );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("300000000.0000"), core_sym::from_string("300000000.0000") ) );
   //vote for producers
   BOOST_REQUIRE_EQUAL( success(), vote( N(alice1111111), { N(defproducer1) } ) );

   activate_network();

   produce_blocks(250);
   auto producer_keys = control->head_block_state()->active_schedule.producers;
   BOOST_REQUIRE_EQUAL( 1, producer_keys.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"), producer_keys[0].producer_name );

   //auto config = config_to_variant( control->get_global_properties().configuration );
   //auto prod1_config = testing::filter_fields( config, producer_parameters_example( 1 ) );
   //REQUIRE_EQUAL_OBJECTS(prod1_config, config);

   // elect 2 producers
   issue_and_transfer( "bob111111111", core_sym::from_string("80000.0000"),  config::system_account_name );
   ilog("stake");
   BOOST_REQUIRE_EQUAL( success(), stake( "bob111111111", core_sym::from_string("40000.0000"), core_sym::from_string("40000.0000") ) );
   ilog("start vote");
   BOOST_REQUIRE_EQUAL( success(), vote( N(bob111111111), { N(defproducer2) } ) );
   ilog(".");
   produce_blocks(250);
   producer_keys = control->head_block_state()->active_schedule.producers;
   BOOST_REQUIRE_EQUAL( 2, producer_keys.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"), producer_keys[0].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer2"), producer_keys[1].producer_name );
   //config = config_to_variant( control->get_global_properties().configuration );
   //auto prod2_config = testing::filter_fields( config, producer_parameters_example( 2 ) );
   //REQUIRE_EQUAL_OBJECTS(prod2_config, config);

   // elect 3 producers
   BOOST_REQUIRE_EQUAL( success(), vote( N(bob111111111), { N(defproducer2), N(defproducer3) } ) );
   produce_blocks(250);
   producer_keys = control->head_block_state()->active_schedule.producers;
   BOOST_REQUIRE_EQUAL( 3, producer_keys.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"), producer_keys[0].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer2"), producer_keys[1].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer3"), producer_keys[2].producer_name );
   //config = config_to_variant( control->get_global_properties().configuration );
   //REQUIRE_EQUAL_OBJECTS(prod2_config, config);

   // try to go back to 2 producers and fail
   BOOST_REQUIRE_EQUAL( success(), vote( N(bob111111111), { N(defproducer3) } ) );
   produce_blocks(250);
   producer_keys = control->head_block_state()->active_schedule.producers;
   /*
   wdump((producer_keys));
   BOOST_REQUIRE_EQUAL( 3, producer_keys.size() );
   */

   // The test below is invalid now, producer schedule is not updated if there are
   // fewer producers in the new schedule
   BOOST_REQUIRE_EQUAL( 2, producer_keys.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"), producer_keys[0].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer3"), producer_keys[1].producer_name );
   /*
   //config = config_to_variant( control->get_global_properties().configuration );
   //auto prod3_config = testing::filter_fields( config, producer_parameters_example( 3 ) );
   //REQUIRE_EQUAL_OBJECTS(prod3_config, config);
   */

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( buyname, eosio_system_tester ) try {
   create_accounts_with_resources( { N(dan), N(sam) } );
   transfer( config::system_account_name, "dan", core_sym::from_string( "10000.0000" ) );
   transfer( config::system_account_name, "sam", core_sym::from_string( "10000.0000" ) );
   stake_with_transfer( config::system_account_name, "sam", core_sym::from_string( "80000000.0000" ), core_sym::from_string( "80000000.0000" ) );
   stake_with_transfer( config::system_account_name, "dan", core_sym::from_string( "80000000.0000" ), core_sym::from_string( "80000000.0000" ) );

   regproducer( config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), vote( N(sam), { config::system_account_name } ) );

   activate_network();
   // wait 14 days after min required amount has been staked
   produce_block( fc::days(7) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(dan), { config::system_account_name } ) );
   produce_block( fc::days(7) );
   produce_block();

   BOOST_REQUIRE_EXCEPTION( create_accounts_with_resources( { N(fail) }, N(dan) ), // dan shouldn't be able to create fail
                            eosio_assert_message_exception, eosio_assert_message_is( "no active bid for name" ) );
   bidname( "dan", "nofail", core_sym::from_string( "1.0000" ) );
   BOOST_REQUIRE_EQUAL( "assertion failure with message: must increase bid by 10%", bidname( "sam", "nofail", core_sym::from_string( "1.0000" ) )); // didn't increase bid by 10%
   BOOST_REQUIRE_EQUAL( success(), bidname( "sam", "nofail", core_sym::from_string( "2.0000" ) )); // didn't increase bid by 10%
   produce_block( fc::days(1) );
   produce_block();

   BOOST_REQUIRE_EXCEPTION( create_accounts_with_resources( { N(nofail) }, N(dan) ), // dan shoudn't be able to do this, sam won
                            eosio_assert_message_exception, eosio_assert_message_is( "only highest bidder can claim" ) );
   //wlog( "verify sam can create nofail" );
   create_accounts_with_resources( { N(nofail) }, N(sam) ); // sam should be able to do this, he won the bid
   //wlog( "verify nofail can create test.nofail" );
   transfer( "eosio", "nofail", core_sym::from_string( "1000.0000" ) );
   create_accounts_with_resources( { N(test.nofail) }, N(nofail) ); // only nofail can create test.nofail
   //wlog( "verify dan cannot create test.fail" );
   BOOST_REQUIRE_EXCEPTION( create_accounts_with_resources( { N(test.fail) }, N(dan) ), // dan shouldn't be able to do this
                            eosio_assert_message_exception, eosio_assert_message_is( "only suffix may create this account" ) );

   create_accounts_with_resources( { N(goodgoodgood) }, N(dan) ); /// 12 char names should succeed
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( bid_invalid_names, eosio_system_tester ) try {
   create_accounts_with_resources( { N(dan) } );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "you can only bid on top-level suffix" ),
                        bidname( "dan", "abcdefg.12345", core_sym::from_string( "1.0000" ) ) );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "the empty name is not a valid account name to bid on" ),
                        bidname( "dan", "", core_sym::from_string( "1.0000" ) ) );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "13 character names are not valid account names to bid on" ),
                        bidname( "dan", "abcdefgh12345", core_sym::from_string( "1.0000" ) ) );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "accounts with 12 character names and no dots can be created without bidding required" ),
                        bidname( "dan", "abcdefg12345", core_sym::from_string( "1.0000" ) ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( multiple_namebids, eosio_system_tester ) try {

   const std::string not_closed_message("auction for name is not closed yet");

   std::vector<account_name> accounts = { N(alice), N(bob), N(carl), N(david), N(eve) };
   create_accounts_with_resources( accounts );
   for ( const auto& a: accounts ) {
      transfer( config::system_account_name, a, core_sym::from_string( "10000.0000" ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "10000.0000" ), get_balance(a) );
   }
   create_accounts_with_resources( { N(producer) } );
   BOOST_REQUIRE_EQUAL( success(), regproducer( N(producer) ) );

   produce_block();
   // stake but but not enough to go live
   stake_with_transfer( config::system_account_name, "bob",  core_sym::from_string( "3500000.0000" ), core_sym::from_string( "3500000.0000" ) );
   stake_with_transfer( config::system_account_name, "carl", core_sym::from_string( "3500000.0000" ), core_sym::from_string( "3500000.0000" ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(bob), { N(producer) } ) );
   BOOST_REQUIRE_EQUAL( success(), vote( N(carl), { N(producer) } ) );

   // start bids
   bidname( "bob",  "prefa", core_sym::from_string("1.0003") );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "9998.9997" ), get_balance("bob") );
   bidname( "bob",  "prefb", core_sym::from_string("1.0000") );
   bidname( "bob",  "prefc", core_sym::from_string("1.0000") );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "9996.9997" ), get_balance("bob") );

   bidname( "carl", "prefd", core_sym::from_string("1.0000") );
   bidname( "carl", "prefe", core_sym::from_string("1.0000") );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "9998.0000" ), get_balance("carl") );

   BOOST_REQUIRE_EQUAL( error("assertion failure with message: account is already highest bidder"),
                        bidname( "bob", "prefb", core_sym::from_string("1.1001") ) );
   BOOST_REQUIRE_EQUAL( error("assertion failure with message: must increase bid by 10%"),
                        bidname( "alice", "prefb", core_sym::from_string("1.0999") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "9996.9997" ), get_balance("bob") );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "10000.0000" ), get_balance("alice") );

   // alice outbids bob on prefb
   {
      const asset initial_names_balance = get_balance(N(eosio.names));
      BOOST_REQUIRE_EQUAL( success(),
                           bidname( "alice", "prefb", core_sym::from_string("1.1001") ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "9997.9997" ), get_balance("bob") );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "9998.8999" ), get_balance("alice") );
      BOOST_REQUIRE_EQUAL( initial_names_balance + core_sym::from_string("0.1001"), get_balance(N(eosio.names)) );
   }

   // david outbids carl on prefd
   {
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "9998.0000" ), get_balance("carl") );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "10000.0000" ), get_balance("david") );
      BOOST_REQUIRE_EQUAL( success(),
                           bidname( "david", "prefd", core_sym::from_string("1.9900") ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "9999.0000" ), get_balance("carl") );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "9998.0100" ), get_balance("david") );
   }

   // eve outbids carl on prefe
   {
      BOOST_REQUIRE_EQUAL( success(),
                           bidname( "eve", "prefe", core_sym::from_string("1.7200") ) );
   }

   produce_block( fc::days(14) );
   produce_block();

   // highest bid is from david for prefd but no bids can be closed yet
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefd), N(david) ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );

   // stake enough to go above the 15% threshold ~ total of ~~30M = 16 + 14 above (minstake to activate ~29M)
   stake_with_transfer( config::system_account_name, "alice", core_sym::from_string( "8000000.0000" ), core_sym::from_string( "8000000.0000" ) );
   BOOST_REQUIRE_EQUAL(0, get_producer_info("producer")["unpaid_blocks"].as<uint32_t>());
   BOOST_REQUIRE_EQUAL( success(), vote( N(alice), { N(producer) } ) );

   activate_network();

   // need to wait for 14 days after going live
   produce_blocks(10);
   produce_block( fc::days(2) );
   produce_blocks( 10 );
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefd), N(david) ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   // it's been 14 days, auction for prefd has been closed
   produce_block( fc::days(12) );
   create_account_with_resources( N(prefd), N(david) );
   produce_blocks(2);
   produce_block( fc::hours(23) );
   // auctions for prefa, prefb, prefc, prefe haven't been closed
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefa), N(bob) ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefb), N(alice) ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefc), N(bob) ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefe), N(eve) ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   // attemp to create account with no bid
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefg), N(alice) ),
                            fc::exception, fc_assert_exception_message_is( "no active bid for name" ) );
   // changing highest bid pushes auction closing time by 24 hours
   BOOST_REQUIRE_EQUAL( success(),
                        bidname( "eve",  "prefb", core_sym::from_string("2.1880") ) );

   produce_block( fc::hours(22) );
   produce_blocks(2);

   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefb), N(eve) ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   // but changing a bid that is not the highest does not push closing time
   BOOST_REQUIRE_EQUAL( success(),
                        bidname( "carl", "prefe", core_sym::from_string("2.0980") ) );
   produce_block( fc::hours(2) );
   produce_blocks(2);
   // bid for prefb has closed, only highest bidder can claim
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefb), N(alice) ),
                            eosio_assert_message_exception, eosio_assert_message_is( "only highest bidder can claim" ) );
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefb), N(carl) ),
                            eosio_assert_message_exception, eosio_assert_message_is( "only highest bidder can claim" ) );
   create_account_with_resources( N(prefb), N(eve) );

   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefe), N(carl) ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   produce_block();
   produce_block( fc::hours(24) );
   // by now bid for prefe has closed
   create_account_with_resources( N(prefe), N(carl) );
   // prefe can now create *.prefe
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(xyz.prefe), N(carl) ),
                            fc::exception, fc_assert_exception_message_is("only suffix may create this account") );
   transfer( config::system_account_name, N(prefe), core_sym::from_string("10000.0000") );
   create_account_with_resources( N(xyz.prefe), N(prefe) );

   // other auctions haven't closed
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(prefa), N(bob) ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );

} FC_LOG_AND_RETHROW()

// BOOST_FIXTURE_TEST_CASE( namebid_pending_winner, eosio_system_tester ) try {
//    activate_network();
//    produce_block( fc::hours(14*24) );    //wait 14 day for name auction activation
//    transfer( config::system_account_name, N(alice1111111), core_sym::from_string("10000.0000") );
//    transfer( config::system_account_name, N(bob111111111), core_sym::from_string("10000.0000") );

//    BOOST_REQUIRE_EQUAL( success(), bidname( "alice1111111", "prefa", core_sym::from_string( "50.0000" ) ));
//    BOOST_REQUIRE_EQUAL( success(), bidname( "bob111111111", "prefb", core_sym::from_string( "30.0000" ) ));
//    produce_block( fc::hours(100) ); //should close "perfa"
//    produce_block( fc::hours(100) ); //should close "perfb"

//    //despite "perfa" account hasn't been created, we should be able to create "perfb" account
//    create_account_with_resources( N(prefb), N(bob111111111) );
// } FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( vote_producers_in_and_out, eosio_system_tester ) try {
   activate_network();
   const asset net = core_sym::from_string("80.0000");
   const asset cpu = core_sym::from_string("80.0000");
   std::vector<account_name> voters = { N(producvotera), N(producvoterb), N(producvoterc), N(producvoterd) };
   for (const auto& v: voters) {
      create_account_with_resources(v, config::system_account_name, core_sym::from_string("1.0000"), false, net, cpu);
   }

   // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
   std::vector<account_name> producer_names;
   {
      producer_names.reserve('z' - 'a' + 1);
      const std::string root("defproducer");
      for ( char c = 'a'; c <= 'z'; ++c ) {
         producer_names.emplace_back(root + std::string(1, c));
      }
      setup_producer_accounts(producer_names);
      for (const auto& p: producer_names) {
         BOOST_REQUIRE_EQUAL( success(), regproducer(p) );
         produce_blocks(1);
         ilog( "------ get pro----------" );
         wdump((p));
         BOOST_TEST(0 == get_producer_info(p)["total_votes"].as<double>());
      }
   }

   for (const auto& v: voters) {
      transfer( config::system_account_name, v, core_sym::from_string("200000000.0000"), config::system_account_name );
      BOOST_REQUIRE_EQUAL(success(), stake(v, core_sym::from_string("30000000.0000"), core_sym::from_string("30000000.0000")) );
   }

   {
      BOOST_REQUIRE_EQUAL(success(), vote(N(producvotera), vector<account_name>(producer_names.begin(), producer_names.begin()+20)));
      BOOST_REQUIRE_EQUAL(success(), vote(N(producvoterb), vector<account_name>(producer_names.begin(), producer_names.begin()+21)));
      BOOST_REQUIRE_EQUAL(success(), vote(N(producvoterc), vector<account_name>(producer_names.begin(), producer_names.end())));
   }

   // give a chance for everyone to produce blocks
   {
      produce_blocks(51 * 24 + 20);

      bool all_21_produced = true;
      for (uint32_t i = 0; i < 21; ++i) {
         if (0 == get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            all_21_produced = false;
         }
      }
      bool rest_didnt_produce = true;
      for (uint32_t i = 21; i < producer_names.size(); ++i) {
         if (0 < get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE(all_21_produced && rest_didnt_produce);
   }

   {
      produce_block(fc::hours(3));
      const uint32_t voted_out_index = 20;
      const uint32_t new_prod_index  = 23;
      // the new weight calculation gives bigger weight for more voted producers
      vector<account_name> v = vector<account_name>(producer_names.begin(), producer_names.begin()+(voted_out_index-1));
      v.emplace_back(producer_names[new_prod_index]);
      
      BOOST_REQUIRE_EQUAL(success(), stake("producvoterd", core_sym::from_string("40000000.0000"), core_sym::from_string("40000000.0000")));
      BOOST_REQUIRE_EQUAL(success(), vote(N(producvoterd), v));
      BOOST_REQUIRE_EQUAL(0, get_producer_info(producer_names[new_prod_index])["unpaid_blocks"].as<uint32_t>());
      produce_blocks(4 * 24 * 21);

      BOOST_REQUIRE(0 < get_producer_info(producer_names[new_prod_index])["unpaid_blocks"].as<uint32_t>());
      const uint32_t initial_unpaid_blocks = get_producer_info(producer_names[voted_out_index])["unpaid_blocks"].as<uint32_t>();
      produce_blocks(2 * 24 * 21);
      // BOOST_REQUIRE_EQUAL(initial_unpaid_blocks, get_producer_info(producer_names[voted_out_index])["unpaid_blocks"].as<uint32_t>());
      produce_block(fc::hours(24));
      BOOST_REQUIRE_EQUAL(success(), vote(N(producvoterd), { producer_names[voted_out_index] }));
      produce_blocks(2 * 24 * 21); 
      BOOST_REQUIRE(fc::crypto::public_key() != fc::crypto::public_key(get_producer_info(producer_names[voted_out_index])["producer_key"].as_string()));
      BOOST_REQUIRE_EQUAL(success(), push_action(producer_names[voted_out_index], N(claimrewards), mvo()("owner", producer_names[voted_out_index])));
   }

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( setparams, eosio_system_tester ) try {
   //install multisig contract
   abi_serializer msig_abi_ser = initialize_multisig();
   auto producer_names = active_and_vote_producers();

   //helper function
   auto push_action_msig = [&]( const account_name& signer, const action_name &name, const variant_object &data, bool auth = true ) -> action_result {
         string action_type_name = msig_abi_ser.get_action_type(name);

         action act;
         act.account = N(eosio.msig);
         act.name = name;
         act.data = msig_abi_ser.variant_to_binary( action_type_name, data, abi_serializer_max_time );

         return base_tester::push_action( std::move(act), auth ? uint64_t(signer) : signer == N(bob111111111) ? N(alice1111111) : N(bob111111111) );
   };

   // test begins
   vector<permission_level> prod_perms;
   for ( auto& x : producer_names ) {
      prod_perms.push_back( { name(x), config::active_name } );
   }

   eosio::chain::chain_config params;
   params = control->get_global_properties().configuration;
   //change some values
   params.max_block_net_usage += 10;
   params.max_transaction_lifetime += 1;

   transaction trx;
   {
      fc::variant pretty_trx = fc::mutable_variant_object()
         ("expiration", "2020-01-01T00:30")
         ("ref_block_num", 2)
         ("ref_block_prefix", 3)
         ("net_usage_words", 0)
         ("max_cpu_usage_ms", 0)
         ("delay_sec", 0)
         ("actions", fc::variants({
               fc::mutable_variant_object()
                  ("account", name(config::system_account_name))
                  ("name", "setparams")
                  ("authorization", vector<permission_level>{ { config::system_account_name, config::active_name } })
                  ("data", fc::mutable_variant_object()
                   ("params", params)
                  )
                  })
         );
      abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer_max_time);
   }

   BOOST_REQUIRE_EQUAL(success(), push_action_msig( N(alice1111111), N(propose), mvo()
                                                    ("proposer",      "alice1111111")
                                                    ("proposal_name", "setparams1")
                                                    ("trx",           trx)
                                                    ("requested", prod_perms)
                       )
   );

   // get 16 approvals
   for ( size_t i = 0; i < 15; ++i ) {
      BOOST_REQUIRE_EQUAL(success(), push_action_msig( name(producer_names[i]), N(approve), mvo()
                                                       ("proposer",      "alice1111111")
                                                       ("proposal_name", "setparams1")
                                                       ("level",         permission_level{ name(producer_names[i]), config::active_name })
                          )
      );
   }

   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );

   BOOST_REQUIRE_EQUAL(success(), push_action_msig( N(alice1111111), N(exec), mvo()
                                                    ("proposer",      "alice1111111")
                                                    ("proposal_name", "setparams1")
                                                    ("executer",      "alice1111111")
                       )
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );

   produce_blocks( 250 );

   // make sure that changed parameters were applied
   auto active_params = control->get_global_properties().configuration;
   BOOST_REQUIRE_EQUAL( params.max_block_net_usage, active_params.max_block_net_usage );
   BOOST_REQUIRE_EQUAL( params.max_transaction_lifetime, active_params.max_transaction_lifetime );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( setram_effect, eosio_system_tester ) try {

   const asset net = core_sym::from_string("8.0000");
   const asset cpu = core_sym::from_string("8.0000");
   std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount) };
   for (const auto& a: accounts) {
      create_account_with_resources(a, config::system_account_name, core_sym::from_string("1.0000"), false, net, cpu);
   }

   {
      const auto name_a = accounts[0];
      transfer( config::system_account_name, name_a, core_sym::from_string("1000.0000") );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance(name_a) );
      const uint64_t init_bytes_a = get_total_stake(name_a)["ram_bytes"].as_uint64();
      BOOST_REQUIRE_EQUAL( success(), buyram( name_a, name_a, core_sym::from_string("300.0000") ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance(name_a) );
      const uint64_t bought_bytes_a = get_total_stake(name_a)["ram_bytes"].as_uint64() - init_bytes_a;

      // after buying and selling balance should be 700 + 300 * 0.995 * 0.995 = 997.0075 (actually 997.0074 due to rounding fees up)
      BOOST_REQUIRE_EQUAL( success(), sellram(name_a, bought_bytes_a ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("997.0074"), get_balance(name_a) );
   }

   {
      const auto name_b = accounts[1];
      transfer( config::system_account_name, name_b, core_sym::from_string("1000.0000") );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance(name_b) );
      const uint64_t init_bytes_b = get_total_stake(name_b)["ram_bytes"].as_uint64();
      // name_b buys ram at current price
      BOOST_REQUIRE_EQUAL( success(), buyram( name_b, name_b, core_sym::from_string("300.0000") ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance(name_b) );
      const uint64_t bought_bytes_b = get_total_stake(name_b)["ram_bytes"].as_uint64() - init_bytes_b;

      // increase max_ram_size, ram bought by name_b loses part of its value
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("ram may only be increased"),
                           push_action(config::system_account_name, N(setram), mvo()("max_ram_size", 12ll*1024 * 1024 * 1024)) );
      BOOST_REQUIRE_EQUAL( error("missing authority of eosio"),
                           push_action(name_b, N(setram), mvo()("max_ram_size", 16ll*1024 * 1024 * 1024)) );
      BOOST_REQUIRE_EQUAL( success(),
                           push_action(config::system_account_name, N(setram), mvo()("max_ram_size", 16ll*1024 * 1024 * 1024)) );

      BOOST_REQUIRE_EQUAL( success(), sellram(name_b, bought_bytes_b ) );
      BOOST_REQUIRE( core_sym::from_string("900.0000") < get_balance(name_b) );
      BOOST_REQUIRE( core_sym::from_string("950.0000") > get_balance(name_b) );
   }

} FC_LOG_AND_RETHROW()

// BOOST_FIXTURE_TEST_CASE( ram_inflation, eosio_system_tester ) try {

//    const uint64_t init_max_ram_size = 64ll*1024 * 1024 * 1024;

//    BOOST_REQUIRE_EQUAL( init_max_ram_size, get_global_state()["max_ram_size"].as_uint64() );
//    produce_blocks(20);
//    BOOST_REQUIRE_EQUAL( init_max_ram_size, get_global_state()["max_ram_size"].as_uint64() );
//    transfer( config::system_account_name, "alice1111111", core_sym::from_string("1000.0000"), config::system_account_name );
//    BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
//    produce_blocks(3);
//    BOOST_REQUIRE_EQUAL( init_max_ram_size, get_global_state()["max_ram_size"].as_uint64() );
//    uint16_t rate = 1000;
//    BOOST_REQUIRE_EQUAL( success(), push_action( config::system_account_name, N(setramrate), mvo()("bytes_per_block", rate) ) );
//    BOOST_REQUIRE_EQUAL( rate, get_global_state2()["new_ram_per_block"].as<uint16_t>() );
//    // last time update_ram_supply called is in buyram, num of blocks since then to
//    // the block that includes the setramrate action is 1 + 3 = 4.
//    // However, those 4 blocks were accumulating at a rate of 0, so the max_ram_size should not have changed.
//    BOOST_REQUIRE_EQUAL( init_max_ram_size, get_global_state()["max_ram_size"].as_uint64() );
//    // But with additional blocks, it should start accumulating at the new rate.
//    uint64_t cur_ram_size = get_global_state()["max_ram_size"].as_uint64();
//    produce_blocks(10);
//    BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
//    BOOST_REQUIRE_EQUAL( cur_ram_size + 11 * rate, get_global_state()["max_ram_size"].as_uint64() );
//    cur_ram_size = get_global_state()["max_ram_size"].as_uint64();
//    produce_blocks(5);
//    BOOST_REQUIRE_EQUAL( cur_ram_size, get_global_state()["max_ram_size"].as_uint64() );
//    BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", 100 ) );
//    BOOST_REQUIRE_EQUAL( cur_ram_size + 6 * rate, get_global_state()["max_ram_size"].as_uint64() );
//    cur_ram_size = get_global_state()["max_ram_size"].as_uint64();
//    produce_blocks();
//    BOOST_REQUIRE_EQUAL( success(), buyrambytes( "alice1111111", "alice1111111", 100 ) );
//    BOOST_REQUIRE_EQUAL( cur_ram_size + 2 * rate, get_global_state()["max_ram_size"].as_uint64() );

//    BOOST_REQUIRE_EQUAL( error("missing authority of eosio"),
//                         push_action( "alice1111111", N(setramrate), mvo()("bytes_per_block", rate) ) );

//    cur_ram_size = get_global_state()["max_ram_size"].as_uint64();
//    produce_blocks(10);
//    uint16_t old_rate = rate;
//    rate = 5000;
//    BOOST_REQUIRE_EQUAL( success(), push_action( config::system_account_name, N(setramrate), mvo()("bytes_per_block", rate) ) );
//    BOOST_REQUIRE_EQUAL( cur_ram_size + 11 * old_rate, get_global_state()["max_ram_size"].as_uint64() );
//    produce_blocks(5);
//    BOOST_REQUIRE_EQUAL( success(), buyrambytes( "alice1111111", "alice1111111", 100 ) );
//    BOOST_REQUIRE_EQUAL( cur_ram_size + 11 * old_rate + 6 * rate, get_global_state()["max_ram_size"].as_uint64() );

// } FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( eosioram_ramusage, eosio_system_tester ) try {
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );
   transfer( "eosio", "alice1111111", core_sym::from_string("1000.0000"), "eosio" );
   BOOST_REQUIRE_EQUAL( success(), stake( "eosio", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   const asset initial_ram_balance = get_balance(N(eosio.ram));
   const asset initial_ramfee_balance = get_balance(N(eosio.ramfee));
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("1000.0000") ) );

   BOOST_REQUIRE_EQUAL( false, get_row_by_account( N(eosio.token), N(alice1111111), N(accounts), symbol{CORE_SYM}.to_symbol_code() ).empty() );

   //remove row
   base_tester::push_action( N(eosio.token), N(close), N(alice1111111), mvo()
                             ( "owner", "alice1111111" )
                             ( "symbol", symbol{CORE_SYM} )
   );
   BOOST_REQUIRE_EQUAL( true, get_row_by_account( N(eosio.token), N(alice1111111), N(accounts), symbol{CORE_SYM}.to_symbol_code() ).empty() );

   auto rlm = control->get_resource_limits_manager();
   auto eosioram_ram_usage = rlm.get_account_ram_usage(N(eosio.ram));
   auto alice_ram_usage = rlm.get_account_ram_usage(N(alice1111111));

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", 2048 ) );

   //make sure that ram was billed to alice, not to eosio.ram
   BOOST_REQUIRE_EQUAL( true, alice_ram_usage < rlm.get_account_ram_usage(N(alice1111111)) );
   BOOST_REQUIRE_EQUAL( eosioram_ram_usage, rlm.get_account_ram_usage(N(eosio.ram)) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( ram_gift, eosio_system_tester ) try {
   active_and_vote_producers();

   auto rlm = control->get_resource_limits_manager();
   int64_t ram_bytes_orig, net_weight, cpu_weight;
   rlm.get_account_limits( N(alice1111111), ram_bytes_orig, net_weight, cpu_weight );

   /*
    * It seems impossible to write this test, because buyrambytes action doesn't give you exact amount of bytes requested
    *
   //check that it's possible to create account bying required_bytes(2724) + userres table(112) + userres row(160) - ram_gift_bytes(1400)
   create_account_with_resources( N(abcdefghklmn), N(alice1111111), 2724 + 112 + 160 - 1400 );

   //check that one byte less is not enough
   BOOST_REQUIRE_THROW( create_account_with_resources( N(abcdefghklmn), N(alice1111111), 2724 + 112 + 160 - 1400 - 1 ),
                        ram_usage_exceeded );
   */

   //check that stake/unstake keeps the gift
   transfer( "eosio", "alice1111111", core_sym::from_string("1000.0000"), "eosio" );
   BOOST_REQUIRE_EQUAL( success(), stake( "eosio", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   int64_t ram_bytes_after_stake;
   rlm.get_account_limits( N(alice1111111), ram_bytes_after_stake, net_weight, cpu_weight );
   BOOST_REQUIRE_EQUAL( ram_bytes_orig, ram_bytes_after_stake );

   BOOST_REQUIRE_EQUAL( success(), unstake( "eosio", "alice1111111", core_sym::from_string("20.0000"), core_sym::from_string("10.0000") ) );
   int64_t ram_bytes_after_unstake;
   rlm.get_account_limits( N(alice1111111), ram_bytes_after_unstake, net_weight, cpu_weight );
   BOOST_REQUIRE_EQUAL( ram_bytes_orig, ram_bytes_after_unstake );

   uint64_t ram_gift = 1400;

   int64_t ram_bytes;
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("1000.0000") ) );
   rlm.get_account_limits( N(alice1111111), ram_bytes, net_weight, cpu_weight );
   auto userres = get_total_stake( N(alice1111111) );
   BOOST_REQUIRE_EQUAL( userres["ram_bytes"].as_uint64() + ram_gift, ram_bytes );

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", 1024 ) );
   rlm.get_account_limits( N(alice1111111), ram_bytes, net_weight, cpu_weight );
   userres = get_total_stake( N(alice1111111) );
   BOOST_REQUIRE_EQUAL( userres["ram_bytes"].as_uint64() + ram_gift, ram_bytes );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( rex_auth, eosio_system_tester ) try {

   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount) };
   const account_name alice = accounts[0], bob = accounts[1];
   const asset init_balance = core_sym::from_string("1000.0000");
   const asset one_eos      = core_sym::from_string("1.0000");
   const asset one_rex      = asset::from_string("1.0000 REX");
   setup_rex_accounts( accounts, init_balance );

   const std::string error_msg("missing authority of aliceaccount");
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(deposit), mvo()("owner", alice)("amount", one_eos) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(withdraw), mvo()("owner", alice)("amount", one_eos) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(buyrex), mvo()("from", alice)("amount", one_eos) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg),
                        push_action( bob, N(unstaketorex), mvo()("owner", alice)("receiver", alice)("from_net", one_eos)("from_cpu", one_eos) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(sellrex), mvo()("from", alice)("rex", one_rex) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(cnclrexorder), mvo()("owner", alice) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg),
                        push_action( bob, N(rentcpu), mvo()("from", alice)("receiver", alice)("loan_payment", one_eos)("loan_fund", one_eos) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg),
                        push_action( bob, N(rentnet), mvo()("from", alice)("receiver", alice)("loan_payment", one_eos)("loan_fund", one_eos) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(fundcpuloan), mvo()("from", alice)("loan_num", 1)("payment", one_eos) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(fundnetloan), mvo()("from", alice)("loan_num", 1)("payment", one_eos) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(defcpuloan), mvo()("from", alice)("loan_num", 1)("amount", one_eos) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(defnetloan), mvo()("from", alice)("loan_num", 1)("amount", one_eos) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(updaterex), mvo()("owner", alice) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(rexexec), mvo()("user", alice)("max", 1) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(consolidate), mvo()("owner", alice) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(mvtosavings), mvo()("owner", alice)("rex", one_rex) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(mvfrsavings), mvo()("owner", alice)("rex", one_rex) ) );
   BOOST_REQUIRE_EQUAL( error(error_msg), push_action( bob, N(closerex), mvo()("owner", alice) ) );

   BOOST_REQUIRE_EQUAL( error("missing authority of eosio"), push_action( alice, N(setrex), mvo()("balance", one_eos) ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( buy_sell_rex, eosio_system_tester ) try {

   const int64_t ratio        = 10000;
   const asset   init_rent    = core_sym::from_string("20000.0000");
   const asset   init_balance = core_sym::from_string("1000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount), N(emilyaccount), N(frankaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2], emily = accounts[3], frank = accounts[4];
   setup_rex_accounts( accounts, init_balance );

   const asset one_unit = core_sym::from_string("0.0001");
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient funds"), buyrex( alice, init_balance + one_unit ) );
   BOOST_REQUIRE_EQUAL( asset::from_string("25000.0000 REX"),  get_buyrex_result( alice, core_sym::from_string("2.5000") ) );
   produce_blocks(2);
   produce_block(fc::days(5));
   BOOST_REQUIRE_EQUAL( core_sym::from_string("2.5000"),     get_sellrex_result( alice, asset::from_string("25000.0000 REX") ) );

   BOOST_REQUIRE_EQUAL( success(),                           buyrex( alice, core_sym::from_string("13.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("13.0000"),    get_rex_vote_stake( alice ) );
   BOOST_REQUIRE_EQUAL( success(),                           buyrex( alice, core_sym::from_string("17.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("30.0000"),    get_rex_vote_stake( alice ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("970.0000"),   get_rex_fund(alice) );
   BOOST_REQUIRE_EQUAL( get_rex_balance(alice).get_amount(), ratio * asset::from_string("30.0000 REX").get_amount() );
   auto rex_pool = get_rex_pool();
   BOOST_REQUIRE_EQUAL( core_sym::from_string("30.0000"),  rex_pool["total_lendable"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("30.0000"),  rex_pool["total_unlent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"),   rex_pool["total_lent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( init_rent,                         rex_pool["total_rent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( get_rex_balance(alice),            rex_pool["total_rex"].as<asset>() );

   BOOST_REQUIRE_EQUAL( success(), buyrex( bob, core_sym::from_string("75.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("925.0000"), get_rex_fund(bob) );
   BOOST_REQUIRE_EQUAL( ratio * asset::from_string("75.0000 REX").get_amount(), get_rex_balance(bob).get_amount() );
   rex_pool = get_rex_pool();
   BOOST_REQUIRE_EQUAL( core_sym::from_string("105.0000"), rex_pool["total_lendable"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("105.0000"), rex_pool["total_unlent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"),   rex_pool["total_lent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( init_rent,                         rex_pool["total_rent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( get_rex_balance(alice) + get_rex_balance(bob), rex_pool["total_rex"].as<asset>() );

   produce_block( fc::days(6) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("user must first buyrex"),        sellrex( carol, asset::from_string("5.0000 REX") ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("asset must be a positive amount of (REX, 4)"),
                        sellrex( bob, core_sym::from_string("55.0000") ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("asset must be a positive amount of (REX, 4)"),
                        sellrex( bob, asset::from_string("-75.0030 REX") ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),    sellrex( bob, asset::from_string("750000.0030 REX") ) );

   auto init_total_rex      = rex_pool["total_rex"].as<asset>().get_amount();
   auto init_total_lendable = rex_pool["total_lendable"].as<asset>().get_amount();
   BOOST_REQUIRE_EQUAL( success(),                             sellrex( bob, asset::from_string("550001.0000 REX") ) );
   BOOST_REQUIRE_EQUAL( asset::from_string("199999.0000 REX"), get_rex_balance(bob) );
   rex_pool = get_rex_pool();
   auto total_rex      = rex_pool["total_rex"].as<asset>().get_amount();
   auto total_lendable = rex_pool["total_lendable"].as<asset>().get_amount();
   BOOST_REQUIRE_EQUAL( init_total_rex / init_total_lendable,          total_rex / total_lendable );
   BOOST_REQUIRE_EQUAL( total_lendable,                                rex_pool["total_unlent"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"),               rex_pool["total_lent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( init_rent,                                     rex_pool["total_rent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( get_rex_balance(alice) + get_rex_balance(bob), rex_pool["total_rex"].as<asset>() );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( buy_sell_small_rex, eosio_system_tester ) try {

   const int64_t ratio        = 10000;
   const asset   init_balance = core_sym::from_string("50000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2];
   setup_rex_accounts( accounts, init_balance );

   const asset payment = core_sym::from_string("40000.0000");
   BOOST_REQUIRE_EQUAL( ratio * payment.get_amount(),               get_buyrex_result( alice, payment ).get_amount() );

   produce_blocks(2);
   produce_block( fc::days(5) );
   produce_blocks(2);

   asset init_rex_stake = get_rex_vote_stake( alice );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("proceeds are negligible"), sellrex( alice, asset::from_string("0.0001 REX") ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("proceeds are negligible"), sellrex( alice, asset::from_string("0.9999 REX") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0001"),            get_sellrex_result( alice, asset::from_string("1.0000 REX") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0001"),            get_sellrex_result( alice, asset::from_string("1.9999 REX") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0002"),            get_sellrex_result( alice, asset::from_string("2.0000 REX") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0002"),            get_sellrex_result( alice, asset::from_string("2.9999 REX") ) );
   BOOST_REQUIRE_EQUAL( get_rex_vote_stake( alice ),                init_rex_stake - core_sym::from_string("0.0006") );

   BOOST_REQUIRE_EQUAL( success(),                                  rentcpu( carol, bob, core_sym::from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( success(),                                  sellrex( alice, asset::from_string("1.0000 REX") ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("proceeds are negligible"), sellrex( alice, asset::from_string("0.4000 REX") ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( unstake_buy_rex, eosio_system_tester, * boost::unit_test::tolerance(1e-10) ) try {

   const int64_t ratio        = 10000;
   const asset   zero_asset   = core_sym::from_string("0.0000");
   const asset   neg_asset    = core_sym::from_string("-0.0001");
   const asset   one_token    = core_sym::from_string("1.0000");
   const asset   init_balance = core_sym::from_string("10000.0000");
   const asset   init_net     = core_sym::from_string("70.0000");
   const asset   init_cpu     = core_sym::from_string("90.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount), N(emilyaccount), N(frankaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2], emily = accounts[3], frank = accounts[4];
   setup_rex_accounts( accounts, init_balance, init_net, init_cpu, false );

   // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
   std::vector<account_name> producer_names;
   {
      producer_names.reserve(30);
      const std::string root("tprod");
      for(uint8_t i = 0; i < 30; i++) {
         name p = name(root + toBase31(i));
         producer_names.emplace_back(p);
      }

      setup_producer_accounts(producer_names);
      for ( const auto& p: producer_names ) {
         BOOST_REQUIRE_EQUAL( success(), regproducer(p) );
         BOOST_TEST_REQUIRE( 0 == get_producer_info(p)["total_votes"].as<double>() );
      }
      std::sort(producer_names.begin(), producer_names.end());
   }

   const int64_t init_cpu_limit = get_cpu_limit( alice );
   const int64_t init_net_limit = get_net_limit( alice );

   {
      const asset net_stake = core_sym::from_string("25.5000");
      const asset cpu_stake = core_sym::from_string("12.4000");
      const asset tot_stake = net_stake + cpu_stake;
      BOOST_REQUIRE_EQUAL( init_balance,                            get_balance( alice ) );
      BOOST_REQUIRE_EQUAL( success(),                               stake( alice, alice, net_stake, cpu_stake ) );
      BOOST_REQUIRE_EQUAL( get_cpu_limit( alice ),                  init_cpu_limit + cpu_stake.get_amount() );
      BOOST_REQUIRE_EQUAL( get_net_limit( alice ),                  init_net_limit + net_stake.get_amount() );
      BOOST_REQUIRE_EQUAL( success(),
                           vote( alice, std::vector<account_name>(producer_names.begin(), producer_names.begin() + 20) ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("must vote for at least 21 producers or for a proxy before buying REX"),
                           unstaketorex( alice, alice, net_stake, cpu_stake ) );
      BOOST_REQUIRE_EQUAL( success(),
                           vote( alice, std::vector<account_name>(producer_names.begin(), producer_names.begin() + 30) ) );
      const asset init_eosio_stake_balance = get_balance( N(eosio.stake) );
      const auto init_voter_info = get_voter_info( alice );
      const auto init_prod_info  = get_producer_info( producer_names[0] );
      BOOST_TEST_REQUIRE( init_prod_info["total_votes"].as_double() ==
                          stake2votes( asset( init_voter_info["staked"].as<int64_t>(), symbol{CORE_SYM} ) ) );
      produce_block( fc::days(4) );
      BOOST_REQUIRE_EQUAL( ratio * tot_stake.get_amount(),          get_unstaketorex_result( alice, alice, net_stake, cpu_stake ).get_amount() );
      BOOST_REQUIRE_EQUAL( get_cpu_limit( alice ),                  init_cpu_limit );
      BOOST_REQUIRE_EQUAL( get_net_limit( alice ),                  init_net_limit );
      BOOST_REQUIRE_EQUAL( ratio * tot_stake.get_amount(),          get_rex_balance( alice ).get_amount() );
      BOOST_REQUIRE_EQUAL( tot_stake,                               get_rex_balance_obj( alice )["vote_stake"].as<asset>() );
      BOOST_REQUIRE_EQUAL( tot_stake,                               get_balance( N(eosio.rex) ) );
      BOOST_REQUIRE_EQUAL( tot_stake,                               init_eosio_stake_balance - get_balance( N(eosio.stake) ) );
      auto current_voter_info = get_voter_info( alice );
      auto current_prod_info  = get_producer_info( producer_names[0] );
      BOOST_REQUIRE_EQUAL( init_voter_info["staked"].as<int64_t>(), current_voter_info["staked"].as<int64_t>() );
      BOOST_TEST_REQUIRE( current_prod_info["total_votes"].as_double() ==
                          stake2votes( asset( current_voter_info["staked"].as<int64_t>(), symbol{CORE_SYM} ) ) );
      BOOST_TEST_REQUIRE( init_prod_info["total_votes"].as_double() <=
       current_prod_info["total_votes"].as_double() );
   }

   {
      const asset net_stake = core_sym::from_string("200.5000");
      const asset cpu_stake = core_sym::from_string("120.0000");
      const asset tot_stake = net_stake + cpu_stake;
      BOOST_REQUIRE_EQUAL( success(),                               stake( bob, carol, net_stake, cpu_stake ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("amount exceeds tokens staked for net"),
                           unstaketorex( bob, carol, net_stake + one_token, zero_asset ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("amount exceeds tokens staked for cpu"),
                           unstaketorex( bob, carol, zero_asset, cpu_stake + one_token ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("delegated bandwidth record does not exist"),
                           unstaketorex( bob, emily, zero_asset, one_token ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("must unstake a positive amount to buy rex"),
                           unstaketorex( bob, carol, zero_asset, zero_asset ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("must unstake a positive amount to buy rex"),
                           unstaketorex( bob, carol, neg_asset, one_token ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("must unstake a positive amount to buy rex"),
                           unstaketorex( bob, carol, one_token, neg_asset ) );
      BOOST_REQUIRE_EQUAL( init_net_limit + net_stake.get_amount(), get_net_limit( carol ) );
      BOOST_REQUIRE_EQUAL( init_cpu_limit + cpu_stake.get_amount(), get_cpu_limit( carol ) );
      BOOST_REQUIRE_EQUAL( false,                                   get_dbw_obj( bob, carol ).is_null() );
      BOOST_REQUIRE_EQUAL( success(),                               unstaketorex( bob, carol, net_stake, zero_asset ) );
      BOOST_REQUIRE_EQUAL( false,                                   get_dbw_obj( bob, carol ).is_null() );
      BOOST_REQUIRE_EQUAL( success(),                               unstaketorex( bob, carol, zero_asset, cpu_stake ) );
      BOOST_REQUIRE_EQUAL( true,                                    get_dbw_obj( bob, carol ).is_null() );
      BOOST_REQUIRE_EQUAL( 0,                                       get_rex_balance( carol ).get_amount() );
      BOOST_REQUIRE_EQUAL( ratio * tot_stake.get_amount(),          get_rex_balance( bob ).get_amount() );
      BOOST_REQUIRE_EQUAL( init_cpu_limit,                          get_cpu_limit( bob ) );
      BOOST_REQUIRE_EQUAL( init_net_limit,                          get_net_limit( bob ) );
      BOOST_REQUIRE_EQUAL( init_cpu_limit,                          get_cpu_limit( carol ) );
      BOOST_REQUIRE_EQUAL( init_net_limit,                          get_net_limit( carol ) );
   }

   {
      const asset net_stake = core_sym::from_string("130.5000");
      const asset cpu_stake = core_sym::from_string("220.0800");
      const asset tot_stake = net_stake + cpu_stake;
      BOOST_REQUIRE_EQUAL( success(),                               stake_with_transfer( emily, frank, net_stake, cpu_stake ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("delegated bandwidth record does not exist"),
                           unstaketorex( emily, frank, net_stake, cpu_stake ) );
      BOOST_REQUIRE_EQUAL( success(),                               unstaketorex( frank, frank, net_stake, cpu_stake ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( frank, asset::from_string("1.0000 REX") ) );
      produce_block( fc::days(5) );
      BOOST_REQUIRE_EQUAL( success(),                               sellrex( frank, asset::from_string("1.0000 REX") ) );
   }

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( buy_rent_rex, eosio_system_tester ) try {

   const int64_t ratio        = 10000;
   const asset   init_balance = core_sym::from_string("60000.0000");
   const asset   init_net     = core_sym::from_string("70.0000");
   const asset   init_cpu     = core_sym::from_string("90.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount), N(emilyaccount), N(frankaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2], emily = accounts[3], frank = accounts[4];
   setup_rex_accounts( accounts, init_balance, init_net, init_cpu );

   const int64_t init_cpu_limit = get_cpu_limit( alice );
   const int64_t init_net_limit = get_net_limit( alice );

   // bob tries to rent rex
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("rex system not initialized yet"), rentcpu( bob, carol, core_sym::from_string("5.0000") ) );
   // alice lends rex
   BOOST_REQUIRE_EQUAL( success(), buyrex( alice, core_sym::from_string("50265.0000") ) );
   BOOST_REQUIRE_EQUAL( init_balance - core_sym::from_string("50265.0000"), get_rex_fund(alice) );
   auto rex_pool = get_rex_pool();
   const asset   init_tot_unlent   = rex_pool["total_unlent"].as<asset>();
   const asset   init_tot_lendable = rex_pool["total_lendable"].as<asset>();
   const asset   init_tot_rent     = rex_pool["total_rent"].as<asset>();
   const int64_t init_stake        = get_voter_info(alice)["staked"].as<int64_t>();
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"),        rex_pool["total_lent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( ratio * init_tot_lendable.get_amount(), rex_pool["total_rex"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( rex_pool["total_rex"].as<asset>(),      get_rex_balance(alice) );

   {
      // bob rents cpu for carol
      const asset fee = core_sym::from_string("17.0000");
      BOOST_REQUIRE_EQUAL( success(),          rentcpu( bob, carol, fee ) );
      BOOST_REQUIRE_EQUAL( init_balance - fee, get_rex_fund(bob) );
      rex_pool = get_rex_pool();
      BOOST_REQUIRE_EQUAL( init_tot_lendable + fee, rex_pool["total_lendable"].as<asset>() ); // 65 + 17
      BOOST_REQUIRE_EQUAL( init_tot_rent + fee,     rex_pool["total_rent"].as<asset>() );     // 100 + 17
      int64_t expected_total_lent = bancor_convert( init_tot_rent.get_amount(), init_tot_unlent.get_amount(), fee.get_amount() );
      BOOST_REQUIRE_EQUAL( expected_total_lent,
                           rex_pool["total_lent"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( rex_pool["total_lent"].as<asset>() + rex_pool["total_unlent"].as<asset>(),
                           rex_pool["total_lendable"].as<asset>() );

      // test that carol's resource limits have been updated properly
      BOOST_REQUIRE_EQUAL( expected_total_lent, get_cpu_limit( carol ) - init_cpu_limit );
      BOOST_REQUIRE_EQUAL( 0,                   get_net_limit( carol ) - init_net_limit );

      // alice tries to sellrex, order gets scheduled then she cancels order
      BOOST_REQUIRE_EQUAL( cancelrexorder( alice ),           wasm_assert_msg("no sellrex order is scheduled") );
      produce_block( fc::days(6) );
      BOOST_REQUIRE_EQUAL( success(),                         sellrex( alice, get_rex_balance(alice) ) );
      BOOST_REQUIRE_EQUAL( success(),                         cancelrexorder( alice ) );
      BOOST_REQUIRE_EQUAL( rex_pool["total_rex"].as<asset>(), get_rex_balance(alice) );

      produce_block( fc::days(20) );
      BOOST_REQUIRE_EQUAL( success(), sellrex( alice, get_rex_balance(alice) ) );
      BOOST_REQUIRE_EQUAL( success(), cancelrexorder( alice ) );
      produce_block( fc::days(10) );
      // alice is finally able to sellrex, she gains the fee paid by bob
      BOOST_REQUIRE_EQUAL( success(),          sellrex( alice, get_rex_balance(alice) ) );
      BOOST_REQUIRE_EQUAL( 0,                  get_rex_balance(alice).get_amount() );
      BOOST_REQUIRE_EQUAL( init_balance + fee, get_rex_fund(alice) );
      // test that carol's resource limits have been updated properly when loan expires
      BOOST_REQUIRE_EQUAL( init_cpu_limit,     get_cpu_limit( carol ) );
      BOOST_REQUIRE_EQUAL( init_net_limit,     get_net_limit( carol ) );

      rex_pool = get_rex_pool();
      BOOST_REQUIRE_EQUAL( 0, rex_pool["total_lendable"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 0, rex_pool["total_unlent"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 0, rex_pool["total_rex"].as<asset>().get_amount() );
   }

   {
      const int64_t init_net_limit = get_net_limit( emily );
      BOOST_REQUIRE_EQUAL( 0,         get_rex_balance(alice).get_amount() );
      BOOST_REQUIRE_EQUAL( success(), buyrex( alice, core_sym::from_string("20050.0000") ) );
      rex_pool = get_rex_pool();
      const asset fee = core_sym::from_string("0.4560");
      int64_t expected_net = bancor_convert( rex_pool["total_rent"].as<asset>().get_amount(),
                                             rex_pool["total_unlent"].as<asset>().get_amount(),
                                             fee.get_amount() );
      BOOST_REQUIRE_EQUAL( success(),    rentnet( emily, emily, fee ) );
      BOOST_REQUIRE_EQUAL( expected_net, get_net_limit( emily ) - init_net_limit );
   }

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( buy_sell_sell_rex, eosio_system_tester ) try {

   const int64_t ratio        = 10000;
   const asset   init_balance = core_sym::from_string("40000.0000");
   const asset   init_net     = core_sym::from_string("70.0000");
   const asset   init_cpu     = core_sym::from_string("90.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2];
   setup_rex_accounts( accounts, init_balance, init_net, init_cpu );

   const int64_t init_cpu_limit = get_cpu_limit( alice );
   const int64_t init_net_limit = get_net_limit( alice );

   // alice lends rex
   const asset payment = core_sym::from_string("30250.0000");
   BOOST_REQUIRE_EQUAL( success(),              buyrex( alice, payment ) );
   BOOST_REQUIRE_EQUAL( success(),              buyrex( bob, core_sym::from_string("0.0005") ) );
   BOOST_REQUIRE_EQUAL( init_balance - payment, get_rex_fund(alice) );
   auto rex_pool = get_rex_pool();
   const asset   init_tot_unlent   = rex_pool["total_unlent"].as<asset>();
   const asset   init_tot_lendable = rex_pool["total_lendable"].as<asset>();
   const asset   init_tot_rent     = rex_pool["total_rent"].as<asset>();
   const int64_t init_stake        = get_voter_info(alice)["staked"].as<int64_t>();
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"),        rex_pool["total_lent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( ratio * init_tot_lendable.get_amount(), rex_pool["total_rex"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( rex_pool["total_rex"].as<asset>(),      get_rex_balance(alice) + get_rex_balance(bob) );

   // bob rents cpu for carol
   const asset fee = core_sym::from_string("7.0000");
   BOOST_REQUIRE_EQUAL( success(),               rentcpu( bob, carol, fee ) );
   rex_pool = get_rex_pool();
   BOOST_REQUIRE_EQUAL( init_tot_lendable + fee, rex_pool["total_lendable"].as<asset>() );
   BOOST_REQUIRE_EQUAL( init_tot_rent + fee,     rex_pool["total_rent"].as<asset>() );

   produce_block( fc::days(5) );
   produce_blocks(2);
   const asset rex_tok = asset::from_string("1.0000 REX");
   BOOST_REQUIRE_EQUAL( success(),                                           sellrex( alice, get_rex_balance(alice) - rex_tok ) );
   BOOST_REQUIRE_EQUAL( false,                                               get_rex_order_obj( alice ).is_null() );
   BOOST_REQUIRE_EQUAL( success(),                                           sellrex( alice, rex_tok ) );
   BOOST_REQUIRE_EQUAL( sellrex( alice, rex_tok ),                           wasm_assert_msg("insufficient funds for current and scheduled orders") );
   BOOST_REQUIRE_EQUAL( ratio * payment.get_amount() - rex_tok.get_amount(), get_rex_order( alice )["rex_requested"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( success(),                                           consolidate( alice ) );
   BOOST_REQUIRE_EQUAL( 0,                                                   get_rex_balance_obj( alice )["rex_maturities"].get_array().size() );

   produce_block( fc::days(26) );
   produce_blocks(2);

   BOOST_REQUIRE_EQUAL( success(),  rexexec( alice, 2 ) );
   BOOST_REQUIRE_EQUAL( 0,          get_rex_balance( alice ).get_amount() );
   BOOST_REQUIRE_EQUAL( 0,          get_rex_balance_obj( alice )["matured_rex"].as<int64_t>() );
   const asset init_fund = get_rex_fund( alice );
   BOOST_REQUIRE_EQUAL( success(),  updaterex( alice ) );
   BOOST_REQUIRE_EQUAL( 0,          get_rex_balance( alice ).get_amount() );
   BOOST_REQUIRE_EQUAL( 0,          get_rex_balance_obj( alice )["matured_rex"].as<int64_t>() );
   BOOST_REQUIRE      ( init_fund < get_rex_fund( alice ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( buy_sell_claim_rex, eosio_system_tester ) try {

   const asset init_balance = core_sym::from_string("3000000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount), N(emilyaccount), N(frankaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2], emily = accounts[3], frank = accounts[4];
   setup_rex_accounts( accounts, init_balance );

   const auto purchase1  = core_sym::from_string("50000.0000");
   const auto purchase2  = core_sym::from_string("235500.0000");
   const auto purchase3  = core_sym::from_string("234500.0000");
   const auto init_stake = get_voter_info(alice)["staked"].as<int64_t>();
   BOOST_REQUIRE_EQUAL( success(), buyrex( alice, purchase1) );
   BOOST_REQUIRE_EQUAL( success(), buyrex( bob,   purchase2) );
   BOOST_REQUIRE_EQUAL( success(), buyrex( carol, purchase3) );

   BOOST_REQUIRE_EQUAL( init_balance - purchase1, get_rex_fund(alice) );
   BOOST_REQUIRE_EQUAL( purchase1.get_amount(),   get_voter_info(alice)["staked"].as<int64_t>() - init_stake );

   BOOST_REQUIRE_EQUAL( init_balance - purchase2, get_rex_fund(bob) );
   BOOST_REQUIRE_EQUAL( init_balance - purchase3, get_rex_fund(carol) );

   auto init_alice_rex = get_rex_balance(alice);
   auto init_bob_rex   = get_rex_balance(bob);
   auto init_carol_rex = get_rex_balance(carol);

   BOOST_REQUIRE_EQUAL( core_sym::from_string("20000.0000"), get_rex_pool()["total_rent"].as<asset>() );

   for (uint8_t i = 0; i < 4; ++i) {
      BOOST_REQUIRE_EQUAL( success(), rentcpu( emily, emily, core_sym::from_string("20000.0000") ) );
   }

   const asset rent_payment = core_sym::from_string("40000.0000");

   BOOST_REQUIRE_EQUAL( success(), rentcpu( frank, frank, rent_payment, rent_payment ) );

   const auto    init_rex_pool        = get_rex_pool();
   const int64_t total_lendable       = init_rex_pool["total_lendable"].as<asset>().get_amount();
   const int64_t total_rex            = init_rex_pool["total_rex"].as<asset>().get_amount();
   const int64_t init_alice_rex_stake = ( eosio::chain::uint128_t(init_alice_rex.get_amount()) * total_lendable ) / total_rex;

   produce_block( fc::days(5) );

   BOOST_REQUIRE_EQUAL( success(), sellrex( alice, asset( 3 * get_rex_balance(alice).get_amount() / 4, symbol(SY(4,REX)) ) ) );

   BOOST_TEST_REQUIRE( within_one( init_alice_rex.get_amount() / 4, get_rex_balance(alice).get_amount() ) );
   BOOST_TEST_REQUIRE( within_one( init_alice_rex_stake / 4,        get_rex_vote_stake( alice ).get_amount() ) );
   BOOST_TEST_REQUIRE( within_one( init_alice_rex_stake / 4,        get_voter_info(alice)["staked"].as<int64_t>() - init_stake ) );

   produce_block( fc::days(5) );

   init_alice_rex = get_rex_balance(alice);
   BOOST_REQUIRE_EQUAL( success(), sellrex( bob,   get_rex_balance(bob) ) );
   BOOST_REQUIRE_EQUAL( success(), sellrex( carol, get_rex_balance(carol) ) );
   BOOST_REQUIRE_EQUAL( success(), sellrex( alice, get_rex_balance(alice) ) );

   BOOST_REQUIRE_EQUAL( init_bob_rex,   get_rex_balance(bob) );
   BOOST_REQUIRE_EQUAL( init_carol_rex, get_rex_balance(carol) );
   BOOST_REQUIRE_EQUAL( init_alice_rex, get_rex_balance(alice) );

   // now bob's, carol's and alice's sellrex orders have been queued
   BOOST_REQUIRE_EQUAL( true,           get_rex_order(alice)["is_open"].as<bool>() );
   BOOST_REQUIRE_EQUAL( init_alice_rex, get_rex_order(alice)["rex_requested"].as<asset>() );
   BOOST_REQUIRE_EQUAL( 0,              get_rex_order(alice)["proceeds"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( true,           get_rex_order(bob)["is_open"].as<bool>() );
   BOOST_REQUIRE_EQUAL( init_bob_rex,   get_rex_order(bob)["rex_requested"].as<asset>() );
   BOOST_REQUIRE_EQUAL( 0,              get_rex_order(bob)["proceeds"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( true,           get_rex_order(carol)["is_open"].as<bool>() );
   BOOST_REQUIRE_EQUAL( init_carol_rex, get_rex_order(carol)["rex_requested"].as<asset>() );
   BOOST_REQUIRE_EQUAL( 0,              get_rex_order(carol)["proceeds"].as<asset>().get_amount() );

   // wait for 30 days minus 1 hour
   produce_block( fc::hours(19*24 + 23) );
   BOOST_REQUIRE_EQUAL( success(),      updaterex( alice ) );
   BOOST_REQUIRE_EQUAL( true,           get_rex_order(alice)["is_open"].as<bool>() );
   BOOST_REQUIRE_EQUAL( true,           get_rex_order(bob)["is_open"].as<bool>() );
   BOOST_REQUIRE_EQUAL( true,           get_rex_order(carol)["is_open"].as<bool>() );

   // wait for 2 more hours, by now frank's loan has expired and there is enough balance in
   // total_unlent to close some sellrex orders. only two are processed, bob's and carol's.
   // alices's order is still open.
   // an action is needed to trigger queue processing
   produce_block( fc::hours(2) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("rex loans are currently not available"),
                        rentcpu( frank, frank, core_sym::from_string("0.0001") ) );
   {
      auto trace = base_tester::push_action( config::system_account_name, N(rexexec), frank,
                                             mvo()("user", frank)("max", 2) );
      auto output = get_rexorder_result( trace );
      BOOST_REQUIRE_EQUAL( output.size(),    1 );
      BOOST_REQUIRE_EQUAL( output[0].first,  bob );
      BOOST_REQUIRE_EQUAL( output[0].second, get_rex_order(bob)["proceeds"].as<asset>() );
   }

   {
      BOOST_REQUIRE_EQUAL( true,           get_rex_order(alice)["is_open"].as<bool>() );
      BOOST_REQUIRE_EQUAL( init_alice_rex, get_rex_order(alice)["rex_requested"].as<asset>() );
      BOOST_REQUIRE_EQUAL( 0,              get_rex_order(alice)["proceeds"].as<asset>().get_amount() );

      BOOST_REQUIRE_EQUAL( false,          get_rex_order(bob)["is_open"].as<bool>() );
      BOOST_REQUIRE_EQUAL( init_bob_rex,   get_rex_order(bob)["rex_requested"].as<asset>() );
      BOOST_REQUIRE      ( 0 <             get_rex_order(bob)["proceeds"].as<asset>().get_amount() );

      BOOST_REQUIRE_EQUAL( true,           get_rex_order(carol)["is_open"].as<bool>() );
      BOOST_REQUIRE_EQUAL( init_carol_rex, get_rex_order(carol)["rex_requested"].as<asset>() );
      BOOST_REQUIRE_EQUAL( 0,              get_rex_order(carol)["proceeds"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("rex loans are currently not available"),
                           rentcpu( frank, frank, core_sym::from_string("1.0000") ) );
   }

   {
      auto trace1 = base_tester::push_action( config::system_account_name, N(updaterex), bob, mvo()("owner", bob) );
      auto trace2 = base_tester::push_action( config::system_account_name, N(updaterex), carol, mvo()("owner", carol) );
      BOOST_REQUIRE_EQUAL( 0,              get_rex_vote_stake( bob ).get_amount() );
      BOOST_REQUIRE_EQUAL( init_stake,     get_voter_info( bob )["staked"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( 0,              get_rex_vote_stake( carol ).get_amount() );
      BOOST_REQUIRE_EQUAL( init_stake,     get_voter_info( carol )["staked"].as<int64_t>() );
      auto output1 = get_rexorder_result( trace1 );
      auto output2 = get_rexorder_result( trace2 );
      BOOST_REQUIRE_EQUAL( 2,              output1.size() + output2.size() );

      BOOST_REQUIRE_EQUAL( false,          get_rex_order_obj(alice).is_null() );
      BOOST_REQUIRE_EQUAL( true,           get_rex_order_obj(bob).is_null() );
      BOOST_REQUIRE_EQUAL( true,           get_rex_order_obj(carol).is_null() );
      BOOST_REQUIRE_EQUAL( false,          get_rex_order(alice)["is_open"].as<bool>() );

      const auto& rex_pool = get_rex_pool();
      BOOST_REQUIRE_EQUAL( 0,              rex_pool["total_lendable"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 0,              rex_pool["total_rex"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("rex loans are currently not available"),
                           rentcpu( frank, frank, core_sym::from_string("1.0000") ) );

      BOOST_REQUIRE_EQUAL( success(),      buyrex( emily, core_sym::from_string("22000.0000") ) );
      BOOST_REQUIRE_EQUAL( false,          get_rex_order_obj(alice).is_null() );
      BOOST_REQUIRE_EQUAL( false,          get_rex_order(alice)["is_open"].as<bool>() );

      BOOST_REQUIRE_EQUAL( success(),      rentcpu( frank, frank, core_sym::from_string("1.0000") ) );
   }

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( rex_loans, eosio_system_tester ) try {

   const int64_t ratio        = 10000;
   const asset   init_balance = core_sym::from_string("40000.0000");
   const asset   one_unit     = core_sym::from_string("0.0001");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount), N(emilyaccount), N(frankaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2], emily = accounts[3], frank = accounts[4];
   setup_rex_accounts( accounts, init_balance  );

   BOOST_REQUIRE_EQUAL( success(), buyrex( alice, core_sym::from_string("25000.0000") ) );

   auto rex_pool            = get_rex_pool();
   const asset payment      = core_sym::from_string("30.0000");
   const asset zero_asset   = core_sym::from_string("0.0000");
   const asset neg_asset    = core_sym::from_string("-1.0000");
   BOOST_TEST_REQUIRE( 0 > neg_asset.get_amount() );
   asset cur_frank_balance  = get_rex_fund( frank );
   int64_t expected_stake   = bancor_convert( rex_pool["total_rent"].as<asset>().get_amount(),
                                              rex_pool["total_unlent"].as<asset>().get_amount(),
                                              payment.get_amount() );
   const int64_t init_stake = get_cpu_limit( frank );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must use core token"),
                        rentcpu( frank, bob, asset::from_string("10.0000 RND") ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must use core token"),
                        rentcpu( frank, bob, payment, asset::from_string("10.0000 RND") ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must use positive asset amount"),
                        rentcpu( frank, bob, zero_asset ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must use positive asset amount"),
                        rentcpu( frank, bob, payment, neg_asset ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must use positive asset amount"),
                        rentcpu( frank, bob, neg_asset, payment ) );
   // create 2 cpu and 3 net loans
   const asset rented_tokens{ expected_stake, symbol{CORE_SYM} };
   BOOST_REQUIRE_EQUAL( rented_tokens,  get_rentcpu_result( frank, bob, payment ) ); // loan_num = 1
   BOOST_REQUIRE_EQUAL( success(),      rentcpu( alice, emily, payment ) );          // loan_num = 2
   BOOST_REQUIRE_EQUAL( 2,              get_last_cpu_loan()["loan_num"].as_uint64() );

   asset expected_rented_net;
   {
      const auto& pool = get_rex_pool();
      const int64_t r  = bancor_convert( pool["total_rent"].as<asset>().get_amount(),
                                         pool["total_unlent"].as<asset>().get_amount(),
                                         payment.get_amount() );
      expected_rented_net = asset{ r, symbol{CORE_SYM} };
   }
   BOOST_REQUIRE_EQUAL( expected_rented_net, get_rentnet_result( alice, emily, payment ) ); // loan_num = 3
   BOOST_REQUIRE_EQUAL( success(),           rentnet( alice, alice, payment ) );            // loan_num = 4
   BOOST_REQUIRE_EQUAL( success(),           rentnet( alice, frank, payment ) );            // loan_num = 5
   BOOST_REQUIRE_EQUAL( 5,                   get_last_net_loan()["loan_num"].as_uint64() );

   auto loan_info         = get_cpu_loan(1);
   auto old_frank_balance = cur_frank_balance;
   cur_frank_balance      = get_rex_fund( frank );
   BOOST_REQUIRE_EQUAL( old_frank_balance,           payment + cur_frank_balance );
   BOOST_REQUIRE_EQUAL( 1,                           loan_info["loan_num"].as_uint64() );
   BOOST_REQUIRE_EQUAL( payment,                     loan_info["payment"].as<asset>() );
   BOOST_REQUIRE_EQUAL( 0,                           loan_info["balance"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( expected_stake,              loan_info["total_staked"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( expected_stake + init_stake, get_cpu_limit( bob ) );

   // frank funds his loan enough to be renewed once
   const asset fund = core_sym::from_string("35.0000");
   BOOST_REQUIRE_EQUAL( fundcpuloan( frank, 1, cur_frank_balance + one_unit), wasm_assert_msg("insufficient funds") );
   BOOST_REQUIRE_EQUAL( fundnetloan( frank, 1, fund ), wasm_assert_msg("loan not found") );
   BOOST_REQUIRE_EQUAL( fundcpuloan( alice, 1, fund ), wasm_assert_msg("user must be loan creator") );
   BOOST_REQUIRE_EQUAL( success(),                     fundcpuloan( frank, 1, fund ) );
   old_frank_balance = cur_frank_balance;
   cur_frank_balance = get_rex_fund( frank );
   loan_info         = get_cpu_loan(1);
   BOOST_REQUIRE_EQUAL( old_frank_balance, fund + cur_frank_balance );
   BOOST_REQUIRE_EQUAL( fund,              loan_info["balance"].as<asset>() );
   BOOST_REQUIRE_EQUAL( payment,           loan_info["payment"].as<asset>() );

   // in the meantime, defund then fund the same amount and test the balances
   {
      const asset amount = core_sym::from_string("7.5000");
      BOOST_REQUIRE_EQUAL( defundnetloan( frank, 1, fund ),                             wasm_assert_msg("loan not found") );
      BOOST_REQUIRE_EQUAL( defundcpuloan( alice, 1, fund ),                             wasm_assert_msg("user must be loan creator") );
      BOOST_REQUIRE_EQUAL( defundcpuloan( frank, 1, core_sym::from_string("75.0000") ), wasm_assert_msg("insufficent loan balance") );
      old_frank_balance = cur_frank_balance;
      asset old_loan_balance = get_cpu_loan(1)["balance"].as<asset>();
      BOOST_REQUIRE_EQUAL( defundcpuloan( frank, 1, amount ), success() );
      BOOST_REQUIRE_EQUAL( old_loan_balance,                  get_cpu_loan(1)["balance"].as<asset>() + amount );
      cur_frank_balance = get_rex_fund( frank );
      old_loan_balance  = get_cpu_loan(1)["balance"].as<asset>();
      BOOST_REQUIRE_EQUAL( old_frank_balance + amount,        cur_frank_balance );
      old_frank_balance = cur_frank_balance;
      BOOST_REQUIRE_EQUAL( fundcpuloan( frank, 1, amount ),   success() );
      BOOST_REQUIRE_EQUAL( old_loan_balance + amount,         get_cpu_loan(1)["balance"].as<asset>() );
      cur_frank_balance = get_rex_fund( frank );
      BOOST_REQUIRE_EQUAL( old_frank_balance - amount,        cur_frank_balance );
   }

   // wait for 30 days, frank's loan will be renewed at the current price
   produce_block( fc::hours(30*24 + 1) );
   rex_pool = get_rex_pool();
   {
      int64_t unlent_tokens = bancor_convert( rex_pool["total_unlent"].as<asset>().get_amount(),
                                              rex_pool["total_rent"].as<asset>().get_amount(),
                                              expected_stake );

      expected_stake = bancor_convert( rex_pool["total_rent"].as<asset>().get_amount() - unlent_tokens,
                                       rex_pool["total_unlent"].as<asset>().get_amount() + expected_stake,
                                       payment.get_amount() );
   }

   BOOST_REQUIRE_EQUAL( success(), sellrex( alice, asset::from_string("1.0000 REX") ) );

   loan_info = get_cpu_loan(1);
   BOOST_REQUIRE_EQUAL( payment,                     loan_info["payment"].as<asset>() );
   BOOST_REQUIRE_EQUAL( fund - payment,              loan_info["balance"].as<asset>() );
   BOOST_REQUIRE_EQUAL( expected_stake,              loan_info["total_staked"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( expected_stake + init_stake, get_cpu_limit( bob ) );

   // check that loans have been processed in order
   BOOST_REQUIRE_EQUAL( false, get_cpu_loan(1).is_null() );
   BOOST_REQUIRE_EQUAL( true,  get_cpu_loan(2).is_null() );
   BOOST_REQUIRE_EQUAL( true,  get_net_loan(3).is_null() );
   BOOST_REQUIRE_EQUAL( true,  get_net_loan(4).is_null() );
   BOOST_REQUIRE_EQUAL( false, get_net_loan(5).is_null() );
   BOOST_REQUIRE_EQUAL( 1,     get_last_cpu_loan()["loan_num"].as_uint64() );
   BOOST_REQUIRE_EQUAL( 5,     get_last_net_loan()["loan_num"].as_uint64() );

   // wait for another month, frank's loan doesn't have enough funds and will be closed
   produce_block( fc::hours(30*24) );
   BOOST_REQUIRE_EQUAL( success(),  buyrex( alice, core_sym::from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( true,       get_cpu_loan(1).is_null() );
   BOOST_REQUIRE_EQUAL( init_stake, get_cpu_limit( bob ) );
   old_frank_balance = cur_frank_balance;
   cur_frank_balance = get_rex_fund( frank );
   BOOST_REQUIRE_EQUAL( fund - payment,     cur_frank_balance - old_frank_balance );
   BOOST_REQUIRE      ( old_frank_balance < cur_frank_balance );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( rex_loan_checks, eosio_system_tester ) try {

   const int64_t ratio        = 10000;
   const asset   init_balance = core_sym::from_string("40000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount) };
   account_name alice = accounts[0], bob = accounts[1];
   setup_rex_accounts( accounts, init_balance );

   const asset payment1 = core_sym::from_string("20000.0000");
   const asset payment2 = core_sym::from_string("4.0000");
   const asset fee      = core_sym::from_string("1.0000");
   BOOST_REQUIRE_EQUAL( success(), buyrex( alice, payment1 ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("loan price does not favor renting"),
                        rentcpu( bob, bob, fee ) );
   BOOST_REQUIRE_EQUAL( success(),            buyrex( alice, payment2 ) );
   BOOST_REQUIRE_EQUAL( success(),            rentcpu( bob, bob, fee, fee + fee + fee ) );
   BOOST_REQUIRE_EQUAL( true,                 !get_cpu_loan(1).is_null() );
   BOOST_REQUIRE_EQUAL( 3 * fee.get_amount(), get_last_cpu_loan()["balance"].as<asset>().get_amount() );

   produce_block( fc::days(31) );
   BOOST_REQUIRE_EQUAL( success(),            rexexec( alice, 3) );
   BOOST_REQUIRE_EQUAL( 2 * fee.get_amount(), get_last_cpu_loan()["balance"].as<asset>().get_amount() );

   BOOST_REQUIRE_EQUAL( success(),            sellrex( alice, asset::from_string("1000000.0000 REX") ) );
   produce_block( fc::days(31) );
   BOOST_REQUIRE_EQUAL( success(),            rexexec( alice, 3) );
   BOOST_REQUIRE_EQUAL( true,                 get_cpu_loan(1).is_null() );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( ramfee_namebid_to_rex, eosio_system_tester ) try {
   
   const int64_t ratio        = 10000;
   const asset   init_balance = core_sym::from_string("10000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount), N(emilyaccount), N(frankaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2], emily = accounts[3], frank = accounts[4];
   setup_rex_accounts( accounts, init_balance, core_sym::from_string("80.0000"), core_sym::from_string("80.0000"), false );

   asset cur_ramfee_balance = get_balance( N(eosio.ramfee) );
   BOOST_REQUIRE_EQUAL( success(),                      buyram( alice, alice, core_sym::from_string("20.0000") ) );
   BOOST_REQUIRE_EQUAL( get_balance( N(eosio.ramfee) ), core_sym::from_string("0.1000") + cur_ramfee_balance );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must deposit to REX fund first"),
                        buyrex( alice, core_sym::from_string("350.0000") ) );
   BOOST_REQUIRE_EQUAL( success(),                      deposit( alice, core_sym::from_string("350.0000") ) );
   BOOST_REQUIRE_EQUAL( success(),                      buyrex( alice, core_sym::from_string("350.0000") ) );
   cur_ramfee_balance = get_balance( N(eosio.ramfee) );
   asset cur_rex_balance = get_balance( N(eosio.rex) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("350.0000"), cur_rex_balance );
   BOOST_REQUIRE_EQUAL( success(),                         buyram( bob, carol, core_sym::from_string("70.0000") ) );
   BOOST_REQUIRE_EQUAL( cur_ramfee_balance,                get_balance( N(eosio.ramfee) ) );
   BOOST_REQUIRE_EQUAL( get_balance( N(eosio.rex) ),       cur_rex_balance + core_sym::from_string("0.3500") );

   cur_rex_balance = get_balance( N(eosio.rex) );
   auto cur_rex_pool = get_rex_pool();

   BOOST_REQUIRE_EQUAL( cur_rex_balance, cur_rex_pool["total_unlent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( 0,               cur_rex_pool["total_lent"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( cur_rex_balance, cur_rex_pool["total_lendable"].as<asset>() );
   BOOST_REQUIRE_EQUAL( 0,               cur_rex_pool["namebid_proceeds"].as<asset>().get_amount() );

   // required for closing namebids
   active_and_vote_producers2();
   vote(N(proxyaccount), { name("tprodaaaaaaa") });
   produce_block( fc::days(14) );

   cur_rex_balance = get_balance( N(eosio.rex) );
   BOOST_REQUIRE_EQUAL( success(),                        bidname( carol, N(rndmbid), core_sym::from_string("23.7000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("23.7000"), get_balance( N(eosio.names) ) );
   BOOST_REQUIRE_EQUAL( success(),                        bidname( alice, N(rndmbid), core_sym::from_string("29.3500") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("29.3500"), get_balance( N(eosio.names) ));

   produce_block( fc::hours(24) );
   produce_blocks( 1000 );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("29.3500"), get_rex_pool()["namebid_proceeds"].as<asset>() );
   BOOST_REQUIRE_EQUAL( success(),                        deposit( frank, core_sym::from_string("5.0000") ) );
   BOOST_REQUIRE_EQUAL( success(),                        buyrex( frank, core_sym::from_string("5.0000") ) );
   BOOST_REQUIRE_EQUAL( get_balance( N(eosio.rex) ),      cur_rex_balance + core_sym::from_string("34.3500") );
   BOOST_REQUIRE_EQUAL( 0,                                get_balance( N(eosio.names) ).get_amount() );

   cur_rex_balance = get_balance( N(eosio.rex) );
   BOOST_REQUIRE_EQUAL( cur_rex_balance,                  get_rex_pool()["total_lendable"].as<asset>() );
   BOOST_REQUIRE_EQUAL( cur_rex_balance,                  get_rex_pool()["total_unlent"].as<asset>() );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( rex_maturity, eosio_system_tester ) try {

   const asset init_balance = core_sym::from_string("1000000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount) };
   account_name alice = accounts[0], bob = accounts[1];
   setup_rex_accounts( accounts, init_balance );

   const int64_t rex_ratio = 10000;
   const symbol  rex_sym( SY(4, REX) );
   {
      BOOST_REQUIRE_EQUAL( success(), buyrex( alice, core_sym::from_string("11.5000") ) );
      produce_block( fc::hours(3) );
      BOOST_REQUIRE_EQUAL( success(), buyrex( alice, core_sym::from_string("18.5000") ) );
      produce_block( fc::hours(25) );
      BOOST_REQUIRE_EQUAL( success(), buyrex( alice, core_sym::from_string("25.0000") ) );

      auto rex_balance = get_rex_balance_obj( alice );
      BOOST_REQUIRE_EQUAL( 550000 * rex_ratio, rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 0,                  rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( 2,                  rex_balance["rex_maturities"].get_array().size() );

      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( alice, asset::from_string("115000.0000 REX") ) );
      produce_block( fc::hours( 3*24 + 20) );
      BOOST_REQUIRE_EQUAL( success(),          sellrex( alice, asset::from_string("300000.0000 REX") ) );
      rex_balance = get_rex_balance_obj( alice );
      BOOST_REQUIRE_EQUAL( 250000 * rex_ratio, rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 0,                  rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( 1,                  rex_balance["rex_maturities"].get_array().size() );
      produce_block( fc::hours(23) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( alice, asset::from_string("250000.0000 REX") ) );
      produce_block( fc::days(1) );
      BOOST_REQUIRE_EQUAL( success(),          sellrex( alice, asset::from_string("130000.0000 REX") ) );
      rex_balance = get_rex_balance_obj( alice );
      BOOST_REQUIRE_EQUAL( 1200000000,         rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 1200000000,         rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( 0,                  rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( alice, asset::from_string("130000.0000 REX") ) );
      BOOST_REQUIRE_EQUAL( success(),          sellrex( alice, asset::from_string("120000.0000 REX") ) );
      rex_balance = get_rex_balance_obj( alice );
      BOOST_REQUIRE_EQUAL( 0,                  rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 0,                  rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( 0,                  rex_balance["rex_maturities"].get_array().size() );
   }

   {
      const asset payment1 = core_sym::from_string("14.8000");
      const asset payment2 = core_sym::from_string("15.2000");
      const asset payment  = payment1 + payment2;
      const asset rex_bucket( rex_ratio * payment.get_amount(), rex_sym );
      for ( uint8_t i = 0; i < 8; ++i ) {
         BOOST_REQUIRE_EQUAL( success(), buyrex( bob, payment1 ) );
         produce_block( fc::hours(2) );
         BOOST_REQUIRE_EQUAL( success(), buyrex( bob, payment2 ) );
         produce_block( fc::hours(24) );
      }

      auto rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 8 * rex_bucket.get_amount(), rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 5,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 3 * rex_bucket.get_amount(), rex_balance["matured_rex"].as<int64_t>() );

      BOOST_REQUIRE_EQUAL( success(),                   updaterex( bob ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 4,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 4 * rex_bucket.get_amount(), rex_balance["matured_rex"].as<int64_t>() );

      produce_block( fc::hours(2) );
      BOOST_REQUIRE_EQUAL( success(),                   updaterex( bob ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 4,                           rex_balance["rex_maturities"].get_array().size() );

      produce_block( fc::hours(1) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( bob, asset( 3 * rex_bucket.get_amount(), rex_sym ) ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 4,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( rex_bucket.get_amount(),     rex_balance["matured_rex"].as<int64_t>() );

      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( bob, asset( 2 * rex_bucket.get_amount(), rex_sym ) ) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( bob, asset( rex_bucket.get_amount(), rex_sym ) ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 4 * rex_bucket.get_amount(), rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 4,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );

      produce_block( fc::hours(23) );
      BOOST_REQUIRE_EQUAL( success(),                   updaterex( bob ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 3,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( rex_bucket.get_amount(),     rex_balance["matured_rex"].as<int64_t>() );

      BOOST_REQUIRE_EQUAL( success(),                   consolidate( bob ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 1,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );

      produce_block( fc::days(3) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( bob, asset( 4 * rex_bucket.get_amount(), rex_sym ) ) );
      produce_block( fc::hours(24 + 20) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( bob, asset( 4 * rex_bucket.get_amount(), rex_sym ) ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );
   }

   {
      const asset payment1 = core_sym::from_string("250000.0000");
      const asset payment2 = core_sym::from_string("10000.0000");
      const asset rex_bucket1( rex_ratio * payment1.get_amount(), rex_sym );
      const asset rex_bucket2( rex_ratio * payment2.get_amount(), rex_sym );
      const asset tot_rex = rex_bucket1 + rex_bucket2;

      BOOST_REQUIRE_EQUAL( success(), buyrex( bob, payment1 ) );
      produce_block( fc::days(3) );
      BOOST_REQUIRE_EQUAL( success(), buyrex( bob, payment2 ) );
      produce_block( fc::days(2) );
      BOOST_REQUIRE_EQUAL( success(), updaterex( bob ) );

      auto rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( tot_rex,                  rex_balance["rex_balance"].as<asset>() );
      BOOST_REQUIRE_EQUAL( rex_bucket1.get_amount(), rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( success(),                rentcpu( alice, alice, core_sym::from_string("8000.0000") ) );
      BOOST_REQUIRE_EQUAL( success(),                sellrex( bob, asset( rex_bucket1.get_amount() - 20, rex_sym ) ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( rex_bucket1.get_amount(), get_rex_order( bob )["rex_requested"].as<asset>().get_amount() + 20 );
      BOOST_REQUIRE_EQUAL( tot_rex,                  rex_balance["rex_balance"].as<asset>() );
      BOOST_REQUIRE_EQUAL( rex_bucket1.get_amount(), rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( success(),                consolidate( bob ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( rex_bucket1.get_amount(), rex_balance["matured_rex"].as<int64_t>() + 20 );
      BOOST_REQUIRE_EQUAL( success(),                cancelrexorder( bob ) );
      BOOST_REQUIRE_EQUAL( success(),                consolidate( bob ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 0,                        rex_balance["matured_rex"].as<int64_t>() );
   }

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( rex_savings, eosio_system_tester ) try {

   const asset init_balance = core_sym::from_string("100000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount), N(emilyaccount), N(frankaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2], emily = accounts[3], frank = accounts[4];
   setup_rex_accounts( accounts, init_balance );

   const int64_t rex_ratio = 10000;
   const symbol  rex_sym( SY(4, REX) );

   {
      const asset payment1 = core_sym::from_string("14.8000");
      const asset payment2 = core_sym::from_string("15.2000");
      const asset payment  = payment1 + payment2;
      const asset rex_bucket( rex_ratio * payment.get_amount(), rex_sym );
      for ( uint8_t i = 0; i < 8; ++i ) {
         BOOST_REQUIRE_EQUAL( success(), buyrex( alice, payment1 ) );
         produce_block( fc::hours(12) );
         BOOST_REQUIRE_EQUAL( success(), buyrex( alice, payment2 ) );
         produce_block( fc::hours(14) );
      }

      auto rex_balance = get_rex_balance_obj( alice );
      BOOST_REQUIRE_EQUAL( 8 * rex_bucket.get_amount(), rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 5,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 4 * rex_bucket.get_amount(), rex_balance["matured_rex"].as<int64_t>() );

      BOOST_REQUIRE_EQUAL( success(),                   mvtosavings( alice, asset( 8 * rex_bucket.get_amount(), rex_sym ) ) );
      rex_balance = get_rex_balance_obj( alice );
      BOOST_REQUIRE_EQUAL( 1,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );
      produce_block( fc::days(1000) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( alice, asset::from_string( "1.0000 REX" ) ) );
      BOOST_REQUIRE_EQUAL( success(),                   mvfrsavings( alice, asset::from_string( "10.0000 REX" ) ) );
      rex_balance = get_rex_balance_obj( alice );
      BOOST_REQUIRE_EQUAL( 2,                           rex_balance["rex_maturities"].get_array().size() );
      produce_block( fc::days(3) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( alice, asset::from_string( "1.0000 REX" ) ) );
      produce_blocks( 2 );
      produce_block( fc::days(2) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( alice, asset::from_string( "10.0001 REX" ) ) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( alice, asset::from_string( "10.0000 REX" ) ) );
      rex_balance = get_rex_balance_obj( alice );
      BOOST_REQUIRE_EQUAL( 1,                           rex_balance["rex_maturities"].get_array().size() );
      produce_block( fc::days(100) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( alice, asset::from_string( "0.0001 REX" ) ) );
      BOOST_REQUIRE_EQUAL( success(),                   mvfrsavings( alice, get_rex_balance( alice ) ) );
      produce_block( fc::days(5) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( alice, get_rex_balance( alice ) ) );
   }

   {
      const asset payment = core_sym::from_string("20.0000");
      const asset rex_bucket( rex_ratio * payment.get_amount(), rex_sym );
      for ( uint8_t i = 0; i < 5; ++i ) {
         produce_block( fc::hours(24) );
         BOOST_REQUIRE_EQUAL( success(), buyrex( bob, payment ) );
      }

      auto rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 5 * rex_bucket.get_amount(), rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 5,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( success(),                   mvtosavings( bob, asset( rex_bucket.get_amount() / 2, rex_sym ) ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 6,                           rex_balance["rex_maturities"].get_array().size() );

      BOOST_REQUIRE_EQUAL( success(),                   mvtosavings( bob, asset( rex_bucket.get_amount() / 2, rex_sym ) ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 5,                           rex_balance["rex_maturities"].get_array().size() );
      produce_block( fc::days(1) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( bob, rex_bucket ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 4,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( 4 * rex_bucket.get_amount(), rex_balance["rex_balance"].as<asset>().get_amount() );

      BOOST_REQUIRE_EQUAL( success(),                   mvtosavings( bob, asset( 3 * rex_bucket.get_amount() / 2, rex_sym ) ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 3,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( bob, rex_bucket ) );

      produce_block( fc::days(1) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( bob, rex_bucket ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 2,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( 3 * rex_bucket.get_amount(), rex_balance["rex_balance"].as<asset>().get_amount() );

      produce_block( fc::days(1) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( bob, rex_bucket ) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( bob, asset( rex_bucket.get_amount() / 2, rex_sym ) ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 1,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( 5 * rex_bucket.get_amount(), 2 * rex_balance["rex_balance"].as<asset>().get_amount() );

      produce_block( fc::days(20) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( bob, rex_bucket ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient REX in savings"),
                           mvfrsavings( bob, asset( 3 * rex_bucket.get_amount(), rex_sym ) ) );
      BOOST_REQUIRE_EQUAL( success(),                   mvfrsavings( bob, rex_bucket ) );
      BOOST_REQUIRE_EQUAL( 2,                           get_rex_balance_obj( bob )["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient REX balance"),
                           mvtosavings( bob, asset( 3 * rex_bucket.get_amount() / 2, rex_sym ) ) );
      produce_block( fc::days(1) );
      BOOST_REQUIRE_EQUAL( success(),                   mvfrsavings( bob, rex_bucket ) );
      BOOST_REQUIRE_EQUAL( 3,                           get_rex_balance_obj( bob )["rex_maturities"].get_array().size() );
      produce_block( fc::days(4) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( bob, rex_bucket ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( bob, rex_bucket ) );
      produce_block( fc::days(1) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( bob, rex_bucket ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 1,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( rex_bucket.get_amount() / 2, rex_balance["rex_balance"].as<asset>().get_amount() );

      BOOST_REQUIRE_EQUAL( success(),                   mvfrsavings( bob, asset( rex_bucket.get_amount() / 4, rex_sym ) ) );
      produce_block( fc::days(2) );
      BOOST_REQUIRE_EQUAL( success(),                   mvfrsavings( bob, asset( rex_bucket.get_amount() / 8, rex_sym ) ) );
      BOOST_REQUIRE_EQUAL( 3,                           get_rex_balance_obj( bob )["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( success(),                   consolidate( bob ) );
      BOOST_REQUIRE_EQUAL( 2,                           get_rex_balance_obj( bob )["rex_maturities"].get_array().size() );

      produce_block( fc::days(5) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient available rex"),
                           sellrex( bob, asset( rex_bucket.get_amount() / 2, rex_sym ) ) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( bob, asset( 3 * rex_bucket.get_amount() / 8, rex_sym ) ) );
      rex_balance = get_rex_balance_obj( bob );
      BOOST_REQUIRE_EQUAL( 1,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );
      BOOST_REQUIRE_EQUAL( rex_bucket.get_amount() / 8, rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( success(),                   mvfrsavings( bob, get_rex_balance( bob ) ) );
      produce_block( fc::days(5) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( bob, get_rex_balance( bob ) ) );
   }

   {
      const asset payment = core_sym::from_string("40000.0000");
      const int64_t rex_bucket_amount = rex_ratio * payment.get_amount();
      const asset rex_bucket( rex_bucket_amount, rex_sym );
      BOOST_REQUIRE_EQUAL( success(),  buyrex( alice, payment ) );
      BOOST_REQUIRE_EQUAL( rex_bucket, get_rex_balance( alice ) );
      BOOST_REQUIRE_EQUAL( rex_bucket, get_rex_pool()["total_rex"].as<asset>() );

      produce_block( fc::days(5) );

      BOOST_REQUIRE_EQUAL( success(),                       rentcpu( bob, bob, core_sym::from_string("2000.0000") ) );
      BOOST_REQUIRE_EQUAL( success(),                       sellrex( alice, asset( 9 * rex_bucket_amount / 10, rex_sym ) ) );
      BOOST_REQUIRE_EQUAL( rex_bucket,                      get_rex_balance( alice ) );
      BOOST_REQUIRE_EQUAL( success(),                       mvtosavings( alice, asset( rex_bucket_amount / 10, rex_sym ) ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient REX balance"),
                           mvtosavings( alice, asset( rex_bucket_amount / 10, rex_sym ) ) );
      BOOST_REQUIRE_EQUAL( success(),                       cancelrexorder( alice ) );
      BOOST_REQUIRE_EQUAL( success(),                       mvtosavings( alice, asset( rex_bucket_amount / 10, rex_sym ) ) );
      auto rb = get_rex_balance_obj( alice );
      BOOST_REQUIRE_EQUAL( rb["matured_rex"].as<int64_t>(), 8 * rex_bucket_amount / 10 );
      BOOST_REQUIRE_EQUAL( success(),                       mvfrsavings( alice, asset( 2 * rex_bucket_amount / 10, rex_sym ) ) );
      produce_block( fc::days(31) );
      BOOST_REQUIRE_EQUAL( success(),                       sellrex( alice, get_rex_balance( alice ) ) );
   }

   {
      const asset   payment                = core_sym::from_string("250.0000");
      const asset   half_payment           = core_sym::from_string("125.0000");
      const int64_t rex_bucket_amount      = rex_ratio * payment.get_amount();
      const int64_t half_rex_bucket_amount = rex_bucket_amount / 2;
      const asset   rex_bucket( rex_bucket_amount, rex_sym );
      const asset   half_rex_bucket( half_rex_bucket_amount, rex_sym );

      BOOST_REQUIRE_EQUAL( success(),                   buyrex( carol, payment ) );
      BOOST_REQUIRE_EQUAL( rex_bucket,                  get_rex_balance( carol ) );
      auto rex_balance = get_rex_balance_obj( carol );

      BOOST_REQUIRE_EQUAL( 1,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );
      produce_block( fc::days(1) );
      BOOST_REQUIRE_EQUAL( success(),                   buyrex( carol, payment ) );
      rex_balance = get_rex_balance_obj( carol );
      BOOST_REQUIRE_EQUAL( 2,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 0,                           rex_balance["matured_rex"].as<int64_t>() );

      BOOST_REQUIRE_EQUAL( success(),                   mvtosavings( carol, half_rex_bucket ) );
      rex_balance = get_rex_balance_obj( carol );
      BOOST_REQUIRE_EQUAL( 3,                           rex_balance["rex_maturities"].get_array().size() );

      BOOST_REQUIRE_EQUAL( success(),                   buyrex( carol, half_payment ) );
      rex_balance = get_rex_balance_obj( carol );
      BOOST_REQUIRE_EQUAL( 3,                           rex_balance["rex_maturities"].get_array().size() );

      produce_block( fc::days(5) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("asset must be a positive amount of (REX, 4)"),
                           mvfrsavings( carol, asset::from_string("0.0000 REX") ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("asset must be a positive amount of (REX, 4)"),
                           mvfrsavings( carol, asset::from_string("1.0000 RND") ) );
      BOOST_REQUIRE_EQUAL( success(),                   mvfrsavings( carol, half_rex_bucket ) );
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient REX in savings"),
                           mvfrsavings( carol, asset::from_string("0.0001 REX") ) );
      rex_balance = get_rex_balance_obj( carol );
      BOOST_REQUIRE_EQUAL( 1,                           rex_balance["rex_maturities"].get_array().size() );
      BOOST_REQUIRE_EQUAL( 5 * half_rex_bucket_amount,  rex_balance["rex_balance"].as<asset>().get_amount() );
      BOOST_REQUIRE_EQUAL( 2 * rex_bucket_amount,       rex_balance["matured_rex"].as<int64_t>() );
      produce_block( fc::days(5) );
      BOOST_REQUIRE_EQUAL( success(),                   sellrex( carol, get_rex_balance( carol) ) );
   }

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( update_rex, eosio_system_tester, * boost::unit_test::tolerance(1e-10) ) try {

   const asset init_balance = core_sym::from_string("30000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount), N(emilyaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2], emily = accounts[3];
   setup_rex_accounts( accounts, init_balance );

   const int64_t init_stake = get_voter_info( alice )["staked"].as<int64_t>();
   

   const asset payment = core_sym::from_string("25000.0000");
   BOOST_REQUIRE_EQUAL( success(),                              buyrex( alice, payment ) );
   BOOST_REQUIRE_EQUAL( payment,                                get_rex_vote_stake(alice) );
   BOOST_REQUIRE_EQUAL( get_rex_vote_stake(alice).get_amount(), get_voter_info(alice)["staked"].as<int64_t>() - init_stake );

   const asset fee = core_sym::from_string("50.0000");
   BOOST_REQUIRE_EQUAL( success(),                              rentcpu( emily, bob, fee ) );
   BOOST_REQUIRE_EQUAL( success(),                              updaterex( alice ) );
   BOOST_REQUIRE_EQUAL( payment + fee,                          get_rex_vote_stake(alice) );
   BOOST_REQUIRE_EQUAL( get_rex_vote_stake(alice).get_amount(), get_voter_info( alice )["staked"].as<int64_t>() - init_stake );

   // create accounts {tprodaaaaaaa, tprodaaaaaab, ..., tprodaaaaaaz} and register as producers
   std::vector<account_name> producer_names;
   {
      
      producer_names.reserve(30);
      const std::string root("tprod");
      for(uint8_t i = 0; i < 30; i++) {
         name p = name(root + toBase31(i));
         producer_names.emplace_back(p);
      }

      setup_producer_accounts(producer_names);
      for ( const auto& p: producer_names ) {
         BOOST_REQUIRE_EQUAL( success(), regproducer(p) );
         BOOST_TEST_REQUIRE( 0 == get_producer_info(p)["total_votes"].as<double>() );
      }
      std::sort(producer_names.begin(), producer_names.end());
   }

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("voter holding REX tokens must vote for at least 21 producers or for a proxy"),
                        vote( alice, std::vector<account_name>(producer_names.begin(), producer_names.begin() + 20) ) );
   BOOST_REQUIRE_EQUAL( success(),
                        vote( alice, std::vector<account_name>(producer_names.begin(), producer_names.begin() + 30) ) );

   BOOST_TEST_REQUIRE( stake2votes( asset( get_voter_info( alice )["staked"].as<int64_t>(), symbol{CORE_SYM} ) )
                       == get_producer_info(producer_names[0])["total_votes"].as<double>() );
   BOOST_TEST_REQUIRE( stake2votes( asset( get_voter_info( alice )["staked"].as<int64_t>(), symbol{CORE_SYM} ) )
                       == get_producer_info(producer_names[20])["total_votes"].as<double>() );

   BOOST_REQUIRE_EQUAL( success(), updaterex( alice ) );
   produce_block( fc::days(20) );

   //NOTE: TELOS doesn't have "vote decay", this test has been rewritten to reflect telos voting logic. 
   BOOST_TEST_REQUIRE( get_producer_info(producer_names[20])["total_votes"].as<double>()
                       == stake2votes( asset( get_voter_info( alice )["staked"].as<int64_t>(), symbol{CORE_SYM} ) ) );
   BOOST_REQUIRE_EQUAL( success(), updaterex( alice ) );
   BOOST_TEST_REQUIRE( stake2votes( asset( get_voter_info( alice )["staked"].as<int64_t>(), symbol{CORE_SYM} ) )
                       == get_producer_info(producer_names[20])["total_votes"].as<double>() );

   const asset   init_rex             = get_rex_balance( alice );
   const auto    current_rex_pool     = get_rex_pool();
   const int64_t total_lendable       = current_rex_pool["total_lendable"].as<asset>().get_amount();
   const int64_t total_rex            = current_rex_pool["total_rex"].as<asset>().get_amount();
   const int64_t init_alice_rex_stake = ( eosio::chain::uint128_t(init_rex.get_amount()) * total_lendable ) / total_rex;
   produce_block( fc::days(5) );
   const asset rex_sell_amount( get_rex_balance(alice).get_amount() / 4, symbol( SY(4,REX) ) );
   BOOST_REQUIRE_EQUAL( success(),                                       sellrex( alice, rex_sell_amount ) );
   BOOST_REQUIRE_EQUAL( init_rex,                                        get_rex_balance( alice ) + rex_sell_amount );
   BOOST_REQUIRE_EQUAL( 3 * init_alice_rex_stake,                        4 * get_rex_vote_stake( alice ).get_amount() );
   BOOST_REQUIRE_EQUAL( get_voter_info( alice )["staked"].as<int64_t>(), init_stake + get_rex_vote_stake(alice).get_amount() );
   BOOST_TEST_REQUIRE( stake2votes( asset( get_voter_info( alice )["staked"].as<int64_t>(), symbol{CORE_SYM} ) )
                       == get_producer_info(producer_names[0])["total_votes"].as<double>() );

   produce_block( fc::days(31) );
   BOOST_REQUIRE_EQUAL( success(), sellrex( alice, get_rex_balance( alice ) ) );
   BOOST_REQUIRE_EQUAL( 0,         get_rex_balance( alice ).get_amount() );
   BOOST_REQUIRE_EQUAL( success(), vote( alice, { producer_names[0], producer_names[4] } ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must vote for at least 21 producers or for a proxy before buying REX"),
                        buyrex( alice, core_sym::from_string("1.0000") ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( update_rex_vote, eosio_system_tester, * boost::unit_test::tolerance(1e-10) ) try {

   activate_network();

   // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
   std::vector<account_name> producer_names;
   {
      producer_names.reserve(30);
      const std::string root("tprod");
      for(uint8_t i = 0; i < 30; i++) {
         name p = name(root + toBase31(i));
         producer_names.emplace_back(p);
      }

      setup_producer_accounts(producer_names);
      for ( const auto& p: producer_names ) {
         BOOST_REQUIRE_EQUAL( success(), regproducer(p) );
         BOOST_TEST_REQUIRE( 0 == get_producer_info(p)["total_votes"].as<double>() );
      }
      std::sort(producer_names.begin(), producer_names.end());
   }

   const asset init_balance = core_sym::from_string("30000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount), N(emilyaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2], emily = accounts[3];
   setup_rex_accounts( accounts, init_balance );

   const int64_t init_stake_amount = get_voter_info( alice )["staked"].as<int64_t>();
   const asset init_stake( init_stake_amount, symbol{CORE_SYM} );

   const asset purchase = core_sym::from_string("25000.0000");
   BOOST_REQUIRE_EQUAL( success(),                              buyrex( alice, purchase ) );
   BOOST_REQUIRE_EQUAL( purchase,                               get_rex_pool()["total_lendable"].as<asset>() );
   BOOST_REQUIRE_EQUAL( purchase,                               get_rex_vote_stake(alice) );
   BOOST_REQUIRE_EQUAL( get_rex_vote_stake(alice).get_amount(), get_voter_info(alice)["staked"].as<int64_t>() - init_stake_amount );
   BOOST_REQUIRE_EQUAL( purchase,                               get_rex_pool()["total_lendable"].as<asset>() );

   BOOST_REQUIRE_EQUAL( success(),
                        vote( alice, std::vector<account_name>(producer_names.begin(), producer_names.begin() + 30) ) );
   BOOST_REQUIRE_EQUAL( purchase,                               get_rex_vote_stake(alice) );
   BOOST_REQUIRE_EQUAL( purchase.get_amount(),                  get_voter_info(alice)["staked"].as<int64_t>() - init_stake_amount );

   const auto init_rex_pool = get_rex_pool();
   const asset rent = core_sym::from_string("25.0000");
   BOOST_REQUIRE_EQUAL( success(),                              rentcpu( emily, bob, rent ) );
   const auto curr_rex_pool = get_rex_pool();
   BOOST_REQUIRE_EQUAL( curr_rex_pool["total_lendable"].as<asset>(), init_rex_pool["total_lendable"].as<asset>() + rent );
   BOOST_REQUIRE_EQUAL( success(),
                        vote( alice, std::vector<account_name>(producer_names.begin(), producer_names.begin() + 30) ) );
   BOOST_REQUIRE_EQUAL( (purchase + rent).get_amount(),         get_voter_info(alice)["staked"].as<int64_t>() - init_stake_amount );
   BOOST_REQUIRE_EQUAL( purchase + rent,                        get_rex_vote_stake(alice) );
   BOOST_TEST_REQUIRE ( stake2votes(purchase + rent + init_stake) ==
                        get_producer_info(producer_names[0])["total_votes"].as_double() );
   BOOST_TEST_REQUIRE ( stake2votes(purchase + rent + init_stake) ==
                        get_producer_info(producer_names[20])["total_votes"].as_double() );

   const asset to_net_stake = core_sym::from_string("60.0000");
   const asset to_cpu_stake = core_sym::from_string("40.0000");
   transfer( config::system_account_name, alice, to_net_stake + to_cpu_stake, config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(),                              rentcpu( emily, bob, rent ) );
   BOOST_REQUIRE_EQUAL( success(),                              stake( alice, alice, to_net_stake, to_cpu_stake ) );
   BOOST_REQUIRE_EQUAL( purchase + rent + rent,                 get_rex_vote_stake(alice) );
   BOOST_TEST_REQUIRE ( stake2votes(init_stake + purchase + rent + rent + to_net_stake + to_cpu_stake) ==
                        get_producer_info(producer_names[0])["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( success(),                              rentcpu( emily, bob, rent ) );
   BOOST_REQUIRE_EQUAL( success(),                              unstake( alice, alice, to_net_stake, to_cpu_stake ) );
   BOOST_REQUIRE_EQUAL( purchase + rent + rent + rent,          get_rex_vote_stake(alice) );
   BOOST_TEST_REQUIRE ( stake2votes(init_stake + purchase + rent + rent + rent) ==
                        get_producer_info(producer_names[0])["total_votes"].as_double() );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( deposit_rex_fund, eosio_system_tester ) try {

   const asset init_balance = core_sym::from_string("1000.0000");
   const asset init_net     = core_sym::from_string("70.0000");
   const asset init_cpu     = core_sym::from_string("90.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount) };
   account_name alice = accounts[0], bob = accounts[1];
   setup_rex_accounts( accounts, init_balance, init_net, init_cpu, false );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"),                   get_rex_fund( alice ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must deposit to REX fund first"), withdraw( alice, core_sym::from_string("0.0001") ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("overdrawn balance"),              deposit( alice, init_balance + init_balance ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must deposit core token"),        deposit( alice, asset::from_string("1.0000 RNDM") ) );

   asset deposit_quant( init_balance.get_amount() / 5, init_balance.get_symbol() );
   BOOST_REQUIRE_EQUAL( success(),                             deposit( alice, deposit_quant ) );
   BOOST_REQUIRE_EQUAL( get_balance( alice ),                  init_balance - deposit_quant );
   BOOST_REQUIRE_EQUAL( get_rex_fund( alice ),                 deposit_quant );
   BOOST_REQUIRE_EQUAL( success(),                             deposit( alice, deposit_quant ) );
   BOOST_REQUIRE_EQUAL( get_rex_fund( alice ),                 deposit_quant + deposit_quant );
   BOOST_REQUIRE_EQUAL( get_balance( alice ),                  init_balance - deposit_quant - deposit_quant );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient funds"), withdraw( alice, get_rex_fund( alice ) + core_sym::from_string("0.0001")) );
   BOOST_REQUIRE_EQUAL( success(),                             withdraw( alice, deposit_quant ) );
   BOOST_REQUIRE_EQUAL( get_rex_fund( alice ),                 deposit_quant );
   BOOST_REQUIRE_EQUAL( get_balance( alice ),                  init_balance - deposit_quant );
   BOOST_REQUIRE_EQUAL( success(),                             withdraw( alice, get_rex_fund( alice ) ) );
   BOOST_REQUIRE_EQUAL( get_rex_fund( alice ).get_amount(),    0 );
   BOOST_REQUIRE_EQUAL( get_balance( alice ),                  init_balance );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( rex_lower_bound, eosio_system_tester ) try {

   const asset init_balance = core_sym::from_string("25000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount) };
   account_name alice = accounts[0], bob = accounts[1];
   setup_rex_accounts( accounts, init_balance );
   const symbol rex_sym( SY(4, REX) );

   const asset payment = core_sym::from_string("25000.0000");
   BOOST_REQUIRE_EQUAL( success(), buyrex( alice, payment ) );
   const asset fee = core_sym::from_string("25.0000");
   BOOST_REQUIRE_EQUAL( success(), rentcpu( bob, bob, fee ) );

   const auto rex_pool = get_rex_pool();
   const int64_t tot_rex      = rex_pool["total_rex"].as<asset>().get_amount();
   const int64_t tot_unlent   = rex_pool["total_unlent"].as<asset>().get_amount();
   const int64_t tot_lent     = rex_pool["total_lent"].as<asset>().get_amount();
   const int64_t tot_lendable = rex_pool["total_lendable"].as<asset>().get_amount();
   double rex_per_eos = double(tot_rex) / double(tot_lendable);
   int64_t sell_amount = rex_per_eos * ( tot_unlent - 0.19 * tot_lent );
   produce_block( fc::days(5) );
   BOOST_REQUIRE_EQUAL( success(), sellrex( alice, asset( sell_amount, rex_sym ) ) );
   BOOST_REQUIRE_EQUAL( success(), cancelrexorder( alice ) );
   sell_amount = rex_per_eos * ( tot_unlent - 0.2 * tot_lent );
   BOOST_REQUIRE_EQUAL( success(), sellrex( alice, asset( sell_amount, rex_sym ) ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("no sellrex order is scheduled"),
                        cancelrexorder( alice ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( close_rex, eosio_system_tester ) try {

   const asset init_balance = core_sym::from_string("25000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount), N(carolaccount), N(emilyaccount) };
   account_name alice = accounts[0], bob = accounts[1], carol = accounts[2], emily = accounts[3];
   setup_rex_accounts( accounts, init_balance );

   BOOST_REQUIRE_EQUAL( true,              !get_rex_fund_obj( alice ).is_null() );
   BOOST_REQUIRE_EQUAL( init_balance,      get_rex_fund( alice ) );
   BOOST_REQUIRE_EQUAL( success(),         closerex( alice ) );
   BOOST_REQUIRE_EQUAL( success(),         withdraw( alice, init_balance ) );
   BOOST_REQUIRE_EQUAL( success(),         closerex( alice ) );
   BOOST_REQUIRE_EQUAL( true,              get_rex_fund_obj( alice ).is_null() );
   BOOST_REQUIRE_EQUAL( success(),         deposit( alice, init_balance ) );
   BOOST_REQUIRE_EQUAL( true,              !get_rex_fund_obj( alice ).is_null() );

   BOOST_REQUIRE_EQUAL( true,              get_rex_balance_obj( bob ).is_null() );
   BOOST_REQUIRE_EQUAL( success(),         buyrex( bob, init_balance ) );
   BOOST_REQUIRE_EQUAL( true,              !get_rex_balance_obj( bob ).is_null() );
   BOOST_REQUIRE_EQUAL( true,              !get_rex_fund_obj( bob ).is_null() );
   BOOST_REQUIRE_EQUAL( 0,                 get_rex_fund( bob ).get_amount() );
   BOOST_REQUIRE_EQUAL( closerex( bob ),   wasm_assert_msg("account has remaining REX balance, must sell first") );
   produce_block( fc::days(5) );
   BOOST_REQUIRE_EQUAL( success(),         sellrex( bob, get_rex_balance( bob ) ) );
   BOOST_REQUIRE_EQUAL( success(),         closerex( bob ) );
   BOOST_REQUIRE_EQUAL( success(),         withdraw( bob, get_rex_fund( bob ) ) );
   BOOST_REQUIRE_EQUAL( success(),         closerex( bob ) );
   BOOST_REQUIRE_EQUAL( true,              get_rex_balance_obj( bob ).is_null() );
   BOOST_REQUIRE_EQUAL( true,              get_rex_fund_obj( bob ).is_null() );

   BOOST_REQUIRE_EQUAL( success(),         deposit( bob, init_balance ) );
   BOOST_REQUIRE_EQUAL( success(),         buyrex( bob, init_balance ) );

   const asset fee = core_sym::from_string("1.0000");
   BOOST_REQUIRE_EQUAL( success(),         rentcpu( carol, emily, fee ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient funds"),
                        withdraw( carol, init_balance ) );
   BOOST_REQUIRE_EQUAL( success(),         withdraw( carol, init_balance - fee ) );

   produce_block( fc::days(20) );

   BOOST_REQUIRE_EQUAL( success(),         closerex( carol ) );
   BOOST_REQUIRE_EQUAL( true,              !get_rex_fund_obj( carol ).is_null() );

   produce_block( fc::days(10) );

   BOOST_REQUIRE_EQUAL( success(),         closerex( carol ) );
   BOOST_REQUIRE_EQUAL( true,              get_rex_balance_obj( carol ).is_null() );
   BOOST_REQUIRE_EQUAL( true,              get_rex_fund_obj( carol ).is_null() );

   BOOST_REQUIRE_EQUAL( success(),         rentnet( emily, emily, fee ) );
   BOOST_REQUIRE_EQUAL( true,              !get_rex_fund_obj( emily ).is_null() );
   BOOST_REQUIRE_EQUAL( success(),         closerex( emily ) );
   BOOST_REQUIRE_EQUAL( true,              !get_rex_fund_obj( emily ).is_null() );

   BOOST_REQUIRE_EQUAL( success(),         sellrex( bob, get_rex_balance( bob ) ) );
   BOOST_REQUIRE_EQUAL( closerex( bob ),   wasm_assert_msg("account has remaining REX balance, must sell first") );

   produce_block( fc::days(30) );

   BOOST_REQUIRE_EQUAL( closerex( bob ),   success() );
   BOOST_REQUIRE      ( 0 <                get_rex_fund( bob ).get_amount() );
   BOOST_REQUIRE_EQUAL( success(),         withdraw( bob, get_rex_fund( bob ) ) );
   BOOST_REQUIRE_EQUAL( success(),         closerex( bob ) );
   BOOST_REQUIRE_EQUAL( true,              get_rex_balance_obj( bob ).is_null() );
   BOOST_REQUIRE_EQUAL( true,              get_rex_fund_obj( bob ).is_null() );

   BOOST_REQUIRE_EQUAL( 0,                 get_rex_pool()["total_rex"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( 0,                 get_rex_pool()["total_lendable"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("rex loans are currently not available"),
                        rentcpu( emily, emily, core_sym::from_string("1.0000") ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( set_rex, eosio_system_tester ) try {

   const asset init_balance = core_sym::from_string("25000.0000");
   const std::vector<account_name> accounts = { N(aliceaccount), N(bobbyaccount) };
   account_name alice = accounts[0], bob = accounts[1];
   setup_rex_accounts( accounts, init_balance );

   const name act_name{ N(setrex) };
   const asset init_total_rent  = core_sym::from_string("20000.0000");
   const asset set_total_rent   = core_sym::from_string("10000.0000");
   const asset negative_balance = core_sym::from_string("-10000.0000");
   const asset different_symbol = asset::from_string("10000.0000 RND");
   BOOST_REQUIRE_EQUAL( error("missing authority of eosio"),
                        push_action( alice, act_name, mvo()("balance", set_total_rent) ) );
   BOOST_REQUIRE_EQUAL( error("missing authority of eosio"),
                        push_action( bob, act_name, mvo()("balance", set_total_rent) ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("rex system is not initialized"),
                        push_action( config::system_account_name, act_name, mvo()("balance", set_total_rent) ) );
   BOOST_REQUIRE_EQUAL( success(), buyrex( alice, init_balance ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("balance must be set to have a positive amount"),
                        push_action( config::system_account_name, act_name, mvo()("balance", negative_balance) ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("balance symbol must be core symbol"),
                        push_action( config::system_account_name, act_name, mvo()("balance", different_symbol) ) );
   const asset fee = core_sym::from_string("100.0000");
   BOOST_REQUIRE_EQUAL( success(),             rentcpu( bob, bob, fee ) );
   const auto& init_rex_pool = get_rex_pool();
   BOOST_REQUIRE_EQUAL( init_total_rent + fee, init_rex_pool["total_rent"].as<asset>() );
   BOOST_TEST_REQUIRE( set_total_rent != init_rex_pool["total_rent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( success(),
                        push_action( config::system_account_name, act_name, mvo()("balance", set_total_rent) ) );
   const auto& curr_rex_pool = get_rex_pool();
   BOOST_REQUIRE_EQUAL( init_rex_pool["total_lendable"].as<asset>(),   curr_rex_pool["total_lendable"].as<asset>() );
   BOOST_REQUIRE_EQUAL( init_rex_pool["total_lent"].as<asset>(),       curr_rex_pool["total_lent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( init_rex_pool["total_unlent"].as<asset>(),     curr_rex_pool["total_unlent"].as<asset>() );
   BOOST_REQUIRE_EQUAL( init_rex_pool["namebid_proceeds"].as<asset>(), curr_rex_pool["namebid_proceeds"].as<asset>() );
   BOOST_REQUIRE_EQUAL( init_rex_pool["loan_num"].as_uint64(),         curr_rex_pool["loan_num"].as_uint64() );
   BOOST_REQUIRE_EQUAL( set_total_rent,                                curr_rex_pool["total_rent"].as<asset>() );

} FC_LOG_AND_RETHROW()

//NOTE: Removed the b1_vesting unit test because it does not apply to the telos version of the system contract.

BOOST_AUTO_TEST_CASE( setabi_bios ) try {
   validating_tester t( validating_tester::default_config() );
   t.execute_setup_policy( setup_policy::preactivate_feature_only );
   abi_serializer abi_ser(fc::json::from_string( (const char*)contracts::bios_abi().data()).template as<abi_def>(), base_tester::abi_serializer_max_time);
   t.set_code( config::system_account_name, contracts::bios_wasm() );
   t.set_abi( config::system_account_name, contracts::bios_abi().data() );
   t.create_account(N(eosio.token));
   t.set_abi( N(eosio.token), contracts::token_abi().data() );
   {
      auto res = t.get_row_by_account( config::system_account_name, config::system_account_name, N(abihash), N(eosio.token) );
      _abi_hash abi_hash;
      auto abi_hash_var = abi_ser.binary_to_variant( "abi_hash", res, base_tester::abi_serializer_max_time );
      abi_serializer::from_variant( abi_hash_var, abi_hash, t.get_resolver(), base_tester::abi_serializer_max_time);
      auto abi = fc::raw::pack(fc::json::from_string( (const char*)contracts::token_abi().data()).template as<abi_def>());
      auto result = fc::sha256::hash( (const char*)abi.data(), abi.size() );

      BOOST_REQUIRE( abi_hash.hash == result );
   }

   t.set_abi( N(eosio.token), contracts::system_abi().data() );
   {
      auto res = t.get_row_by_account( config::system_account_name, config::system_account_name, N(abihash), N(eosio.token) );
      _abi_hash abi_hash;
      auto abi_hash_var = abi_ser.binary_to_variant( "abi_hash", res, base_tester::abi_serializer_max_time );
      abi_serializer::from_variant( abi_hash_var, abi_hash, t.get_resolver(), base_tester::abi_serializer_max_time);
      auto abi = fc::raw::pack(fc::json::from_string( (const char*)contracts::system_abi().data()).template as<abi_def>());
      auto result = fc::sha256::hash( (const char*)abi.data(), abi.size() );

      BOOST_REQUIRE( abi_hash.hash == result );
   }
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( setabi, eosio_system_tester ) try {
   set_abi( N(eosio.token), contracts::token_abi().data() );
   {
      auto res = get_row_by_account( config::system_account_name, config::system_account_name, N(abihash), N(eosio.token) );
      _abi_hash abi_hash;
      auto abi_hash_var = abi_ser.binary_to_variant( "abi_hash", res, abi_serializer_max_time );
      abi_serializer::from_variant( abi_hash_var, abi_hash, get_resolver(), abi_serializer_max_time);
      auto abi = fc::raw::pack(fc::json::from_string( (const char*)contracts::token_abi().data()).template as<abi_def>());
      auto result = fc::sha256::hash( (const char*)abi.data(), abi.size() );

      BOOST_REQUIRE( abi_hash.hash == result );
   }

   set_abi( N(eosio.token), contracts::system_abi().data() );
   {
      auto res = get_row_by_account( config::system_account_name, config::system_account_name, N(abihash), N(eosio.token) );
      _abi_hash abi_hash;
      auto abi_hash_var = abi_ser.binary_to_variant( "abi_hash", res, abi_serializer_max_time );
      abi_serializer::from_variant( abi_hash_var, abi_hash, get_resolver(), abi_serializer_max_time);
      auto abi = fc::raw::pack(fc::json::from_string( (const char*)contracts::system_abi().data()).template as<abi_def>());
      auto result = fc::sha256::hash( (const char*)abi.data(), abi.size() );

      BOOST_REQUIRE( abi_hash.hash == result );
   }

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( change_limited_account_back_to_unlimited, eosio_system_tester ) try {
   BOOST_REQUIRE( get_total_stake( "eosio" ).is_null() );

   transfer( N(eosio), N(alice1111111), core_sym::from_string("1.0000") );

   auto error_msg = stake( N(alice1111111), N(eosio), core_sym::from_string("0.0000"), core_sym::from_string("1.0000") );
   auto semicolon_pos = error_msg.find(';');

   BOOST_REQUIRE_EQUAL( error("account eosio has insufficient ram"),
                        error_msg.substr(0, semicolon_pos) );

   int64_t ram_bytes_needed = 0;
   {
      std::istringstream s( error_msg );
      s.seekg( semicolon_pos + 7, std::ios_base::beg );
      s >> ram_bytes_needed;
      ram_bytes_needed += 256; // enough room to cover total_resources_table
   }

   push_action( N(eosio), N(setalimits), mvo()
                                          ("account", "eosio")
                                          ("ram_bytes", ram_bytes_needed)
                                          ("net_weight", -1)
                                          ("cpu_weight", -1)
              );

   stake( N(alice1111111), N(eosio), core_sym::from_string("0.0000"), core_sym::from_string("1.0000") );

   REQUIRE_MATCHING_OBJECT( get_total_stake( "eosio" ), mvo()
      ("owner", "eosio")
      ("net_weight", core_sym::from_string("0.0000"))
      ("cpu_weight", core_sym::from_string("1.0000"))
      ("ram_bytes",  0)
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "only supports unlimited accounts" ),
                        push_action( N(eosio), N(setalimits), mvo()
                                          ("account", "eosio")
                                          ("ram_bytes", ram_bytes_needed)
                                          ("net_weight", -1)
                                          ("cpu_weight", -1)
                        )
   );

   BOOST_REQUIRE_EQUAL( error( "transaction net usage is too high: 128 > 0" ),
                        push_action( N(eosio), N(setalimits), mvo()
                           ("account", "eosio.saving")
                           ("ram_bytes", -1)
                           ("net_weight", -1)
                           ("cpu_weight", -1)
                        )
   );

   BOOST_REQUIRE_EQUAL( success(),
                        push_action( N(eosio), N(setacctnet), mvo()
                           ("account", "eosio")
                           ("net_weight", -1)
                        )
   );

   BOOST_REQUIRE_EQUAL( success(),
                        push_action( N(eosio), N(setacctcpu), mvo()
                           ("account", "eosio")
                           ("cpu_weight", -1)

                        )
   );

   BOOST_REQUIRE_EQUAL( success(),
                        push_action( N(eosio), N(setalimits), mvo()
                                          ("account", "eosio.saving")
                                          ("ram_bytes", ram_bytes_needed)
                                          ("net_weight", -1)
                                          ("cpu_weight", -1)
                        )
   );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( buy_pin_sell_ram, eosio_system_tester ) try {
   BOOST_REQUIRE( get_total_stake( "eosio" ).is_null() );

   transfer( N(eosio), N(alice1111111), core_sym::from_string("1020.0000") );

   auto error_msg = stake( N(alice1111111), N(eosio), core_sym::from_string("10.0000"), core_sym::from_string("10.0000") );
   auto semicolon_pos = error_msg.find(';');

   BOOST_REQUIRE_EQUAL( error("account eosio has insufficient ram"),
                        error_msg.substr(0, semicolon_pos) );

   int64_t ram_bytes_needed = 0;
   {
      std::istringstream s( error_msg );
      s.seekg( semicolon_pos + 7, std::ios_base::beg );
      s >> ram_bytes_needed;
      ram_bytes_needed += ram_bytes_needed/10; // enough buffer to make up for buyrambytes estimation errors
   }

   auto alice_original_balance = get_balance( N(alice1111111) );

   BOOST_REQUIRE_EQUAL( success(), buyrambytes( N(alice1111111), N(eosio), static_cast<uint32_t>(ram_bytes_needed) ) );

   auto tokens_paid_for_ram = alice_original_balance - get_balance( N(alice1111111) );

   auto total_res = get_total_stake( "eosio" );

   REQUIRE_MATCHING_OBJECT( total_res, mvo()
      ("owner", "eosio")
      ("net_weight", core_sym::from_string("0.0000"))
      ("cpu_weight", core_sym::from_string("0.0000"))
      ("ram_bytes",  total_res["ram_bytes"].as_int64() )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "only supports unlimited accounts" ),
                        push_action( N(eosio), N(setalimits), mvo()
                                          ("account", "eosio")
                                          ("ram_bytes", ram_bytes_needed)
                                          ("net_weight", -1)
                                          ("cpu_weight", -1)
                        )
   );

   BOOST_REQUIRE_EQUAL( success(),
                        push_action( N(eosio), N(setacctram), mvo()
                           ("account", "eosio")
                           ("ram_bytes", total_res["ram_bytes"].as_int64() )
                        )
   );

   auto eosio_original_balance = get_balance( N(eosio) );

   BOOST_REQUIRE_EQUAL( success(), sellram( N(eosio), total_res["ram_bytes"].as_int64() ) );

   auto tokens_received_by_selling_ram = get_balance( N(eosio) ) - eosio_original_balance;

   BOOST_REQUIRE( double(tokens_paid_for_ram.get_amount() - tokens_received_by_selling_ram.get_amount()) / tokens_paid_for_ram.get_amount() < 0.01 );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
