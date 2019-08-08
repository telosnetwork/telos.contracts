#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/wast_to_wasm.hpp>
//#include <eosio.arbitration/eosio.arbitration.hpp>


#include <Runtime/Runtime.h>
#include <iomanip>

#include <fc/variant_object.hpp>
#include "contracts.hpp"
#include "test_symbol.hpp"
#include "eosio.arb_tester.hpp"

#include <iostream>

using namespace eosio::testing;
using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;
using namespace std;

using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(eosio_arb_tests)

BOOST_FIXTURE_TEST_CASE( check_config_setter, eosio_arb_tester ) try {
   auto one_day = 86400;
   uint32_t 
      start_election = 300, 
      election_duration = 300, 
      arbitrator_term_length = one_day * 10;
   vector<int64_t> 
      fees = {int64_t(1), int64_t(2), int64_t(3), int64_t(4)};
   uint16_t 
      max_elected_arbs = 20;

   produce_blocks(1);
   auto current_time = now();
   // setup config
   setconfig ( max_elected_arbs, start_election, election_duration, arbitrator_term_length, fees);
   produce_blocks();
   
   auto config = get_config();

   REQUIRE_MATCHING_OBJECT(
      config, 
      mvo()
		   ("publisher", eosio::chain::name("eosio.arb"))
         ("max_elected_arbs", max_elected_arbs)
         ("election_duration", election_duration)
         ("election_start", start_election)
		   ("fee_structure", vector<int64_t>({int64_t(1), int64_t(2), int64_t(3), int64_t(4)}))
         ("arb_term_length", uint32_t(one_day * 10))
		   ("last_time_edited", config["last_time_edited"].as<uint32_t>()) 
		   ("current_ballot_id", 0)
		   ("auto_start_election", 0)
   );
   
   produce_blocks(1);

   init_election();
   produce_blocks(1);
   produce_block(fc::seconds(start_election + election_duration));
   produce_blocks(1);
   regarb(test_voters[0], ipfs_hash1);
   endelection(test_voters[0]);
   produce_blocks(1);

   max_elected_arbs--;
   start_election++;
   election_duration++;
   arbitrator_term_length++;
   setconfig ( max_elected_arbs, start_election, election_duration, arbitrator_term_length, fees);
   produce_blocks(1);

   config = get_config();
   REQUIRE_MATCHING_OBJECT(
      config, 
      mvo()
         ("publisher", eosio::chain::name("eosio.arb"))
         ("max_elected_arbs", max_elected_arbs)
         ("election_duration", election_duration)
         ("election_start", start_election)
		   ("fee_structure", vector<int64_t>({int64_t(1), int64_t(2), int64_t(3), int64_t(4)}))
         ("arb_term_length", uint32_t(one_day * 10 + 1))
		   ("last_time_edited", now())
		   ("current_ballot_id", 1)
		   ("auto_start_election", 1)
   );
   produce_blocks(1);
   
   // cannot init another election while one is in progress
   BOOST_REQUIRE_EXCEPTION( 
      init_election(),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Election is on auto start mode." )
   );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( init_election_integrity, eosio_arb_tester ) try {
   auto one_day = 86400;
   uint32_t 
      start_election = 300, 
      election_duration = 300, 
      arbitrator_term_length = one_day * 10;
   vector<int64_t> 
      fees = {int64_t(1), int64_t(2), int64_t(3), int64_t(4)};
   uint16_t 
      max_elected_arbs = 20;

   // setup config
   setconfig ( max_elected_arbs, start_election, election_duration, arbitrator_term_length, fees);
   produce_blocks(1);
   
   init_election();
   uint32_t expected_begin_time = uint32_t(now() + start_election);
   uint32_t expected_end_time = expected_begin_time + election_duration;
   produce_blocks(1);

   auto config = get_config();
   auto cbid = config["current_ballot_id"].as_uint64();   

   auto ballot = get_ballot(cbid);
   auto bid = ballot["reference_id"].as_uint64();

   auto leaderboard = get_leaderboard(bid);
   auto lid = leaderboard["board_id"].as_uint64();

   BOOST_REQUIRE_EQUAL(expected_begin_time, leaderboard["begin_time"].as<uint32_t>());
   BOOST_REQUIRE_EQUAL(expected_end_time, leaderboard["end_time"].as<uint32_t>());

   // exceptions : "ballot doesn't exist" and "leaderboard doesn't exist" mean the following checks should fail
      // if they didn't yet got the error, check the code - can't check here
   // verify correct assignments of available primary keys
   BOOST_REQUIRE_EQUAL(bid, lid);
   BOOST_REQUIRE_EQUAL(cbid, lid);

   // cannot init another election while one is in progress
   BOOST_REQUIRE_EXCEPTION( 
      init_election(),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Election is on auto start mode." )
   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( register_unregister_endelection, eosio_arb_tester ) try {
   auto one_day = 86400;
   uint32_t start_election = 300, election_duration = 300, arbitrator_term_length = one_day * 10;
   vector<int64_t> fees = {int64_t(1), int64_t(2), int64_t(3), int64_t(4)};
   uint16_t max_elected_arbs = 20;

   // setup config
   setconfig ( max_elected_arbs, start_election, election_duration, arbitrator_term_length, fees);
   produce_blocks(1);
   
   // choose 1 candidate
   name voter = test_voters[0];
   name candidate = test_voters[1];
   name dropout = test_voters[2];
   name noncandidate = test_voters[3];

   std::string credentials = ipfs_hash2;

   // candidates cannot register without an election
   regarb(candidate, credentials);
   BOOST_REQUIRE_EXCEPTION( 
      candaddlead(candidate, credentials), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "there is no active election" )
   );

   auto config = get_config();
   BOOST_REQUIRE_EQUAL(false, config["auto_start_election"]);

   // start an election
   init_election();
   produce_blocks(1);

   BOOST_REQUIRE_EXCEPTION( 
      candrmvlead(candidate), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "candidate not found in leaderboard list" )
   );

   config = get_config();
   BOOST_REQUIRE_EQUAL(true, config["auto_start_election"]);
   
   BOOST_REQUIRE_EXCEPTION( 
      candaddlead( dropout, credentials ), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Nominee isn't an applicant. Use regarb action to register as a nominee" )
   );

   // register 
   candaddlead( candidate, credentials );
   regarb( dropout, credentials );
   candaddlead( dropout, credentials );
   produce_blocks(1);

   // check integrity
   auto c = get_nominee(dropout.value);
   BOOST_REQUIRE_EQUAL( c["nominee_name"].as<name>(), dropout );
   BOOST_REQUIRE_EQUAL( c["credentials_link"],  credentials );
   
   c = get_nominee(candidate.value);
   BOOST_REQUIRE_EQUAL( c["nominee_name"].as<name>(), candidate );
   BOOST_REQUIRE_EQUAL( c["credentials_link"],  credentials );

   // dropout unregisters
   candrmvlead( dropout );
   unregnominee( dropout );
   // candidate unregisters
   candrmvlead( candidate );
   unregnominee( candidate );
   produce_blocks(1);

   // check dropout is not a candidate anymore
   c = get_nominee(dropout.value);
   BOOST_REQUIRE_EQUAL(true, c.is_null());
   c = get_nominee(candidate.value);
   BOOST_REQUIRE_EQUAL(true, c.is_null());
   
   // candidate registers back
   regarb( candidate, credentials );
   candaddlead( candidate, credentials );
   c = get_nominee(candidate.value);
   BOOST_REQUIRE_EQUAL( c["nominee_name"].as<name>(), candidate );
   BOOST_REQUIRE_EQUAL( c["credentials_link"],  credentials );
   produce_blocks(1);

   // candidates cannot register multiple times
   BOOST_REQUIRE_EXCEPTION( 
      regarb(candidate, credentials), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Nominee is already an applicant" )
   );

   // dropout cannot unregister multiple times
   BOOST_REQUIRE_EXCEPTION( 
      unregnominee(dropout), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Nominee isn't an applicant" )
   );

   BOOST_REQUIRE_EXCEPTION( 
      candrmvlead(dropout), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Nominee isn't an applicant." )
   );
   
   // start the election period
   produce_block(fc::seconds(start_election));
   produce_blocks(1);
   
   // candidates cannot unregister during election
   BOOST_REQUIRE_EXCEPTION( 
      unregnominee(candidate), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Cannot unregister while election is in progress" )
   );
   BOOST_REQUIRE_EXCEPTION( 
      candrmvlead(candidate), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "cannot remove candidates once voting has begun" )
   );
   
   // new candidates can register while an election is ongoing
   regarb(dropout, credentials);

   // but they cannot unregister during election even if they are not part of it
   BOOST_REQUIRE_EXCEPTION( 
      unregnominee(dropout), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Cannot unregister while election is in progress" )
   );

   config = get_config();
   auto cbid = config["current_ballot_id"].as_uint64();   

   // vote for candidate
   symbol vote_symbol = symbol(4, "VOTE");
   register_voters(test_voters, 0, 1, vote_symbol);
   mirrorcast(voter.value, symbol(4, "TLOS"));
   castvote(voter.value, config["current_ballot_id"].as_uint64(), 0);
   produce_blocks(1);

   auto ballot = get_ballot(cbid);
   auto bid = ballot["reference_id"].as_uint64();

   auto leaderboard = get_leaderboard(bid);
   auto lid = leaderboard["board_id"].as_uint64();

   // election cannot end while in progress
   uint32_t remaining_seconds = uint32_t( leaderboard["end_time"].as<uint32_t>() - now() );
   BOOST_REQUIRE_EXCEPTION( 
      endelection(candidate), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Election hasn't ended. Please check again after the election is over in " + std::to_string( remaining_seconds ) + " seconds" )
   );

   // election period is over
   produce_block(fc::seconds(election_duration));
   produce_blocks(1);

   // non-candidates cannot end the election
   BOOST_REQUIRE_EXCEPTION( 
      endelection(voter), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Nominee isn't an applicant." )
   );
   produce_blocks(1);

   config = get_config();
   cbid = config["current_ballot_id"].as_uint64();   

   uint32_t expected_term_length = now() + arbitrator_term_length;

   // candidates that have NOT participated in the election CAN end it
   endelection(candidate); 
   produce_blocks(1);

   // the single candidate passes
   // so, candidate should be an arbitrator
   // and candidate is removed from the pending_candidates_table
   c = get_nominee(candidate.value);
   BOOST_REQUIRE_EQUAL(true, c.is_null());

   auto arb = get_arbitrator(candidate.value);
   uint16_t UNAVAILABLE_STATUS = 1;
   BOOST_REQUIRE_EQUAL(false, arb.is_null());
   BOOST_REQUIRE_EQUAL(arb["arb"].as<name>(), candidate);
   BOOST_REQUIRE_EQUAL(arb["arb_status"].as<uint16_t>(), UNAVAILABLE_STATUS);
   BOOST_REQUIRE_EQUAL(arb["credentials_link"].as<std::string>(), credentials);
//    BOOST_REQUIRE_EQUAL(arb["term_length"].as<uint32_t>(), expected_term_length);
//    candidate is not in the candidates table anymore
   BOOST_REQUIRE_EQUAL(true, get_nominee(candidate.value).is_null());
   
   config = get_config();
   auto next_cbid = config["current_ballot_id"].as_uint64();   

   BOOST_REQUIRE_EQUAL(true, config["auto_start_election"]);

   // election will start automatically because dropout reged mid-election
   BOOST_REQUIRE_NE(cbid, next_cbid);

   config = get_config();
   cbid = config["current_ballot_id"].as_uint64();   

   ballot = get_ballot(cbid);
   bid = ballot["reference_id"].as_uint64();

   leaderboard = get_leaderboard(bid);
   lid = leaderboard["board_id"].as_uint64();

   // dropout is part of the new election
   BOOST_REQUIRE_EQUAL((get_leaderboard(lid)["candidates"].as<vector<mvo>>()[0])["member"].as<name>(), dropout);

   // start the election period
   produce_block(fc::seconds(start_election));
   produce_blocks(1);
   
   // election period is over
   produce_block(fc::seconds(election_duration));
   produce_blocks(1);

   // arbitrators cannot end elections
   BOOST_REQUIRE_EXCEPTION( 
      endelection(candidate), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Nominee isn't an applicant." )
   );
   produce_blocks(1);

   produce_block(fc::seconds(arbitrator_term_length - start_election - election_duration - 10));
   produce_blocks(1);

   endelection(dropout); 
   expected_term_length = now() + arbitrator_term_length;
   produce_blocks(1);

   config = get_config();
   cbid = next_cbid;
   next_cbid = config["current_ballot_id"].as_uint64();   

   BOOST_REQUIRE_EQUAL(true, config["auto_start_election"]);

   // election will start automatically because dropout had no votes
   BOOST_REQUIRE_NE(cbid, next_cbid);
   produce_blocks(1);

   // arbitrators with a valid seat cannot register for election
   BOOST_REQUIRE_EXCEPTION( 
      regarb(candidate, credentials), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Nominee is already an Arbitrator and the seat hasn't expired" )
   );

   // let the term expire
   produce_block(fc::seconds(10));
   produce_blocks(1);

   // arbitrators with expired terms can join the election
   regarb(candidate, credentials);
   candaddlead(candidate, credentials);
   produce_blocks(1);

   arb = get_arbitrator(candidate);
   uint16_t SEAT_EXPIRED_STATUS = 3;
   BOOST_REQUIRE_EQUAL(false, arb.is_null());
   BOOST_REQUIRE_EQUAL(arb["arb_status"], SEAT_EXPIRED_STATUS);
   
   config = get_config();
   cbid = config["current_ballot_id"].as_uint64();   

   ballot = get_ballot(cbid);
   bid = ballot["reference_id"].as_uint64();

   leaderboard = get_leaderboard(bid);
   lid = leaderboard["board_id"].as_uint64();

   // dropout and the expired arbitrator are part of the new election
   BOOST_REQUIRE_EQUAL(get_leaderboard(lid)["candidates"].size(), 2);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( full_election, eosio_arb_tester ) try {
   auto one_day = 86400;
   uint32_t 
      start_election = 300, 
      election_duration = 300, 
      arbitrator_term_length = one_day * 10;
   vector<int64_t> 
      fees = {int64_t(1), int64_t(2), int64_t(3), int64_t(4)};
   uint16_t 
      max_elected_arbs = 2;

   // setup config
   setconfig ( max_elected_arbs, start_election, election_duration, arbitrator_term_length, fees);
   produce_blocks(1);

   // initialize election
   init_election();
   produce_blocks(1);

   // prepare voters
   symbol vote_symbol = symbol(4, "VOTE");
   register_voters(test_voters, 5, 30, vote_symbol);

   // choose 3 candidates
   name candidate1 = test_voters[0];
   name candidate2 = test_voters[1];
   name candidate3 = test_voters[2];
   name noncandidate = test_voters[3];

   std::string credentials = ipfs_hash3;

   // register and verifiy integrity of candidate info
   for(int i = 0; i <= 2; i++){
      // register 
      regarb( test_voters[i], credentials);
      candaddlead( test_voters[i], credentials);
      produce_blocks(1);
   }

   // ensure candidates that unreg + reg after initelection will be votable
   // note for the continuation of the test :
   // unreg + rereg will put the candidate at the end of the leaderboard queue 
   candrmvlead(candidate3);
   unregnominee(candidate3);
   produce_blocks(1);
   regarb(candidate3, credentials);
   candaddlead(candidate3, credentials);
   produce_blocks(1);

   // start the election period
   produce_block(fc::seconds(start_election));
   produce_blocks(1);

   // prepare for vote weight checking 
   asset weight = asset::from_string("200.0000 VOTE"), zero_asset = asset::from_string("0.0000 VOTE");
   vector<asset> expected_weights = {zero_asset, zero_asset, zero_asset};
   auto config = get_config();
   auto cbid = config["current_ballot_id"].as_uint64();   

   auto ballot = get_ballot(cbid);
   auto bid = ballot["reference_id"].as_uint64();

   //cast 6 votes
   for(int i = 5; i < 11; i++) {
      mirrorcast(test_voters[i].value, symbol(4, "TLOS"));
  
      // everyone votes candidate1 => the direction is actually candidate index for leaderboard voting
      uint16_t vote_for_candidate = 0;      
      castvote(test_voters[i].value, config["current_ballot_id"].as_uint64(), vote_for_candidate);
      expected_weights[vote_for_candidate] += weight;

      // todo : recast vote test ??

      // other vote :
      // every 3rd voter votes for candidate3 and others vote for candidate2
      vote_for_candidate = ( i % 3 == 0 ) ? uint16_t(2) : uint16_t(1);
      castvote(test_voters[i].value, config["current_ballot_id"].as_uint64(), vote_for_candidate);
      expected_weights[vote_for_candidate] += weight;

      produce_blocks(1);
   }

   // verify votes
   BOOST_REQUIRE_EQUAL(expected_weights[0], asset::from_string("1200.0000 VOTE"));
   BOOST_REQUIRE_EQUAL(expected_weights[1], asset::from_string("800.0000 VOTE"));
   BOOST_REQUIRE_EQUAL(expected_weights[2], asset::from_string("400.0000 VOTE"));

   auto leaderboard = get_leaderboard(bid);
   auto lid = leaderboard["board_id"].as_uint64();

   for(int i = 0 ; i < 3 ; i++){
      REQUIRE_MATCHING_OBJECT(
         leaderboard["candidates"][i], 
         mvo()
            ("member", test_voters[i])
            ("info_link", credentials)
            ("votes", expected_weights[i])
            ("status", uint8_t(0))
      );
   }
   
   // election period is over
   produce_block(fc::seconds(election_duration));
   produce_blocks(1);

   config = get_config();
   cbid = config["current_ballot_id"].as_uint64();   

   // end the election
   endelection(candidate1);
   uint32_t expected_term_length = now() + arbitrator_term_length;
   produce_blocks(1);

   // candidate1 should be arbitrator and not a candidate anymore
   auto arb = get_arbitrator(candidate1.value);
   uint16_t UNAVAILABLE_STATUS = 1;
   BOOST_REQUIRE_EQUAL(false, arb.is_null());
   BOOST_REQUIRE_EQUAL(arb["arb"].as<name>(), candidate1);
   BOOST_REQUIRE_EQUAL(arb["arb_status"].as<uint16_t>(), UNAVAILABLE_STATUS);
   BOOST_REQUIRE_EQUAL(arb["credentials_link"].as<std::string>(), credentials);
//    BOOST_REQUIRE_EQUAL(arb["term_length"].as<uint32_t>(), expected_term_length);
   auto c = get_nominee(candidate1.value);
   BOOST_REQUIRE_EQUAL(c.is_null(), true);
   
   // candidate2 should be arbitrator and not a candidate anymore
   arb = get_arbitrator(candidate2.value);
   BOOST_REQUIRE_EQUAL(false, arb.is_null());
   BOOST_REQUIRE_EQUAL(arb["arb"].as<name>(), candidate2);
   BOOST_REQUIRE_EQUAL(arb["arb_status"].as<uint16_t>(), UNAVAILABLE_STATUS);
   BOOST_REQUIRE_EQUAL(arb["credentials_link"].as<std::string>(), credentials);
//    BOOST_REQUIRE_EQUAL(arb["term_length"].as<uint32_t>(), expected_term_length); //TODO: determine term length test based on new struct fields
   c = get_nominee(candidate2.value);
   BOOST_REQUIRE_EQUAL(c.is_null(), true);

   // candidate3 should NOT be an arbitrator, only 2 seats were available
   arb = get_arbitrator(candidate3.value);
   BOOST_REQUIRE_EQUAL(true, arb.is_null());
   // candidate3 will be removed as a candidate because there are no more seats available , and no new election will start
   c = get_nominee(candidate3.value);
   BOOST_REQUIRE_EQUAL(c.is_null(), true);

   auto previous_cbid = cbid;
   
   config = get_config();
   cbid = config["current_ballot_id"].as_uint64();   

   ballot = get_ballot(cbid);
   bid = ballot["reference_id"].as_uint64();

   leaderboard = get_leaderboard(bid);
   lid = leaderboard["board_id"].as_uint64();

   // there is no new election in progress since no seats are left
   BOOST_REQUIRE_EQUAL(cbid, previous_cbid);
   BOOST_REQUIRE_EQUAL(false, config["auto_start_election"]);

   produce_blocks(1);

   BOOST_REQUIRE_EXCEPTION( 
      candaddlead(noncandidate, credentials),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "there is no active election" )
   );

   regarb(noncandidate, credentials);
   produce_blocks(1);

   BOOST_REQUIRE_EXCEPTION( 
      candaddlead(noncandidate, credentials),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "there is no active election" )
   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( tiebreaker, eosio_arb_tester ) try {
   auto one_day = 86400;
   uint32_t 
      start_election = 300, 
      election_duration = 300, 
      arbitrator_term_length = one_day * 10;
   vector<int64_t> 
      fees = {int64_t(1), int64_t(2), int64_t(3), int64_t(4)};
   uint16_t 
      max_elected_arbs = 2;


   // setup config
   setconfig ( max_elected_arbs, start_election, election_duration, arbitrator_term_length, fees);
   produce_blocks(1);

   // initialize election
   init_election();
   produce_blocks(1);

   // prepare voters
   symbol vote_symbol = symbol(4, "VOTE");
   register_voters(test_voters, 5, 30, vote_symbol);

   // choose 3 candidates
   name candidate1 = test_voters[0];
   name candidate2 = test_voters[1];
   name candidate3 = test_voters[2];

   std::string credentials = ipfs_hash1;

   // register and verifiy integrity of candidate info
   for(int i = 0; i <= 2; i++){
      // register 
      regarb( test_voters[i], credentials);
      candaddlead( test_voters[i], credentials);
      produce_blocks(1);
   }

   // ensure candidates that unreg + reg after initelection will be votable
   // note for the continuation of the test :
   // unreg + rereg will put the candidate at the end of the leaderboard queue 
   candrmvlead(candidate3);
   unregnominee(candidate3);
   produce_blocks(1);
   regarb(candidate3, credentials);
   candaddlead(candidate3, credentials);
   produce_blocks(1);

   // start the election period
   produce_block(fc::seconds(start_election));
   produce_blocks(1);

   // prepare for vote weight checking 
   asset weight = asset::from_string("200.0000 VOTE"), zero_asset = asset::from_string("0.0000 VOTE");
   vector<asset> expected_weights = {zero_asset, zero_asset, zero_asset};

   auto config = get_config();
   auto cbid = config["current_ballot_id"].as_uint64();   

   auto ballot = get_ballot(cbid);
   auto bid = ballot["reference_id"].as_uint64();

   //cast 6 votes
   for(int i = 5; i < 11; i++) {
      mirrorcast(test_voters[i].value, symbol(4, "TLOS"));
  
      // everyone votes candidate1 => the direction is actually candidate index for leaderboard voting
      uint16_t vote_for_candidate = 0;      
      castvote(test_voters[i].value, config["current_ballot_id"].as_uint64(), vote_for_candidate);
      expected_weights[vote_for_candidate] += weight;

      // other vote : 1 for candidate2 and 1 for candidate3 to get a tie
      vote_for_candidate = ( i % 2 == 0 ) ? uint16_t(2) : uint16_t(1);
      castvote(test_voters[i].value, config["current_ballot_id"].as_uint64(), vote_for_candidate);
      expected_weights[vote_for_candidate] += weight;

      produce_blocks(1);
   }

   // verify votes
   BOOST_REQUIRE_EQUAL(expected_weights[0], asset::from_string("1200.0000 VOTE"));
   BOOST_REQUIRE_EQUAL(expected_weights[1], asset::from_string("600.0000 VOTE"));
   BOOST_REQUIRE_EQUAL(expected_weights[2], asset::from_string("600.0000 VOTE"));

   auto leaderboard = get_leaderboard(bid);
   auto lid = leaderboard["board_id"].as_uint64();

   for(int i = 0 ; i < 3 ; i++){
      REQUIRE_MATCHING_OBJECT(
         leaderboard["candidates"][i], 
         mvo()
            ("member", test_voters[i])
            ("info_link", credentials)
            ("votes", expected_weights[i])
            ("status", uint8_t(0))
      );
   }
   
   // election period is over
   produce_block(fc::seconds(election_duration));
   produce_blocks(1);

   // end the election
   uint32_t expected_term_length = now() + arbitrator_term_length;
   endelection(candidate1);
   produce_blocks(1);

   // candidate1 should be arbitrator and not a candidate anymore
   auto arb = get_arbitrator(candidate1.value);
   uint16_t UNAVAILABLE_STATUS = 1;
   BOOST_REQUIRE_EQUAL(false, arb.is_null());
   BOOST_REQUIRE_EQUAL(arb["arb"].as<name>(), candidate1);
   BOOST_REQUIRE_EQUAL(arb["arb_status"].as<uint16_t>(), UNAVAILABLE_STATUS);
   BOOST_REQUIRE_EQUAL(arb["credentials_link"].as<std::string>(), credentials);
//    BOOST_REQUIRE_EQUAL(arb["term_length"].as<uint32_t>(), expected_term_length);
   auto c = get_nominee(candidate1.value);
   BOOST_REQUIRE_EQUAL(c.is_null(), true);
   
   // candidate2 and candidate3 should still be candidates in new election for a tie breaker
   arb = get_arbitrator(candidate2.value);
   BOOST_REQUIRE_EQUAL(true, arb.is_null());
   c = get_nominee(candidate2.value);
   BOOST_REQUIRE_EQUAL(c.is_null(), false);
   BOOST_REQUIRE_EQUAL( c["nominee_name"].as<name>(), candidate2 );
   
   arb = get_arbitrator(candidate3.value);
   BOOST_REQUIRE_EQUAL(true, arb.is_null());
   c = get_nominee(candidate3.value);
   BOOST_REQUIRE_EQUAL(c.is_null(), false);
   BOOST_REQUIRE_EQUAL( c["nominee_name"].as<name>(), candidate3 );

   config = get_config();
   // a new leaderboard for the re-election / tiebreaker
   BOOST_REQUIRE_NE(config["current_ballot_id"].as_uint64(), cbid);
   cbid = config["current_ballot_id"].as_uint64();   

   ballot = get_ballot(cbid);
   bid = ballot["reference_id"].as_uint64();

   leaderboard = get_leaderboard(bid);
   lid = leaderboard["board_id"].as_uint64();
 
   for(int i = 0 ; i < 2 ; i++){
      REQUIRE_MATCHING_OBJECT(
         leaderboard["candidates"][i], 
         mvo()
            ("member", test_voters[i+1])
            ("info_link", credentials)
            ("votes", zero_asset)
            ("status", uint8_t(0))
      );
   }

   BOOST_REQUIRE_EQUAL(true, config["auto_start_election"]);

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( case_setup_flow, eosio_arb_tester ) try {

    // choose 3 claimants
    vector<name> claimants = {
		test_voters[0],
		test_voters[1],
		test_voters[2],
		test_voters[3],
		test_voters[4],
		test_voters[6]
	};

    vector<name> respondants = {
		test_voters[7],
		test_voters[8],
		test_voters[9]
	};

    // specify claim link
    string claim_link_invalid = "http://google.com";

    // Lang codes
    vector<uint8_t> langcodes = {uint8_t(0)};

    // Test invalid claim link
   //  std::cout <<"test invalid claim link" << endl;
    BOOST_REQUIRE_EXCEPTION(
            filecase(claimants[0], claim_link_invalid, langcodes, {}  ),
            eosio_assert_message_exception,
            eosio_assert_message_is( "invalid ipfs string, valid schema: <hash>" )
    );

    // file 3 cases for 3 separate claimants
    // file case w/o and w respondant

    filecase(claimants[0], claim_links[0], langcodes , {} );
    filecase(claimants[1], claim_links[0], langcodes , respondants[0] );
    filecase(claimants[2], claim_links[0], langcodes , respondants[1] );
    produce_blocks(1);

    // attempt to retrieve case info for first case filed
    auto casef = get_casefile(uint64_t(0));
    auto cid = casef["case_id"].as_uint64();
    auto cstatus = casef["case_status"];
    auto cunread_claims = casef["unread_claims"];

	//TODO: assert respondant is empty in first case.

    // std::cout<<"Case_id: " << cid  << endl;
    // std::cout<<"Case_status: " << cstatus << endl;

    // verify first case filed matches retrieved case
    BOOST_REQUIRE_EQUAL( casef["claimant"].as<name>(), claimants[0] );

    // verify first case filed status is CASE_SETUP
    BOOST_REQUIRE_EQUAL( casef["case_status"].as_uint64(), CASE_SETUP  );

    // Check new case file has one unread claims.
   //  std::cout << "Unread Claim Count for new case: " << cunread_claims.size() << endl;
    BOOST_REQUIRE_EQUAL(casef["unread_claims"].size(), 1 );

    // claimant who filed case is the only one allowed to add additional claims to casefile
    BOOST_REQUIRE_EXCEPTION(
         addclaim( cid, claim_links[1], claimants[1]  ),
         eosio_assert_message_exception,
         eosio_assert_message_is( "you are not the claimant of this case." )
    );

    // add additional claims to the case file
    addclaim( cid, claim_links[1], claimants[0]  );
    addclaim( cid, claim_links[2], claimants[0]  );
    addclaim( cid, claim_links[3], claimants[0]  );
    produce_blocks(1);

    // Check unread claim was added to casefile
    casef = get_casefile(uint64_t(0));
    BOOST_REQUIRE_EQUAL(casef["unread_claims"].size(), 4 );


    // Retrieve 1st unread claim for first case and verify info
    auto unread_claim = get_unread_claim(0,0);
    REQUIRE_MATCHING_OBJECT (unread_claim,
         mvo()
         ("claim_id",0)
         ("claim_summary", claim_links[0])
         ("decision_link", "")
         ("response_link", "")
         ("decision_class", 0)
    );

    BOOST_REQUIRE_EQUAL(false, get_unread_claim( cid, claim_links[2]).is_null() );
    removeclaim(cid, claim_links[2], claimants[0] );
    // check to see if claim is removed
    //get_unread_claim(uint64_t casefile_id, string claim_link)
    BOOST_REQUIRE_EQUAL(true, get_unread_claim( cid, claim_links[2]).is_null() );


    // attempt to retrieve case info for last case filed and shred case
    BOOST_REQUIRE_EQUAL(false, get_casefile(uint64_t(2)).is_null() );
    casef = get_casefile(uint64_t(2));
    cid = casef["case_id"].as_uint64();
    shredcase(cid, claimants[2]);
    BOOST_REQUIRE_EQUAL(true, get_casefile(uint64_t(2)).is_null() );

    //check number of cases after 1 case shredded (should be 3 after shred)

    //ready first case filed and verify status changed from CASE_SETUP to AWAITING_ARBS
    casef = get_casefile(uint64_t(0));
    cid = casef["case_id"].as_uint64();
    BOOST_REQUIRE_EQUAL( CASE_SETUP,  casef["case_status"].as<uint8_t>());

    transfer(N(eosio), claimants[0], asset::from_string("1000.0000 TLOS"), "");
    transfer(claimants[0], N(eosio.arb), asset::from_string("200.0000 TLOS"), "");
    readycase(cid, claimants[0]);

    casef = get_casefile(uint64_t(0));
    BOOST_REQUIRE_EQUAL( AWAITING_ARBS,  casef["case_status"].as<uint8_t>());


} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( assign_arb_flow, eosio_arb_tester ) try {
	elect_arbitrators(8, 10); // test_voters 0-7 are arbitrators, 8-17 voted for 0-7

	filecase(claimant, claim_link1, lang_codes, respondant);
	uint64_t current_case_id = 0;
	BOOST_REQUIRE_EQUAL(false, get_casefile(current_case_id).is_null());
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link1).is_null());
	addclaim(current_case_id, claim_link2, claimant);
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link2).is_null());

	transfer(N(eosio), claimant.value, asset::from_string("1000.0000 TLOS"), "");
	transfer(claimant.value, N(eosio.arb), asset::from_string("200.0000 TLOS"), "");

	readycase(current_case_id, claimant);

	auto cf = get_casefile(current_case_id);

	BOOST_REQUIRE_EQUAL(cf["case_status"].as<uint8_t>(), AWAITING_ARBS);
	
	produce_blocks();

	newarbstatus(AVAILABLE, test_voters[0]);
	newarbstatus(AVAILABLE, test_voters[1]);
	newarbstatus(AVAILABLE, test_voters[2]);
	produce_blocks();

	assigntocase(current_case_id, test_voters[0], assigner); //TODO: missing require signature issue.

	cf = get_casefile(current_case_id);
	BOOST_REQUIRE_EQUAL(cf["case_status"].as<uint8_t>(), CASE_INVESTIGATION);

	auto case_arbs = cf["arbitrators"].as<vector<fc::variant>>();
	BOOST_REQUIRE_EQUAL(case_arbs.size(), 1);
	BOOST_REQUIRE_EQUAL(case_arbs[0].as_string(), test_voters[0].to_string());

	//NOTE: Arbitrator calls addarbs, in order to add new arbitrators.
	addarbs(current_case_id, test_voters[0], 2);

	//NOTE: this assumes that a demux or chain watching service catches the aboves call to addarbs
	// and acts according to the arguments in the action

	//NOTE: in this case the service would then call assigntocase twice
	assigntocase(current_case_id, test_voters[1], assigner);
	assigntocase(current_case_id, test_voters[2], assigner);

	cf = get_casefile(current_case_id);
	case_arbs = cf["arbitrators"].as<vector<fc::variant>>();
	BOOST_REQUIRE_EQUAL(case_arbs.size(), 3);
	BOOST_REQUIRE_EQUAL(case_arbs[0].as_string(), test_voters[0].to_string());
	BOOST_REQUIRE_EQUAL(case_arbs[1].as_string(), test_voters[1].to_string());
	BOOST_REQUIRE_EQUAL(case_arbs[2].as_string(), test_voters[2].to_string());

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( transfer_handler_integrity, eosio_arb_tester ) try {
	auto tlos_transfer_amount = asset::from_string("400.0000 TLOS");
	transfer(N(eosio), claimant.value, tlos_transfer_amount, "claimant initial eosio.token balance");

	//balance check from token contract
	auto balance = get_currency_balance(N(eosio.token), symbol(4, "TLOS"), claimant.value);
	BOOST_REQUIRE_EQUAL(balance, tlos_transfer_amount);

	//balance check from arb contract
	transfer(claimant.value, N(eosio.arb), tlos_transfer_amount, "claimant initial eosio.arb balance");
	balance = get_currency_balance(N(eosio.arb), symbol(4, "TLOS"), claimant.value);
	BOOST_REQUIRE_EQUAL(balance, tlos_transfer_amount);

	create(N(eosio), asset::from_string("10000000000.0000 PETER"));
	issue(N(eosio), N(eosio), asset::from_string("1000000000.0000 PETER"), "Initial amount!");

	auto custom_transfer_balance = asset::from_string("400.0000 PETER");
	transfer(N(eosio), claimant.value, custom_transfer_balance, "claimant initial custom eosio.token balance");

	//custom balance check from token contract
	balance = get_currency_balance(N(eosio.token), symbol(4, "PETER"), claimant.value);
	BOOST_REQUIRE_EQUAL(balance, custom_transfer_balance);

	BOOST_REQUIRE_EXCEPTION(
		transfer(claimant.value, N(eosio.arb), custom_transfer_balance, "claimant initial custom eosio.arb balance"),
		eosio_assert_message_exception,
		eosio_assert_message_is("only TLOS tokens are accepted by this contract")
    );

	withdraw(claimant);
	produce_blocks();

	// token balance check from arb contract, should be 0
	balance = get_currency_balance(N(eosio.arb), symbol(4, "TLOS"), claimant.value);
	BOOST_REQUIRE_EQUAL(balance, asset::from_string("0.0000 TLOS"));

	// token balance check from token contract, should be 400.0000 TLOS
	balance = get_currency_balance(N(eosio.token), symbol(4, "TLOS"), claimant.value);
	BOOST_REQUIRE_EQUAL(balance, tlos_transfer_amount);

	BOOST_REQUIRE_EXCEPTION(
		withdraw(claimant),
		eosio_assert_message_exception,
		eosio_assert_message_is("balance does not exist")
    );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( advance_case, eosio_arb_tester ) try {
	elect_arbitrators(8, 10); // test_voters 0-7 are arbitrators, 8-17 voted for 0-7
	newarbstatus(AVAILABLE, test_voters[0]);
	newarbstatus(AVAILABLE, test_voters[1]);
	newarbstatus(AVAILABLE, test_voters[2]);
	uint64_t current_case_id = 0;
   // cout << "claim_link1: " << claim_link1 << endl;
	filecase(claimant, claim_link1, lang_codes, respondant);
	produce_blocks();

	BOOST_REQUIRE_EQUAL(false, get_casefile(current_case_id).is_null());
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link1).is_null());
	addclaim(current_case_id, claim_link2, claimant);
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link2).is_null());

	transfer(N(eosio), claimant.value, asset::from_string("1000.0000 TLOS"), "");
	transfer(claimant.value, N(eosio.arb), asset::from_string("200.0000 TLOS"), "");

	readycase(current_case_id, claimant);
	produce_blocks();

	BOOST_REQUIRE_EXCEPTION(
		advancecase(current_case_id, test_voters[0]),
		eosio_assert_message_exception,
		eosio_assert_message_is("case_status must be greater than AWAITING_ARBS")
    );

	assigntocase(current_case_id, test_voters[0], assigner);
	dismisscase(current_case_id, test_voters[0], response_link1);

	BOOST_REQUIRE_EQUAL(DISMISSED, get_casefile(current_case_id)["case_status"].as<uint8_t>());

	BOOST_REQUIRE_EXCEPTION(
		advancecase(current_case_id, test_voters[0]),
		eosio_assert_message_exception,
		eosio_assert_message_is("Case has already been resolved or dismissed")
    );

	filecase(claimant, claim_link1, lang_codes, respondant);
	current_case_id++;
	produce_blocks();

	BOOST_REQUIRE_EQUAL(false, get_casefile(current_case_id).is_null());
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link1).is_null());
	addclaim(current_case_id, claim_link2, claimant);
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link2).is_null());

	transfer(claimant.value, N(eosio.arb), asset::from_string("200.0000 TLOS"), "");

	readycase(current_case_id, claimant);
	produce_blocks();

	assigntocase(current_case_id, test_voters[0], assigner);

	BOOST_REQUIRE_EXCEPTION(
		advancecase(current_case_id, bad_actor),
		eosio_assert_message_exception,
		eosio_assert_message_is("actor is not assigned to this case_id")
    );

	advancecase(current_case_id, test_voters[0]);
	BOOST_REQUIRE_EQUAL(HEARING, get_casefile(current_case_id)["case_status"].as<uint8_t>());
	produce_blocks();

	advancecase(current_case_id, test_voters[0]);
	BOOST_REQUIRE_EQUAL(DELIBERATION, get_casefile(current_case_id)["case_status"].as<uint8_t>());
	produce_blocks();

	advancecase(current_case_id, test_voters[0]);
	BOOST_REQUIRE_EQUAL(DECISION, get_casefile(current_case_id)["case_status"].as<uint8_t>());
	produce_blocks();

	BOOST_REQUIRE_EXCEPTION(
		eosio_arb_tester::setruling(current_case_id, test_voters[0], claim_link1),
		eosio_assert_message_exception,
		eosio_assert_message_is("case_status must be ENFORCEMENT")
    );

	advancecase(current_case_id, test_voters[0]);
	BOOST_REQUIRE_EQUAL(ENFORCEMENT, get_casefile(current_case_id)["case_status"].as<uint8_t>());
	produce_blocks();

	addarbs(current_case_id, test_voters[0], 2);
	assigntocase(current_case_id, test_voters[1], assigner);
	assigntocase(current_case_id, test_voters[2], assigner);
	produce_blocks();

	BOOST_REQUIRE_EXCEPTION(
		advancecase(current_case_id, test_voters[0]),
		eosio_assert_message_exception,
		eosio_assert_message_is("Case Ruling must be set before advancing case to RESOLVED status")
    );

	eosio_arb_tester::setruling(current_case_id, test_voters[0], claim_link1);
	produce_blocks();

	BOOST_REQUIRE_EQUAL(claim_link1, get_casefile(current_case_id)["case_ruling"].as_string());

	advancecase(current_case_id, test_voters[0]);
	
	auto approvals = get_casefile(current_case_id)["approvals"].as<vector<fc::variant>>();

	BOOST_REQUIRE_EQUAL(1, approvals.size());
	BOOST_REQUIRE_EQUAL(test_voters[0], approvals[0].as_string());
	produce_blocks();

	// cout << "casefile: " << get_casefile(current_case_id) << endl;

	BOOST_REQUIRE_EXCEPTION(
	advancecase(current_case_id, test_voters[0]),
		eosio_assert_message_exception,
		eosio_assert_message_is("arbitrator has already approved advancing this case")
    );

	advancecase(current_case_id, test_voters[1]);
	approvals = get_casefile(current_case_id)["approvals"].as<vector<fc::variant>>();

	BOOST_REQUIRE_EQUAL(2, approvals.size());
	BOOST_REQUIRE_EQUAL(test_voters[0], approvals[0].as_string());
	BOOST_REQUIRE_EQUAL(test_voters[1], approvals[1].as_string());
	produce_blocks();

	advancecase(current_case_id, test_voters[2]);
	approvals = get_casefile(current_case_id)["approvals"].as<vector<fc::variant>>();

	BOOST_REQUIRE_EQUAL(0, approvals.size());
	BOOST_REQUIRE_EQUAL(RESOLVED, get_casefile(current_case_id)["case_status"].as<uint8_t>());
	produce_blocks();

	//TODO: advance case to resolve, should pass
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( respondant_response, eosio_arb_tester ) try {
	elect_arbitrators(8, 10); // test_voters 0-7 are arbitrators, 8-17 voted for 0-7
	newarbstatus(AVAILABLE, test_voters[0]);
   
	filecase(claimant, claim_link1, lang_codes, respondant);
	produce_blocks();

	uint64_t current_case_id = 0;
	BOOST_REQUIRE_EQUAL(false, get_casefile(current_case_id).is_null());
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link1).is_null());
	addclaim(current_case_id, claim_link2, claimant);
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link2).is_null());

	transfer(N(eosio), claimant.value, asset::from_string("1000.0000 TLOS"), "");
	transfer(claimant.value, N(eosio.arb), asset::from_string("200.0000 TLOS"), "");

	readycase(current_case_id, claimant);
	produce_blocks();

	BOOST_REQUIRE_EXCEPTION(
		respond(current_case_id, claim_link1, bad_actor, response_link1),
		eosio_assert_message_exception,
		eosio_assert_message_is("must be the respondant of this case_id")
    );

	BOOST_REQUIRE_EXCEPTION(
		respond(current_case_id, claim_link1, respondant, response_link1),
		eosio_assert_message_exception,
		eosio_assert_message_is("case status does NOT allow responses at this time")
    );

	assigntocase(current_case_id, test_voters[0], assigner);

	respond(current_case_id, claim_link1, respondant, response_link1);
	produce_blocks();

	auto claim = get_unread_claim(current_case_id, claim_link1);
	BOOST_REQUIRE_EQUAL(false, claim.is_null());
	BOOST_REQUIRE_EQUAL(claim["claim_summary"].as_string(), claim_link1);
	BOOST_REQUIRE_EQUAL(claim["response_link"].as_string(), response_link1);

	respond(current_case_id, claim_link1, respondant, response_link2);

	claim = get_unread_claim(current_case_id, claim_link1);
	BOOST_REQUIRE_EQUAL(false, claim.is_null());
	BOOST_REQUIRE_EQUAL(claim["claim_summary"].as_string(), claim_link1);
	BOOST_REQUIRE_EQUAL(claim["response_link"].as_string(), response_link2);

	// file case with no respondant
	filecase(claimant, claim_link1, lang_codes, {});

	current_case_id++;
	BOOST_REQUIRE_EQUAL(false, get_casefile(current_case_id).is_null());
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link1).is_null());
	addclaim(current_case_id, claim_link2, claimant);
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link2).is_null());

	transfer(claimant.value, N(eosio.arb), asset::from_string("200.0000 TLOS"), "");

	readycase(current_case_id, claimant);
	assigntocase(current_case_id, test_voters[0], assigner);

	BOOST_REQUIRE_EXCEPTION(
		respond(current_case_id, claim_link1, bad_actor, response_link1),
		eosio_assert_message_exception,
		eosio_assert_message_is("case_id does not have a respondant")
    );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( recuse_arb, eosio_arb_tester ) try {
	elect_arbitrators(8, 10); // test_voters 0-7 are arbitrators, 8-17 voted for 0-7
	newarbstatus(AVAILABLE, test_voters[0]);
	uint64_t current_case_id = 0;

	BOOST_REQUIRE_EXCEPTION(
		recuse(current_case_id, "because i'm bias and can't hear this case", test_voters[0]),
		eosio_assert_message_exception,
		eosio_assert_message_is("No case found for given case_id")
    );

	filecase(claimant, claim_link1, lang_codes, respondant);
	produce_blocks();

	BOOST_REQUIRE_EQUAL(false, get_casefile(current_case_id).is_null());
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link1).is_null());
	addclaim(current_case_id, claim_link2, claimant);
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link2).is_null());

	transfer(N(eosio), claimant.value, asset::from_string("1000.0000 TLOS"), "");
	transfer(claimant.value, N(eosio.arb), asset::from_string("200.0000 TLOS"), "");

	readycase(current_case_id, claimant);
	produce_blocks();

	BOOST_REQUIRE_EXCEPTION(
		recuse(current_case_id, "because i'm bias and can't hear this case", test_voters[0]),
		eosio_assert_message_exception,
		eosio_assert_message_is("unable to recuse if the case is resolved")
    );

	assigntocase(current_case_id, test_voters[0], assigner);

	auto cf = get_casefile(current_case_id);
	auto assigned_arbs = cf["arbitrators"].as<vector<fc::variant>>();
	BOOST_REQUIRE_EQUAL(assigned_arbs.size(), 1);
	BOOST_REQUIRE_EQUAL(assigned_arbs[0].as_string(), test_voters[0].to_string());
	
	BOOST_REQUIRE_EXCEPTION( //TODO: should fail with "actor isn't an arbitrator"
		recuse(current_case_id, "because i'm bias and can't hear this case", bad_actor),
		eosio_assert_message_exception,
		eosio_assert_message_is("Arbitrator isn't selected for this case.")
    );

	recuse(current_case_id, "because i'm bias and can't hear this case", test_voters[0]);

	cf = get_casefile(current_case_id);
	assigned_arbs = cf["arbitrators"].as<vector<fc::variant>>();
	BOOST_REQUIRE_EQUAL(assigned_arbs.size(), 0);

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( dismiss_case, eosio_arb_tester ) try {

    //TODO: filecase
    elect_arbitrators(8, 10); // test_voters 0-7 are arbitrators, 8-17 voted for 0-7
    newarbstatus(AVAILABLE, test_voters[0]);
    uint64_t current_case_id = 0;

    filecase(claimant, claim_links[0], lang_codes, respondant);
    produce_blocks();

    //TODO: addclaims x 3

    addclaim(current_case_id, claim_links[1], claimant);
    addclaim(current_case_id, claim_links[2], claimant);
    addclaim(current_case_id, claim_links[3], claimant);
    auto cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL( cf["unread_claims"].size() , uint8_t(4)  );

    //TODO: transfer funds
    transfer(N(eosio), claimant.value, asset::from_string("1000.0000 TLOS"), "");
    transfer(claimant.value, N(eosio.arb), asset::from_string("200.0000 TLOS"), "");

    //TODO: readycase
    readycase(current_case_id, claimant);
    cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), AWAITING_ARBS);
    produce_blocks();

    BOOST_REQUIRE_EXCEPTION(
            dismisscase(current_case_id, bad_actor, ruling_links[0]   ),
            eosio_assert_message_exception,
            eosio_assert_message_is("Arbitrator isn't selected for this case")
    );


	//TODO: assigntocase
    //cf = get_casefile(current_case_id);
    auto assigned_arbs = cf["arbitrators"].as<vector<fc::variant>>();
	BOOST_REQUIRE_EQUAL(assigned_arbs.size(), 0);
    assigntocase(current_case_id, test_voters[0], assigner);

    cf = get_casefile(current_case_id);
   //  std::cout << "Case status: " << cf["case_status"].as_string() << endl;
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), CASE_INVESTIGATION );
    assigned_arbs = cf["arbitrators"].as<vector<fc::variant>>();
    BOOST_REQUIRE_EQUAL(assigned_arbs.size(), 1);
    BOOST_REQUIRE_EQUAL(assigned_arbs[0].as_string(), test_voters[0].to_string());

	//TODO: dismisscase
	// std::cout << "Case status: " << cf["case_status"].as_string() << endl;
    //[[eosio::action]] void dismisscase(uint64_t case_id, name assigned_arb, string ruling_link);

    BOOST_REQUIRE_EXCEPTION(
            dismisscase(current_case_id, bad_actor, ruling_links[0]   ),
            eosio_assert_message_exception,
            eosio_assert_message_is("Arbitrator isn't selected for this case")
    );

    dismisscase(current_case_id, test_voters[0], ruling_links[0]   );
    produce_blocks();

    cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), DISMISSED );
    BOOST_REQUIRE_EXCEPTION(
            dismisscase(current_case_id, test_voters[0], ruling_links[0]   ),
            eosio_assert_message_exception,
            eosio_assert_message_is("Case is already dismissed or complete")
    );


} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( accept_dismiss_claims, eosio_arb_tester ) try {
    //filecase
    elect_arbitrators(8, 10); // test_voters 0-7 are arbitrators, 8-17 voted for 0-7
    newarbstatus(AVAILABLE, test_voters[0]);
    uint64_t current_case_id = 0;

    filecase(claimant, claim_links[0], lang_codes, respondant);
    produce_blocks();

    //addclaims x 3

    addclaim(current_case_id, claim_links[1], claimant);
    addclaim(current_case_id, claim_links[2], claimant);
    addclaim(current_case_id, claim_links[3], claimant);
    auto cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL( cf["unread_claims"].size() , uint8_t(4)  );

    BOOST_REQUIRE_EXCEPTION(
            dismissclaim(current_case_id, claimant, claim_links[0], "The claim is not valid.  Dismissed" ),
            eosio_assert_message_exception,
            eosio_assert_message_is("unable to dismiss claim while this case file is in this status")
    );

    BOOST_REQUIRE_EXCEPTION(
            acceptclaim(current_case_id, test_voters[0], claim_links[1], ruling_links[1], A_TORT   ),
            eosio_assert_message_exception,
            eosio_assert_message_is("Only the assigned arbitrator can accept a claim")
    );

    //transfer funds
    transfer(N(eosio), claimant.value, asset::from_string("1000.0000 TLOS"), "");
    transfer(claimant.value, N(eosio.arb), asset::from_string("200.0000 TLOS"), "");

    //readycase
    readycase(current_case_id, claimant);
    cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), AWAITING_ARBS);
    produce_blocks();

    BOOST_REQUIRE_EXCEPTION(
            dismissclaim(current_case_id, claimant, claim_links[0], "The claim is not valid.  Dismissed" ),
            eosio_assert_message_exception,
            eosio_assert_message_is("unable to dismiss claim while this case file is in this status")
    );

    BOOST_REQUIRE_EXCEPTION(
            acceptclaim(current_case_id, test_voters[0], claim_links[1], ruling_links[1], A_TORT   ),
            eosio_assert_message_exception,
            eosio_assert_message_is("Only the assigned arbitrator can accept a claim")
    );

    //assigntocase
    //cf = get_casefile(current_case_id);
    auto assigned_arbs = cf["arbitrators"].as<vector<fc::variant>>();
    BOOST_REQUIRE_EQUAL(assigned_arbs.size(), 0);
    assigntocase(current_case_id, test_voters[0], assigner);

    cf = get_casefile(current_case_id);
   //  std::cout << "Case status: " << cf["case_status"].as_string() << endl;
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), CASE_INVESTIGATION );
    assigned_arbs = cf["arbitrators"].as<vector<fc::variant>>();
    BOOST_REQUIRE_EQUAL(assigned_arbs.size(), 1);
    BOOST_REQUIRE_EQUAL(assigned_arbs[0].as_string(), test_voters[0].to_string());

	//respond
	respond(current_case_id, claim_links[0], respondant, response_links[0]);
    produce_blocks();

    auto claim = get_unread_claim(current_case_id, claim_links[0]);
    BOOST_REQUIRE_EQUAL(false, claim.is_null());

    REQUIRE_MATCHING_OBJECT (claim,
            mvo()
    ("claim_id",0)
    ("claim_summary", claim_links[0])
    ("decision_link", "")
    ("response_link", response_links[0])
    ("decision_class", 0)
    );

    respond(current_case_id, claim_links[1], respondant, response_links[1]);
    produce_blocks();

    claim = get_unread_claim(current_case_id, claim_links[1]);
    BOOST_REQUIRE_EQUAL(false, claim.is_null());

    REQUIRE_MATCHING_OBJECT (claim,
            mvo()
    ("claim_id",0)
    ("claim_summary", claim_links[1])
    ("decision_link", "")
    ("response_link", response_links[1])
    ("decision_class", 0)
    );
	//dismissclaim

    claim = get_unread_claim(current_case_id, claim_links[0]);
    BOOST_REQUIRE_EQUAL(false, claim.is_null());

    BOOST_REQUIRE_EXCEPTION(
        dismissclaim(current_case_id, bad_actor, claim_links[0], "The claim is not valid.  Dismissed" ),
        eosio_assert_message_exception,
        eosio_assert_message_is("Only an assigned arbitrator can dismiss a claim")
    );

    dismissclaim(current_case_id, test_voters[0], claim_links[0], "The claim is not valid.  Dismissed" );

    claim = get_unread_claim(current_case_id, claim_links[0]);
    BOOST_REQUIRE_EQUAL(true, claim.is_null());


    //acceptclaim with out of range decision_class
    BOOST_REQUIRE_EXCEPTION (
        acceptclaim(current_case_id, test_voters[0], claim_links[3], ruling_links[3],  40  ),
        eosio_assert_message_exception,
        eosio_assert_message_is("decision_class must be valid [2 - 15]")
    );

	//acceptclaim
    acceptclaim(current_case_id, test_voters[0], claim_links[1], ruling_links[1], A_TORT   );

    // check claim was removed from unread claims
    claim = get_unread_claim(current_case_id, claim_links[1]);
    BOOST_REQUIRE_EQUAL(true, claim.is_null());

    //check accept claim(s) were moved into claim table
    auto claim_accepted = get_claim(0);
    //std::cout << "Claim Summary: " << claim_accepted["claim_summary"].as_string() << endl;
    REQUIRE_MATCHING_OBJECT (claim_accepted,
                mvo()
    ("claim_id",0)
    ("claim_summary", claim_links[1])
    ("decision_link", ruling_links[1])
    ("response_link", response_links[1])
    ("decision_class", 9 )
    );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( case_resolution, eosio_arb_tester ) try {
    //filecase
    elect_arbitrators(8, 10); // test_voters 0-7 are arbitrators, 8-17 voted for 0-7
    newarbstatus(AVAILABLE, test_voters[0]);
    uint64_t current_case_id = 0;

    filecase(claimant, claim_links[0], lang_codes, respondant);
    produce_blocks();

    //addclaims x 3
    addclaim(current_case_id, claim_links[1], claimant);
    addclaim(current_case_id, claim_links[2], claimant);
    addclaim(current_case_id, claim_links[3], claimant);
    auto cf = get_casefile(current_case_id);

    //transfer funds
    transfer(N(eosio), claimant.value, asset::from_string("1000.0000 TLOS"), "");
    transfer(claimant.value, N(eosio.arb), asset::from_string("200.0000 TLOS"), "");

    //readycase
    readycase(current_case_id, claimant);
    cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), AWAITING_ARBS);
    produce_blocks();

    //assigntocase
    //cf = get_casefile(current_case_id);
    auto assigned_arbs = cf["arbitrators"].as<vector<fc::variant>>();
    BOOST_REQUIRE_EQUAL(assigned_arbs.size(), 0);
    assigntocase(current_case_id, test_voters[0], assigner);

    cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), CASE_INVESTIGATION );
    assigned_arbs = cf["arbitrators"].as<vector<fc::variant>>();

    //respond
    respond(current_case_id, claim_links[0], respondant, response_links[0]);
    produce_blocks();

    respond(current_case_id, claim_links[1], respondant, response_links[1]);
    produce_blocks();

	//dismissclaim
    dismissclaim(current_case_id, test_voters[0], claim_links[0], "The claim is not valid.  Dismissed" );

    auto claim = get_unread_claim(current_case_id, claim_links[0]);
    BOOST_REQUIRE_EQUAL(true, claim.is_null());

    //acceptclaim
    acceptclaim(current_case_id, test_voters[0], claim_links[1], ruling_links[1], A_TORT   );

    // check claim was removed from unread claims
    claim = get_unread_claim(current_case_id, claim_links[1]);
    BOOST_REQUIRE_EQUAL(true, claim.is_null());

	//advancecase
    advancecase(current_case_id, test_voters[0]);
    cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), HEARING );
    produce_blocks();

    advancecase(current_case_id, test_voters[0]);
    cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), DELIBERATION );
    produce_blocks();

    advancecase(current_case_id, test_voters[0]);
    cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), DECISION );
    produce_blocks();

    BOOST_REQUIRE_EXCEPTION(
            setruling(current_case_id, bad_actor, ruling_links[0]),
            eosio_assert_message_exception,
            eosio_assert_message_is("case_status must be ENFORCEMENT")
    );

    advancecase(current_case_id, test_voters[0]);
    cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), ENFORCEMENT );
    produce_blocks();

    //resolvecase
    BOOST_REQUIRE_EXCEPTION(
    setruling(current_case_id, bad_actor, ruling_links[0]),
    eosio_assert_message_exception,
    eosio_assert_message_is("arbitrator is not assigned to this case_id")
    );

    setruling(current_case_id, test_voters[0], ruling_links[0]);
    advancecase(current_case_id, test_voters[0]);

    cf = get_casefile(current_case_id);
    BOOST_REQUIRE_EQUAL ( cf["case_status"].as<uint8_t>(), RESOLVED );
    BOOST_REQUIRE_EQUAL ( cf["case_ruling"].as_string(), ruling_links[0] );




} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( dismiss_arb, eosio_arb_tester ) try { //TODO: for peter
	elect_arbitrators(8, 10); // test_voters 0-7 are arbitrators, 8-17 voted for 0-7
	newarbstatus(AVAILABLE, test_voters[0]);
	newarbstatus(AVAILABLE, test_voters[1]);
	BOOST_REQUIRE_EQUAL(AVAILABLE, get_arbitrator(test_voters[0])["arb_status"].as<uint8_t>());

	dismissarb(test_voters[0], false);
	produce_blocks();

	BOOST_REQUIRE_EXCEPTION(
		dismissarb(test_voters[0], false),
		eosio_assert_message_exception,
		eosio_assert_message_is("arbitrator is already removed or their seat has expired")
    );

	BOOST_REQUIRE_EQUAL(REMOVED, get_arbitrator(test_voters[0])["arb_status"].as<uint8_t>());
	uint64_t current_case_id = 0;
	filecase(claimant, claim_link1, lang_codes, respondant);
	produce_blocks();
	
	BOOST_REQUIRE_EQUAL(false, get_casefile(current_case_id).is_null());
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link1).is_null());
	addclaim(current_case_id, claim_link2, claimant);
	BOOST_REQUIRE_EQUAL(false, get_unread_claim(current_case_id, claim_link2).is_null());

	transfer(N(eosio), claimant.value, asset::from_string("1000.0000 TLOS"), "");
	transfer(claimant.value, N(eosio.arb), asset::from_string("200.0000 TLOS"), "");

	readycase(current_case_id, claimant);
	produce_blocks();

	BOOST_REQUIRE_EXCEPTION(
		assigntocase(current_case_id, test_voters[0], assigner),
		eosio_assert_message_exception,
		eosio_assert_message_is("Arbitrator has been removed.")
    );

	assigntocase(current_case_id, test_voters[1], assigner);
	auto cf = get_casefile(current_case_id);
	auto case_arbs = cf["arbitrators"].as<vector<name>>();

	BOOST_REQUIRE_EQUAL(1, case_arbs.size());
	BOOST_REQUIRE_EQUAL(test_voters[1].to_string(), case_arbs[0].to_string());

	auto arb = get_arbitrator(test_voters[1]);
	auto open_ids = arb["open_case_ids"].as<vector<uint64_t>>();

	BOOST_REQUIRE_EQUAL(1, open_ids.size());
	BOOST_REQUIRE_EQUAL(current_case_id, open_ids[0]);

	dismissarb(test_voters[1], true);

	cf = get_casefile(current_case_id);
	case_arbs = cf["arbitrators"].as<vector<name>>();

	BOOST_REQUIRE_EQUAL(0, case_arbs.size());

	arb = get_arbitrator(test_voters[1]);
	open_ids = arb["open_case_ids"].as<vector<uint64_t>>();

	BOOST_REQUIRE_EQUAL(0, open_ids.size());
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()