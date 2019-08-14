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

#include "eosio.tedp_tester.hpp"

using namespace std;


const uint64_t daily_interval = 86400;
const uint64_t rex_interval = 1800;

const uint64_t max_econdev_amount = 16438;
const uint64_t max_tf_amount = 32876;
const uint64_t max_rex_amount = 685;

BOOST_AUTO_TEST_SUITE(eosio_tedp_tests)

BOOST_FIXTURE_TEST_CASE( set_payouts, eosio_tedp_tester ) try {
    validate_payout([&](uint64_t amount) -> transaction_trace_ptr { return settf(amount); }, 
        name("tf"), max_tf_amount, daily_interval);

    validate_payout([&](uint64_t amount) -> transaction_trace_ptr { return setecondev(amount); }, 
        name("econdevfunds"), max_econdev_amount, daily_interval);

    validate_payout([&](uint64_t amount) -> transaction_trace_ptr { return setrex(amount); }, 
        name("eosio.rex"), max_rex_amount, rex_interval);

    validate_payout([&](uint64_t amount) -> transaction_trace_ptr { return setrex(amount); }, 
        name("eosio.rex"), max_rex_amount - 10, rex_interval);

    validate_payout([&](uint64_t amount) -> transaction_trace_ptr { return setecondev(amount); }, 
        name("econdevfunds"), max_econdev_amount - 10, daily_interval);

    BOOST_REQUIRE_EXCEPTION(settf(max_tf_amount + 1),
		eosio_assert_message_exception, eosio_assert_message_is( "Max amount for tf account is 32876 per day" ) 
   	);

    BOOST_REQUIRE_EXCEPTION(setecondev(max_econdev_amount + 1),
		eosio_assert_message_exception, eosio_assert_message_is( "Max amount for econdevfunds account is 16438 per day" ) 
   	);

    BOOST_REQUIRE_EXCEPTION(setrex(max_rex_amount + 1),
		eosio_assert_message_exception, eosio_assert_message_is( "Max amount for eosio.rex account is 685 per 30min" ) 
   	);

    validate_payout_del([&](const name payout_name) -> transaction_trace_ptr {
        return delpayout(payout_name);
    }, name("tf"));

    validate_payout_del([&](const name payout_name) -> transaction_trace_ptr {
        return delpayout(payout_name);
    }, name("econdevfunds"));

    validate_payout_del([&](const name payout_name) -> transaction_trace_ptr {
        return delpayout(payout_name);
    }, name("eosio.rex"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( pay_flow, eosio_tedp_tester ) try {
    transfer( config::system_account_name, test_account, core_sym::from_string("139873804.3119"), config::system_account_name );
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

    validate_payout([&](uint64_t amount) -> transaction_trace_ptr { return settf(amount); }, 
        name("tf"), max_tf_amount, daily_interval);

    validate_payout([&](uint64_t amount) -> transaction_trace_ptr { return setecondev(amount); }, 
        name("econdevfunds"), max_econdev_amount, daily_interval);

    validate_payout([&](uint64_t amount) -> transaction_trace_ptr { return setrex(amount); }, 
        name("eosio.rex"), max_rex_amount, rex_interval);

    auto dump_trace = [&](transaction_trace_ptr trace_ptr) -> transaction_trace_ptr {
        cout << endl << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<" << endl;
        for(auto trace : trace_ptr->action_traces) {
            cout << "action_name trace: " << trace.act.name.to_string() << endl;
            //TODO: split by new line character, loop and indent output
            cout << trace.console << endl << endl;
        }
        cout << endl << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>" << endl << endl;
        return trace_ptr;
    };

    produce_blocks();
    produce_block(fc::seconds(daily_interval));
    produce_blocks(10);

    fc::variant initial_rex_pool = get_rex_pool();
    asset initial_total_unlent = initial_rex_pool["total_unlent"].as<asset>();
    asset initial_total_lendable = initial_rex_pool["total_lendable"].as<asset>();
    uint64_t last_payout      = get_payout(N(tf))["last_payout"].as<uint64_t>();

    asset initial_tf_balance = get_balance(N(tf));
    asset initial_econ_balance = get_balance(N(econdevfunds));

    auto trace = dump_trace(payout());
    BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

    vector<name> payout_names { name("eosio.rex"), name("tf"), name("econdevfunds") };
    
    for(const auto &payout : payout_names) {
        fc::variant payout_info = get_payout(payout);

        uint64_t time_since_last_payout = now() - last_payout;
        uint64_t payouts_due = time_since_last_payout / payout_info["interval"].as<uint64_t>();
        uint64_t total_due = payouts_due * payout_info["amount"].as<uint64_t>();
        asset total_payout = asset(total_due * 10000, symbol(4, "TLOS"));

        if (payout_info["to"].as<name>() == N(eosio.rex)) {
            fc::variant rex_pool = get_rex_pool();
            BOOST_REQUIRE_EQUAL(rex_pool["total_unlent"].as<asset>(), initial_total_unlent + total_payout);
            BOOST_REQUIRE_EQUAL(rex_pool["total_lendable"].as<asset>(), initial_total_lendable + total_payout);
        } else {
            auto initial_balance = (payout == N(econdevfunds) ? initial_econ_balance : initial_tf_balance);
            cout << "initial_balance: " << initial_balance << endl;
            BOOST_REQUIRE_EQUAL(get_balance(payout), initial_balance + total_payout);
        }
    }

    produce_blocks();
    produce_block(fc::seconds(daily_interval * 2));
    produce_blocks(10);

    initial_rex_pool = get_rex_pool();
    initial_total_unlent = initial_rex_pool["total_unlent"].as<asset>();
    initial_total_lendable = initial_rex_pool["total_lendable"].as<asset>();
    last_payout      = get_payout(N(tf))["last_payout"].as<uint64_t>();

    initial_tf_balance = get_balance(N(tf));
    initial_econ_balance = get_balance(N(econdevfunds));

    trace = dump_trace(payout());
    BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);
    
    for(const auto &payout : payout_names) {
        fc::variant payout_info = get_payout(payout);

        uint64_t time_since_last_payout = now() - last_payout;
        uint64_t payouts_due = time_since_last_payout / payout_info["interval"].as<uint64_t>();
        uint64_t total_due = payouts_due * payout_info["amount"].as<uint64_t>();
        asset total_payout = asset(total_due * 10000, symbol(4, "TLOS"));

        if (payout_info["to"].as<name>() == N(eosio.rex)) {
            fc::variant rex_pool = get_rex_pool();
            BOOST_REQUIRE_EQUAL(rex_pool["total_unlent"].as<asset>(), initial_total_unlent + total_payout);
            BOOST_REQUIRE_EQUAL(rex_pool["total_lendable"].as<asset>(), initial_total_lendable + total_payout);
        } else {
            auto initial_balance = (payout == N(econdevfunds) ? initial_econ_balance : initial_tf_balance);
            BOOST_REQUIRE_EQUAL(get_balance(payout), initial_balance + total_payout);
        }
    }
    
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()