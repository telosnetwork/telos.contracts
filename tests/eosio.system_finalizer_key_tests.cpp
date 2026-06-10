#include <boost/test/unit_test.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/variant_object.hpp>

#include "eosio.system_tester.hpp"

/*
 * Tests for the Savanna finalizer key actions (regfinkey, actfinkey,
 * delfinkey) and the switchtosvnn consensus transition added in PR #59.
 *
 * NOTE: written against the Spring (TelosZero core) tester. These tests
 * were authored without a local Spring build available and need a CI run
 * to verify; they are modeled on AntelopeIO/reference-contracts
 * eosio.system finalizer key tests and the local Telos tester helpers.
 */

using namespace eosio_system;

struct finalizer_key_tester : eosio_system_tester {
   static fc::crypto::blslib::bls_private_key new_bls_key() {
      return fc::crypto::blslib::bls_private_key::generate();
   }

   fc::variant get_finalizer_info( const account_name& act ) {
      vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name, "finalizers"_n, act );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "finalizer_info", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }

   fc::variant get_last_prop_finalizers() {
      vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name, "lastpropfins"_n, account_name(0) );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "last_prop_finalizers_info", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }

   action_result regfinkey( const account_name& finalizer, const std::string& key, const std::string& pop ) {
      return push_action( finalizer, "regfinkey"_n, mvo()
                          ("finalizer_name", finalizer)
                          ("finalizer_key", key)
                          ("proof_of_possession", pop) );
   }

   action_result actfinkey( const account_name& finalizer, const std::string& key ) {
      return push_action( finalizer, "actfinkey"_n, mvo()
                          ("finalizer_name", finalizer)
                          ("finalizer_key", key) );
   }

   action_result delfinkey( const account_name& finalizer, const std::string& key ) {
      return push_action( finalizer, "delfinkey"_n, mvo()
                          ("finalizer_name", finalizer)
                          ("finalizer_key", key) );
   }

   action_result switchtosvnn( const account_name& signer ) {
      return push_action( signer, "switchtosvnn"_n, mvo() );
   }
};

BOOST_AUTO_TEST_SUITE(eosio_system_finalizer_key_tests)

BOOST_FIXTURE_TEST_CASE( regfinkey_validation, finalizer_key_tester ) try {
   const account_name alice = "alice1111111"_n;
   const account_name prod  = "defproducer1"_n;

   create_account_with_resources( prod, config::system_account_name, core_sym::from_string("10.0000"), false );
   transfer( config::system_account_name, prod, core_sym::from_string("1000.0000"), config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( prod, prod, core_sym::from_string("100.0000"), core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( prod ) );

   const auto key = new_bls_key();
   const std::string pubkey = key.get_public_key().to_string();
   const std::string pop    = key.proof_of_possession().to_string();

   // not a registered producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "finalizer alice1111111 is not a registered producer" ),
                        regfinkey( alice, pubkey, pop ) );

   // malformed key / pop prefixes
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "finalizer key does not start with PUB_BLS: UNKNOWN_" + pubkey.substr(8) ),
                        regfinkey( prod, "UNKNOWN_" + pubkey.substr(8), pop ) );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "proof of possession signature does not start with SIG_BLS: UNKNOWN_" + pop.substr(8) ),
                        regfinkey( prod, pubkey, "UNKNOWN_" + pop.substr(8) ) );

   // proof of possession must match the key
   const auto other = new_bls_key();
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "proof of possession check failed" ),
                        regfinkey( prod, pubkey, other.proof_of_possession().to_string() ) );

   // success
   BOOST_REQUIRE_EQUAL( success(), regfinkey( prod, pubkey, pop ) );
   auto fin = get_finalizer_info( prod );
   BOOST_REQUIRE_EQUAL( fin["finalizer_name"].as_string(), prod.to_string() );
   BOOST_REQUIRE_EQUAL( fin["finalizer_key_count"].as<uint32_t>(), 1u );

   // duplicate key rejected
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "duplicate finalizer key: " + pubkey ),
                        regfinkey( prod, pubkey, pop ) );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( finalizer_key_lifecycle, finalizer_key_tester ) try {
   const account_name prod = "defproducer1"_n;

   create_account_with_resources( prod, config::system_account_name, core_sym::from_string("10.0000"), false );
   transfer( config::system_account_name, prod, core_sym::from_string("1000.0000"), config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( prod, prod, core_sym::from_string("100.0000"), core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( prod ) );

   const auto key1 = new_bls_key();
   const auto key2 = new_bls_key();
   const std::string pub1 = key1.get_public_key().to_string();
   const std::string pub2 = key2.get_public_key().to_string();

   BOOST_REQUIRE_EQUAL( success(), regfinkey( prod, pub1, key1.proof_of_possession().to_string() ) );
   BOOST_REQUIRE_EQUAL( success(), regfinkey( prod, pub2, key2.proof_of_possession().to_string() ) );
   BOOST_REQUIRE_EQUAL( get_finalizer_info( prod )["finalizer_key_count"].as<uint32_t>(), 2u );

   // first registered key is the active one; activating it again fails
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "finalizer key was already active: " + pub1 ),
                        actfinkey( prod, pub1 ) );

   // unregistered key cannot be activated
   const auto key3 = new_bls_key();
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "finalizer key was not registered: " + key3.get_public_key().to_string() ),
                        actfinkey( prod, key3.get_public_key().to_string() ) );

   // switch active key to key2
   BOOST_REQUIRE_EQUAL( success(), actfinkey( prod, pub2 ) );

   // an active key cannot be deleted while other keys remain
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "cannot delete an active key unless it is the last registered finalizer key, has 2 keys" ),
                        delfinkey( prod, pub2 ) );

   // delete the non-active key, then the last (active) key
   BOOST_REQUIRE_EQUAL( success(), delfinkey( prod, pub1 ) );
   BOOST_REQUIRE_EQUAL( get_finalizer_info( prod )["finalizer_key_count"].as<uint32_t>(), 1u );
   BOOST_REQUIRE_EQUAL( success(), delfinkey( prod, pub2 ) );
   BOOST_REQUIRE( get_finalizer_info( prod ).is_null() );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( switchtosvnn_guards_and_transition, finalizer_key_tester ) try {
   // requires the system account's authority
   BOOST_REQUIRE_EQUAL( error( "missing authority of eosio" ),
                        switchtosvnn( "alice1111111"_n ) );

   // establish an active 21-producer schedule
   auto producer_names = active_and_vote_producers();

   // every scheduled producer must have an active finalizer key
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "producer " + producer_names[0].to_string() + " does not have an active finalizer key" ),
                        switchtosvnn( config::system_account_name ) );

   // register a finalizer key for each scheduled producer
   for( const auto& p : producer_names ) {
      const auto key = new_bls_key();
      BOOST_REQUIRE_EQUAL( success(),
                           regfinkey( p, key.get_public_key().to_string(), key.proof_of_possession().to_string() ) );
   }

   BOOST_REQUIRE_EQUAL( success(), switchtosvnn( config::system_account_name ) );

   // first finalizer policy stored, one finalizer per scheduled producer
   auto lpf = get_last_prop_finalizers();
   BOOST_REQUIRE( !lpf.is_null() );
   BOOST_REQUIRE_EQUAL( lpf["last_proposed_finalizers"].get_array().size(), producer_names.size() );

   // transition proceeds
   produce_blocks( 10 );

   // one-way switch
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "switchtosvnn can be run only once" ),
                        switchtosvnn( config::system_account_name ) );
} FC_LOG_AND_RETHROW()

