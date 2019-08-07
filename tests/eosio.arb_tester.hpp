#include "eosio.trail_tester.hpp"

using namespace eosio::testing;
using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;
using namespace std;

class eosio_arb_tester : public eosio_trail_tester {
public:

	const name claimant = name("claimant");
	const name respondant = name("respondant");
	const name bad_actor = name("badactor");
	const name assigner = name("assigner");
	const name non_claimant = name("nonclaimant");

	const string claim_link1 = "QmPcfm42GpRbShfMv7aBuXSN3mWwmbmiq6ZrRT1LtqL3ZX";
	const string claim_link2 = "QmaR8uSSy9MxFs6cftuaUNfirA6ypHKPpfB7eStds4AYDh";
    const vector<string> claim_links = {
            "QmaqXXdxX8CSDrLHidjRk51RVdGhyLLX1w8SMyudNy7Aye",
            "QmQUxGfBBctLVQrAEQJjWHSLufaBUAzncRAf2o3Ue88n55",
            "QmVXaHjxByYNsh7ZwzoJMWmpDQ6pTzzBqAymH9XK2JkvRK",
            "QmTqdYyLPzZ5VjLB5v4yS44NVsjfYuBPtjMj5B8U8uPQMm",
    };


    const string response_link1 = "QmQeqYBXbQZ91QVrLdczc43z5RKurPqJiZze3jpTQ7MY3G";
	const string response_link2 = "QmfJcVYGDeK4SBudEPXWKRBG4dmTFapmUHJGx2SfpR5qPW";
    const vector<string> response_links ={
            "QmR1mWk6DtJEC6k7qgucuhNyu3g6WsEmrb6Z3wMKprq43e",
            "QmV59dyJn3RoJz1axfMk1BL2MMgj1iMFf99GR1w2veg9mK",
    };

    const vector<string> ruling_links = {
            "QmekkVz5Jy1VgyQgH31p7LMfc6euXWfiHxPat2yf8BuhTM",
            "QmQwNsNHBbG5WRai6jnmW2b3QdwVLxxQn4tiPcSgSEHnat",
            "Qmcgk5fmBqXweDYq816i7m1abdGfzzbG2XY66AC1DvuWp7",
            "QmbnYHXjb4qpC8GzvmgMUY5u3p2cjarTrNopkkjDQWhjuN",
    };

	const vector<uint8_t> lang_codes = {0, 1, 2};

    abi_serializer abi_ser;

    eosio_arb_tester() {
        deploy_contract();
        produce_blocks(1);
    }

    void deploy_contract() {
        create_accounts({N(eosio.arb)});

        set_code(N(eosio.arb), contracts::eosio_arb_wasm());
        set_abi(N(eosio.arb), contracts::eosio_arb_abi().data());
        {
            const auto &accnt = control->db().get<account_object, by_name>(N(eosio.arb));
            abi_def abi;
            BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
            abi_ser.set_abi(abi, abi_serializer_max_time);
        }

		create_accounts({
			claimant.value, 
			respondant.value, 
			non_claimant.value,
			assigner.value,
			bad_actor.value
		});

		updateauth(
			name("eosio.arb"), 
			name("assign"), 
			name("active"), 
			authority{
				1,
				vector<key_weight> {},
				vector<permission_level_weight> {
					permission_level_weight {
						permission_level {
							assigner.value,
							N(active)
						},
						1
					}
				},
				vector<wait_weight> {}
			}
		);

		linkauth(name("eosio.arb"), name("eosio.arb"), name("assigntocase"), name("assign"));
		produce_blocks();
    }

#pragma region get_tables

    fc::variant get_config() {
        vector<char> data = get_row_by_account(N(eosio.arb), N(eosio.arb), N(config), N(config));
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant("config", data, abi_serializer_max_time);
    }

