#pragma once

#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/resource_limits.hpp>
#include "contracts.hpp"
#include "test_symbol.hpp"
#include "eosio.system_tester.hpp"

#include <fc/variant_object.hpp>
#include <fstream>

using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;
using namespace std;

using mvo = fc::mutable_variant_object;

#ifndef TESTER
#ifdef NON_VALIDATING_TEST
#define TESTER tester
#else
#define TESTER validating_tester
#endif
#endif

class eosio_tedp_tester : public eosio_system::eosio_system_tester { 
public:
    abi_serializer tedp_abi_ser;

    eosio_tedp_tester() {
        produce_blocks( 2 );

        create_accounts_with_resources({ N(tf), N(econdevfunds), N(exrsrv.tf) });

        set_code( N(eosio.tedp), contracts::eosio_tedp_wasm() );
        set_abi( N(eosio.tedp), contracts::eosio_tecp_abi().data() );
        {
            const auto& accnt = control->db().get<account_object, by_name>(N(eosio.tedp));
            abi_def abi;
            BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
            tedp_abi_ser.set_abi(abi, abi_serializer_max_time);
        }

        issue( N(exrsrv.tf), core_sym::from_string("100000000.0000"));
        issue( N(eosio.tedp), core_sym::from_string("100000000.0000"));
    }

    asset get_balance( const account_name& act, symbol balance_symbol = symbol{CORE_SYM}, const account_name& contract = N(eosio.token) ) {
		return get_currency_balance(contract, balance_symbol, act);
	}

    void create_currency( name contract, name manager, asset maxsupply ) {
        auto act =  mutable_variant_object()
            ("issuer",       manager )
            ("maximum_supply", maxsupply );

        base_tester::push_action(contract, N(create), contract, act );
    }

    void create_core_token( symbol core_symbol = symbol{CORE_SYM} ) {
        FC_ASSERT( core_symbol.decimals() == 4, "create_core_token assumes core token has 4 digits of precision" );
        create_currency( N(eosio.token), config::system_account_name, asset(100000000000000, core_symbol) );
        issue(config::system_account_name, asset(10000000000000, core_symbol) );
        BOOST_REQUIRE_EQUAL( asset(10000000000000, core_symbol), get_balance( "eosio", core_symbol ) );
    }

    void issue( name to, const asset& amount, name manager = config::system_account_name ) {
        base_tester::push_action( N(eosio.token), N(issue), manager, mutable_variant_object()
            ("to",      to )
            ("quantity", amount )
            ("memo", ""));
    }

    transaction_trace_ptr settf(const uint64_t amount) {
        signed_transaction trx;
        action act = get_action(N(eosio.tedp), N(settf), vector<permission_level>{{N(eosio), config::active_name}},
			mvo()
			    ("amount", amount));
        trx.actions.emplace_back(act);
        set_transaction_headers(trx);
        trx.sign( get_private_key(N(eosio), "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr setecondev(const uint64_t amount) {
        signed_transaction trx;
        action act = get_action(N(eosio.tedp), N(setecondev), vector<permission_level>{{N(eosio), config::active_name}},
			mvo()
			    ("amount", amount));
        trx.actions.emplace_back(act);
        set_transaction_headers(trx);
        trx.sign( get_private_key(N(eosio), "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr setrex(const uint64_t amount) {
        signed_transaction trx;
        action act = get_action(N(eosio.tedp), N(setrex), vector<permission_level>{{N(eosio), config::active_name}},
			mvo()
			    ("amount", amount));
        trx.actions.emplace_back(act);
        set_transaction_headers(trx);
        trx.sign( get_private_key(N(eosio), "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr setdrawdown(const uint64_t amount) {
        signed_transaction trx;
        action act = get_action(N(eosio.tedp), N(setdrawdown), vector<permission_level>{{N(eosio), config::active_name}},
			mvo()
			    ("amount", amount));
        trx.actions.emplace_back(act);
        set_transaction_headers(trx);
        trx.sign( get_private_key(N(eosio), "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr delpayout(const name to) {
        signed_transaction trx;
        action act = get_action(N(eosio.tedp), N(delpayout), vector<permission_level>{{N(eosio), config::active_name}},
			mvo()
			    ("to", to));
        trx.actions.emplace_back(act);
        set_transaction_headers(trx);
        trx.sign( get_private_key(N(eosio), "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr payout() {
        signed_transaction trx;
        action act = get_action(N(eosio.tedp), N(pay), vector<permission_level>{{N(eosio), config::active_name}},
			mvo());
        trx.actions.emplace_back(act);
        set_transaction_headers(trx);
        trx.sign( get_private_key(N(eosio), "active"), control->get_chain_id() );
        return push_transaction(trx);
    }

    fc::variant get_payout(name to) {
      vector<char> data = get_row_by_account( N(eosio.tedp), N(eosio.tedp), N(payouts), to );
      return data.empty() ? fc::variant() : tedp_abi_ser.binary_to_variant("payout", data, abi_serializer_max_time);
    }

    uint32_t now() {
	   return (control->pending_block_time().time_since_epoch().count() / 1000000);
    }

    template<typename Lambda>
    void validate_payout(Lambda&& func, name payout_name, uint64_t amount, uint64_t interval) {
        auto trace = func(amount);
        BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);
        fc::variant payout = get_payout(payout_name);

        cout << payout_name << ": " << payout << endl;

        BOOST_REQUIRE(!payout.is_null());
        BOOST_REQUIRE_EQUAL(payout["to"].as<name>(), payout_name);
        BOOST_REQUIRE_EQUAL(payout["interval"].as<uint64_t>(), interval);
        BOOST_REQUIRE_EQUAL(payout["amount"].as<uint64_t>(), amount);
        BOOST_REQUIRE_EQUAL(payout["last_payout"].as<uint64_t>(), now());
    };

    template<typename Lambda>
    void validate_payout_del(Lambda&& func, name payout_name) {
        fc::variant payout = get_payout(payout_name);
        BOOST_REQUIRE(!payout.is_null());
        auto trace = func(payout_name);
        BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

        payout = get_payout(payout_name);
        BOOST_REQUIRE(payout.is_null());
    };
};