// Regression for the Savanna autokick fix: once on Savanna,
// set_proposed_producers() succeeds even when the schedule is unchanged.
// Schedule metrics (and their in-flight missed-block counters) must survive
// those no-op proposals instead of being rebuilt every scheduling round.
BOOST_FIXTURE_TEST_CASE( savanna_schedule_metrics_preserved, finalizer_key_tester ) try {
   auto producer_names = active_and_vote_producers();

   for( const auto& p : producer_names ) {
      const auto key = new_bls_key();
      BOOST_REQUIRE_EQUAL( success(),
                           regfinkey( p, key.get_public_key().to_string(), key.proof_of_possession().to_string() ) );
   }
   BOOST_REQUIRE_EQUAL( success(), switchtosvnn( config::system_account_name ) );
   produce_blocks( 10 );

   auto get_metrics = [&]() {
      vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name,
                                              "schedulemetr"_n, "schedulemetr"_n );
      BOOST_REQUIRE( !data.empty() );
      return abi_ser.binary_to_variant( "schedule_metrics_state", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   };

   auto before = get_metrics();
   const auto before_metrics = before["producers_metric"].get_array();
   BOOST_REQUIRE_EQUAL( before_metrics.size(), producer_names.size() );

   // cross several update_elected_producers() rounds (one per ~120 slots)
   // with unchanged membership
   produce_blocks( 500 );

   auto after = get_metrics();
   const auto after_metrics = after["producers_metric"].get_array();

   // membership must be unchanged - the metrics vector must not have been
   // rebuilt (a rebuild resets every producer's counters to 12, which was
   // the bug that disabled autokick under Savanna)
   BOOST_REQUIRE_EQUAL( after_metrics.size(), before_metrics.size() );

   // at least one producer should be mid-cycle (counter != 12) at any given
   // instant while blocks are being produced; if every single counter reads
   // exactly 12 the metrics were just wiped
   bool any_mid_cycle = false;
   for( const auto& m : after_metrics ) {
      if( m["missed_blocks_per_cycle"].as<uint32_t>() != 12 ) {
         any_mid_cycle = true;
         break;
      }
   }
   BOOST_REQUIRE( any_mid_cycle );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