    fc::variant get_nominee(uint64_t nominee_id) {
        vector<char> data = get_row_by_account(N(eosio.arb), N(eosio.arb), N(nominees), nominee_id);
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant("nominee", data, abi_serializer_max_time);
    }

    fc::variant get_arbitrator(uint64_t arbitrator_id) {
        vector<char> data = get_row_by_account(N(eosio.arb), N(eosio.arb), N(arbitrators), arbitrator_id);
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant("arbitrator", data, abi_serializer_max_time);
    }

    fc::variant get_casefile(uint64_t casefile_id) {
        vector<char> data = get_row_by_account(N(eosio.arb), N(eosio.arb), N(casefiles), casefile_id);
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant("casefile", data, abi_serializer_max_time);
    }

    fc::variant get_unread_claim(uint64_t casefile_id, uint8_t claim_id) {
        auto cf = get_casefile(casefile_id);

        BOOST_REQUIRE_EQUAL(false, cf.is_null());
        auto unread_claims = cf["unread_claims"].as < vector < mvo > > ();
        return unread_claims[claim_id];
        //vector<char> data = get_row_by_accounts(N(eosio.arb),N(eosio.arb), N(unread_claims), casefile_id);
        //return data.empty()? fc::variant() : abi_ser.binary_to_variant("unread_claim", data, abi_serializer_max_time);
    }

    fc::variant get_unread_claim(uint64_t casefile_id, string claim_link) {
        auto cf = get_casefile(casefile_id);

        BOOST_REQUIRE_EQUAL(false, cf.is_null());
        auto unread_claims = cf["unread_claims"].as < vector < mvo > > ();

        auto it = find_if(unread_claims.begin(), unread_claims.end(), [&](auto c) {
            return claim_link == c["claim_summary"];
        });
        return it == unread_claims.end() ? fc::variant() : *it;
    }

    fc::variant get_claim(uint64_t claim_id) {
        vector<char> data = get_row_by_account(N(eosio.arb), N(eosio.arb), N(claims), claim_id);
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant("claim", data, abi_serializer_max_time);
    }

    #pragma endregion get_tables

	void elect_arbitrators(uint8_t num_arbitrators, uint8_t num_voters) {
		const symbol vote_sym = symbol(4, "VOTE");
		const symbol tlos_sym = symbol(4, "TLOS");
		auto one_day = 86400;
   		uint32_t start_election = 300, election_duration = 300, arbitrator_term_length = one_day * 10;
		vector<int64_t> fees = { int64_t(1), int64_t(2), int64_t(3), int64_t(4) };
		uint16_t max_elected_arbs = 20;

		// setup config
		setconfig ( max_elected_arbs, start_election, election_duration, arbitrator_term_length, fees);
		produce_blocks(1);

		init_election();
		uint32_t expected_begin_time = uint32_t(now() + start_election);
		uint32_t expected_end_time = expected_begin_time + election_duration;

		auto config = get_config();
		auto cbid = config["current_ballot_id"].as_uint64();   

		auto ballot = get_ballot(cbid);
		auto bid = ballot["reference_id"].as_uint64();

		auto leaderboard = get_leaderboard(bid);
		auto lid = leaderboard["board_id"].as_uint64();

		BOOST_REQUIRE_EQUAL(expected_begin_time, leaderboard["begin_time"].as<uint32_t>());
   		BOOST_REQUIRE_EQUAL(expected_end_time, leaderboard["end_time"].as<uint32_t>());

		BOOST_REQUIRE_EQUAL(bid, lid);
   		BOOST_REQUIRE_EQUAL(cbid, lid);

		voter_map(0, num_arbitrators, [&](auto& account) {
			regarb(account, "ipfs://123456jkfadfhjlkldfajldfshjkldfahjfdsghaleedkjaagkso");
			candaddlead(account, "ipfs://123456jkfadfhjlkldfajldfshjkldfahjfdsghaleedkjaagkso");
			produce_blocks();
		});

		produce_block(fc::seconds(300));
		produce_blocks();

		voter_map(num_arbitrators, num_arbitrators + num_voters, [&](auto& account) {
			regvoter(account, vote_sym);
			mirrorcast(account, tlos_sym);
			for (uint8_t i = 0; i < num_arbitrators; i++) {
				castvote(account, bid, i);
			}
			produce_blocks();
		});

		produce_block(fc::seconds(300));
		produce_blocks();
		
		endelection(test_voters[0]);

		voter_map(0, num_arbitrators, [&](auto& account) {
			auto arb = get_arbitrator(account);
			BOOST_REQUIRE_EQUAL(false, arb.is_null());
		});
	}

    #pragma region actions

    transaction_trace_ptr setconfig(uint16_t max_elected_arbs, uint32_t election_duration, uint32_t start_election, uint32_t arbitrator_term_length, vector<int64_t> fees) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(setconfig), vector<permission_level>{{N(eosio), config::active_name}}, mvo()
            ("max_elected_arbs", max_elected_arbs)
            ("election_duration", election_duration)
            ("start_election", start_election)
            ("arbitrator_term_length", arbitrator_term_length)
            ("fees", fees))
        );

        set_transaction_headers(trx);
        trx.sign(get_private_key(N(eosio), "active"), control->get_chain_id());
        return push_transaction(trx);
    }


    transaction_trace_ptr init_election() {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(initelection), vector<permission_level>{{N(eosio), config::active_name}}, mvo()));
        set_transaction_headers(trx);
        trx.sign(get_private_key(N(eosio), "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr regarb(name nominee, string credentials_link) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(regarb), vector<permission_level>{{nominee, config::active_name}}, mvo()
            ("nominee", nominee)
            ("credentials_link", credentials_link)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(nominee, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr unregnominee(name nominee) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(unregnominee), vector<permission_level>{{nominee, config::active_name}}, mvo()
            ("nominee", nominee)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(nominee, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr candaddlead(name nominee, string credentials_link) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(candaddlead), vector<permission_level>{{nominee, config::active_name}}, mvo()
            ("nominee", nominee)
            ("credentials_link", credentials_link)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(nominee, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr candrmvlead(name nominee) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(candrmvlead), vector<permission_level>{{nominee, config::active_name}}, mvo()
            ("nominee", nominee)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(nominee, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr endelection(name nominee) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(endelection), vector<permission_level>{{nominee, config::active_name}}, mvo()
            ("nominee", nominee)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(nominee, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    //note: case setup

    transaction_trace_ptr filecase(name claimant, string claim_link, vector <uint8_t> lang_codes, optional<name> respondant ) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(filecase), vector<permission_level>{{claimant, config::active_name}}, mvo()
            ("claimant", claimant)
            ("claim_link", claim_link)
            ("lang_codes", lang_codes)
            ("respondant", respondant)
            ));
        set_transaction_headers(trx);
        trx.sign(get_private_key(claimant, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr addclaim(uint64_t case_id, string claim_link, name claimant) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(addclaim), vector<permission_level>{{claimant, config::active_name}}, mvo()
            ("case_id", case_id)
            ("claim_link", claim_link)
            ("claimant", claimant)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(claimant, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr removeclaim(uint64_t case_id, string claim_hash, name claimant) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(removeclaim), vector<permission_level>{{claimant, config::active_name}}, mvo()
            ("case_id", case_id)
            ("claim_hash", claim_hash)
            ("claimant", claimant)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(claimant, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr shredcase(uint64_t case_id, name claimant) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(shredcase), vector<permission_level>{{claimant, config::active_name}}, mvo()
            ("case_id", case_id)
            ("claimant", claimant)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(claimant, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr readycase(uint64_t case_id, name claimant) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(readycase), vector<permission_level>{{claimant, config::active_name}}, mvo()
            ("case_id", case_id)
            ("claimant", claimant)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(claimant, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    //note: case progression

    transaction_trace_ptr assigntocase(uint64_t case_id, name arb_to_assign, name assigner) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(assigntocase), vector<permission_level>{{N(eosio.arb), N(assign)}}, mvo()
            ("case_id", case_id)
            ("arb_to_assign", arb_to_assign)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(assigner, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

	transaction_trace_ptr updateauth(name account, name permission, name parent, authority auth) {
		signed_transaction trx;
		trx.actions.emplace_back(get_action(N(eosio), N(updateauth), vector<permission_level>{{account, config::active_name}},
			mvo()
				("account", account)
				("permission", permission)
				("parent", parent)
				("auth", auth)
		));
		set_transaction_headers(trx);
		trx.sign(get_private_key(account, "active"), control->get_chain_id());
		return push_transaction(trx);
	}

	transaction_trace_ptr withdraw(name owner) {
		signed_transaction trx;
		trx.actions.emplace_back(get_action(N(eosio.arb), N(withdraw), vector<permission_level>{{owner, config::active_name}},
			mvo()
				("owner", owner)
		));
		set_transaction_headers(trx);
		trx.sign(get_private_key(owner, "active"), control->get_chain_id());
		return push_transaction(trx);
	}

	transaction_trace_ptr linkauth(name account, name code, name type, name requirement) {
		signed_transaction trx;
		trx.actions.emplace_back(get_action(N(eosio), N(linkauth), vector<permission_level>{{account, config::active_name}},
			mvo()
				("account", account)
				("code", code)
				("type", type)
				("requirement", requirement)
		));
		set_transaction_headers(trx);
		trx.sign(get_private_key(account, "active"), control->get_chain_id());
		return push_transaction(trx);
	}

    transaction_trace_ptr dismissclaim(uint64_t case_id, name assigned_arb, string claim_hash, string memo) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(dismissclaim), vector<permission_level>{{assigned_arb, config::active_name}}, mvo()
            ("case_id", case_id)
            ("assigned_arb", assigned_arb)
            ("claim_hash", claim_hash)
            ("memo", memo)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(assigned_arb, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr acceptclaim(uint64_t case_id, name assigned_arb, string claim_hash, string decision_link, uint8_t decision_class) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(acceptclaim), vector<permission_level>{{assigned_arb, config::active_name}}, mvo()
            ("case_id", case_id)
            ("assigned_arb", assigned_arb)
            ("claim_hash", claim_hash)
            ("decision_link", decision_link)
            ("decision_class", decision_class)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(assigned_arb, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

	transaction_trace_ptr respond(uint64_t case_id, string claim_hash, name respondant, string response_link) {
		signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(respond), vector<permission_level>{{respondant, config::active_name}}, mvo()
            ("case_id", case_id)
            ("claim_hash", claim_hash)
            ("respondant", respondant)
            ("response_link", response_link)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(respondant, "active"), control->get_chain_id());
        return push_transaction(trx);
	}

    transaction_trace_ptr dismisscase(uint64_t case_id, name assigned_arb, string ruling_link) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(dismisscase), vector<permission_level>{{assigned_arb, config::active_name}}, mvo()
            ("case_id", case_id)
            ("assigned_arb", assigned_arb)
            ("ruling_link", ruling_link)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(assigned_arb, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr advancecase(uint64_t case_id, name assigned_arb) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(advancecase), vector<permission_level>{{assigned_arb, config::active_name}}, mvo()
            ("case_id", case_id)
            ("assigned_arb", assigned_arb)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(assigned_arb, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    transaction_trace_ptr recuse(uint64_t case_id, string rationale, name assigned_arb) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(recuse), vector<permission_level>{{assigned_arb, config::active_name}}, mvo()
            ("case_id", case_id)
            ("rationale", rationale)
            ("assigned_arb", assigned_arb)));
        set_transaction_headers(trx);
        trx.sign(get_private_key(assigned_arb, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

	transaction_trace_ptr setruling(uint64_t case_id, name assigned_arb, string case_ruling) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(setruling), vector<permission_level>{{assigned_arb, config::active_name}}, mvo()
            ("case_id", case_id)
            ("assigned_arb", assigned_arb)
            ("case_ruling", case_ruling)
		));
        set_transaction_headers(trx);
        trx.sign(get_private_key(assigned_arb, "active"), control->get_chain_id());
        return push_transaction(trx);
    }

	transaction_trace_ptr newarbstatus(uint8_t new_status, name arbitrator)	{
		signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(newarbstatus), vector<permission_level>{{arbitrator, config::active_name}}, mvo()
            ("new_status", new_status)
            ("arbitrator", arbitrator)
		));
        set_transaction_headers(trx);
        trx.sign(get_private_key(arbitrator, "active"), control->get_chain_id());
        return push_transaction(trx);
	}

	transaction_trace_ptr addarbs(uint64_t case_id, name assigned_arb, uint8_t num_arbs_to_assign) {
		signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(addarbs), vector<permission_level>{{assigned_arb, config::active_name}}, mvo()
            ("case_id", case_id)
            ("assigned_arb", assigned_arb)
			("num_arbs_to_assign", num_arbs_to_assign)
		));
        set_transaction_headers(trx);
        trx.sign(get_private_key(assigned_arb, "active"), control->get_chain_id());
        return push_transaction(trx);
	}

    transaction_trace_ptr dismissarb(name arb, bool remove_from_cases) {
        signed_transaction trx;
        trx.actions.emplace_back(get_action(N(eosio.arb), N(dismissarb), vector<permission_level>{{N(eosio), config::active_name}},
                mvo()
					("arb", arb)
					("remove_from_cases", remove_from_cases)
		));
        set_transaction_headers(trx);
        trx.sign(get_private_key(N(eosio), "active"), control->get_chain_id());
        return push_transaction(trx);
    }

    #pragma endregion actions

	#pragma region native_structs

	#pragma endregion native_structs


    #pragma region enums


    enum case_state : uint8_t
    {
        CASE_SETUP,			// 0
        AWAITING_ARBS,		// 1
        CASE_INVESTIGATION, // 2
        HEARING,			// 3
        DELIBERATION,		// 4
        DECISION,			// 5 NOTE: No more joinders allowed
        ENFORCEMENT,		// 6
        RESOLVED,			// 7
        DISMISSED			// 8 NOTE: Dismissed cases advance and stop here
    };

    enum claim_class : uint8_t
    {
        UNDECIDED			= 1,
        LOST_KEY_RECOVERY	= 2,
        TRX_REVERSAL		= 3,
        EMERGENCY_INTER		= 4,
        CONTESTED_OWNER		= 5,
        UNEXECUTED_RELIEF	= 6,
        CONTRACT_BREACH		= 7,
        MISUSED_CR_IP		= 8,
        A_TORT				= 9,
        BP_PENALTY_REVERSAL	= 10,
        WRONGFUL_ARB_ACT 	= 11,
        ACT_EXEC_RELIEF		= 12,
        WP_PROJ_FAILURE		= 13,
        TBNOA_BREACH		= 14,
        MISC				= 15
    };

    enum arb_status : uint8_t
    {
        AVAILABLE,    // 0
        UNAVAILABLE,  // 1
        REMOVED,	  // 2
        SEAT_EXPIRED  // 3
    };

    enum election_status : uint8_t
    {
        OPEN,   // 0
        PASSED, // 1
        FAILED, // 2
        CLOSED  // 3
    };

    enum lang_code : uint8_t
    {
        ENGL, //0 English
        FRCH, //1 French
        GRMN, //2 German
        KREA, //3 Korean
        JAPN, //4 Japanese
        CHNA, //5 Chinese
        SPAN, //6 Spanish
        PGSE, //7 Portuguese
        SWED  //8 Swedish
    };

    #pragma endregion enums

};