/**
 * Arbitration Contract Interface
 *
 * @author Craig Branscom, Peter Bue, Ed Silva, Douglas Horn
 * @copyright defined in telos/LICENSE.txt
 */

#pragma once
#include <trail.voting.hpp>
#include <eosiolib/action.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/permission.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/types.h>

using namespace std;
using namespace eosio;

//NOTE: for the purposes of test integrity this number should be kept at 1.
// main net value should be 10000000000
//TODO: refactor unit tests to work with MIN_VOTE_THRESHOLD value
#define MIN_VOTE_THRESHOLD 1 

class[[eosio::contract("eosio.arbitration")]] arbitration : public eosio::contract
{

  public:
	using contract::contract;

	const symbol native_sym = symbol("TLOS", 4);

	arbitration(name s, name code, datastream<const char *> ds);

	~arbitration();

	const uint8_t MAX_UNREAD_CLAIMS = 21;

	[[eosio::action]] void setconfig(uint16_t max_elected_arbs, uint32_t election_duration, uint32_t start_election, uint32_t arbitrator_term_length, vector<int64_t> fees);

#pragma region Enums

	//TODO: describe each enum in README

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

#pragma endregion Enums

	// [[eosio::action]] 
	// void injectarbs(vector<name> to_inject); //TODO: remove production deployment

	// [[eosio::action]]
	// void deleteclaim(uint64_t claim_id, name arb);

#pragma region Arb_Elections

	[[eosio::action]] 
	void initelection();

	[[eosio::action]] //REGCAND
	void regarb(name nominee, string credentials_link);
    //NOTE: actually regnominee, currently regarb for nonsense governance reasons

	[[eosio::action]] //UNREGCAND
	void unregnominee(name nominee);

	[[eosio::action]] //TODO: rename?
	void candaddlead(name nominee, string credentials_link);

	[[eosio::action]] //TODO: rename?
	void candrmvlead(name nominee);

	[[eosio::action]] //TODO: need nominee param?
	void endelection(name nominee);

#pragma endregion Arb_Elections

#pragma region Case_Setup

	[[eosio::action]] void withdraw(name owner);

	//NOTE: filing a case doesn't require a respondent
	[[eosio::action]] void filecase(name claimant, string claim_link, vector<uint8_t> lang_codes,
	        std::optional<name> respondant);

	[[eosio::action]] void execfile(uint64_t new_case_id, name claimant, string claim_link,
	        vector<uint8_t> lang_codes, name respondant);

	//NOTE: adds subsequent claims to a case
	[[eosio::action]] void addclaim(uint64_t case_id, string claim_link, name claimant);

	//NOTE: claims can only be removed by a claimant during case setup
	[[eosio::action]] void removeclaim(uint64_t case_id, string claim_hash, name claimant);

	//NOTE: member-level case removal, called during CASE_SETUP
	[[eosio::action]] void shredcase(uint64_t case_id, name claimant);

	//NOTE: enforce claimant has at least 1 claim before readying
	[[eosio::action]] void readycase(uint64_t case_id, name claimant);

#pragma endregion Case_Setup

#pragma region Case_Progression

	//NOTE: action used by respondant to respond to claimants accusation 
	[[eosio::action]] void respond(uint64_t case_id, string claim_hash, name respondant, string response_link);

	[[eosio::action]] void assigntocase(uint64_t case_id, name arb_to_assign);

	[[eosio::action]] void addarbs(uint64_t case_id, name assigned_arb, uint8_t num_arbs_to_assign);

	[[eosio::action]] void dismissclaim(uint64_t case_id, name assigned_arb, string claim_hash, string memo);

	[[eosio::action]] void acceptclaim(uint64_t case_id, name assigned_arb, string claim_hash, string decision_link,
	        uint8_t decision_class);

	[[eosio::action]] void execclaim(uint64_t new_claim_id, uint64_t case_id, name assigned_arb, string claim_hash,
                                     string decision_link, uint8_t decision_class);

	[[eosio::action]] void advancecase(uint64_t case_id, name assigned_arb);

	[[eosio::action]] void dismisscase(uint64_t case_id, name assigned_arb, string ruling_link);

	[[eosio::action]] void setruling(uint64_t case_id, name assigned_arb, string case_ruling);

	[[eosio::action]] void recuse(uint64_t case_id, string rationale, name assigned_arb);

	//NOTE: removed from v1, to be implemented in a future version.
	// [[eosio::action]] void newjoinder(uint64_t base_case_id, uint64_t joining_case_id, name arb); 

	// [[eosio::action]] void joincases(uint64_t joinder_id, uint64_t new_case_id, name arb);

#pragma endregion Case_Progression

#pragma region Arb_Actions

	[[eosio::action]] void setlangcodes(name arbitrator, vector<uint8_t> lang_codes);

	[[eosio::action]] void newarbstatus(uint8_t new_status, name arbitrator);

	[[eosio::action]] void deletecase(uint64_t case_id);
	
	//TODO: deletearb action, removes EXPIRED or REMOVED status arbs from the arbitrators table
#pragma endregion Arb_Actions

#pragma region BP_Multisig_Actions

	[[eosio::action]] void dismissarb(name arb, bool remove_from_cases);

	//TODO: affidavit action, forced recusal of arbitrator from a specified case.

#pragma endregion BP_Multisig_Actions

#pragma region System Structs

	struct permission_level_weight
	{
		permission_level permission;
		uint16_t weight;

		EOSLIB_SERIALIZE(permission_level_weight, (permission)(weight))
	};

	struct key_weight
	{
		eosio::public_key key;
		uint16_t weight;

		EOSLIB_SERIALIZE(key_weight, (key)(weight))
	};

	struct wait_weight
	{
		uint32_t wait_sec;
		uint16_t weight;

		EOSLIB_SERIALIZE(wait_weight, (wait_sec)(weight))
	};

	struct authority
	{
		uint32_t threshold = 0;
		std::vector<key_weight> keys;
		std::vector<permission_level_weight> accounts;
		std::vector<wait_weight> waits;

		EOSLIB_SERIALIZE(authority, (threshold)(keys)(accounts)(waits))
	};

#pragma endregion System Structs

#pragma region Tables and Structs

	/**
   * Holds all arbitrator nominee applications.
   * @scope get_self().value
   * @key uint64_t nominee_name.value
   */
	struct [[eosio::table]] nominee
	{
		name nominee_name;
		string credentials_link;
		uint32_t application_time;

		uint64_t primary_key() const { return nominee_name.value; }
		EOSLIB_SERIALIZE(nominee, (nominee_name)(credentials_link)(application_time))
	};

	/**
   * Holds all currently elected arbitrators.
   * @scope get_self().value
   * @key uint64_t arb.value
   */
	struct [[eosio::table]] arbitrator
	{
		name arb;
		uint8_t arb_status;
		vector<uint64_t> open_case_ids;
		vector<uint64_t> closed_case_ids;
		string credentials_link; //NOTE: ipfs_url of arbitrator credentials
		uint32_t elected_time;
		uint32_t term_expiration;
		vector<uint8_t> languages; //NOTE: language codes

		uint64_t primary_key() const { return arb.value; }
		EOSLIB_SERIALIZE(arbitrator, (arb)(arb_status)(open_case_ids)(closed_case_ids)(credentials_link)
		    (elected_time)(term_expiration)(languages))
	};

	//NOTE: Stores all information related to a single claim.
	struct [[eosio::table]] claim
	{
		uint64_t claim_id;
		string claim_summary; //NOTE: ipfs link to claim document from claimant
		string decision_link; //NOTE: ipfs link to decision document from arbitrator
		string response_link; //NOTE: ipfs link to response document from respondant (if any)
		uint8_t decision_class;

		uint64_t primary_key() const { return claim_id; }
		EOSLIB_SERIALIZE(claim, (claim_id)(claim_summary)(decision_link)(response_link)(decision_class))
	};

	/**
   * Case Files for all arbitration cases.
   * @scope get_self().value
   * @key case_id
   */
	struct [[eosio::table]] casefile
	{
		//NOTE: alternative table design. No secondary indices. Scope by claimant. Index by respondant.
		//pros: easy to track by claimant, no longer need secondary indexes.
		//cons: limited discoverability, must avoid secondary indexes.
		uint64_t case_id;
		uint8_t case_status;

		name claimant;
		name respondant;
		vector<name> arbitrators;
		vector<name> approvals;

		vector<uint8_t> required_langs;

		vector<claim> unread_claims;
		vector<uint64_t> accepted_claims;
		string case_ruling;
		uint32_t last_edit; //TODO: do we need to keep this? If so, then we need to update it everytime an action modifies

		uint64_t primary_key() const { return case_id; }

		uint64_t by_claimant() const { return claimant.value; }
		uint128_t by_uuid() const
		{
			uint128_t claimant_id = static_cast<uint128_t>(claimant.value);
			uint128_t respondant_id = static_cast<uint128_t>(respondant.value);
			return (claimant_id << 64) | respondant_id;
		}
		
		EOSLIB_SERIALIZE(casefile, (case_id)(case_status)(claimant)(respondant)(arbitrators)(approvals)
		(required_langs)(unread_claims)(accepted_claims)(case_ruling)(last_edit))
	};

	/**
   * Singleton for global config settings.
   * @scope singleton scope (get_self().value)
   * @key table name
   */
	//TODO: make fee structure a constant?
	//NOTE: initial deposit saved
	struct [[eosio::table]] config
	{
		name publisher;
		uint16_t max_elected_arbs;
		uint32_t election_duration;
		uint32_t election_start;

		/**
		 * Vector of fees by claim class, with an offset of 1
		 * fee_structure[0] 					= initial fee
		 * fee_structure[UNDECIDED] 			= UNDECIDED fee
		 * fee_structure[LOST_KEY_RECOVERY]  	= LOST_KEY_RECOVERY fee
		 *  ...
		*/
		vector<int64_t> fee_structure; //NOTE: always in TLOS so only store asset.amount value
		// TODO: just make vector of assets
		uint32_t arb_term_length;
		uint32_t last_time_edited;
		uint64_t current_ballot_id = 0;
		bool auto_start_election = false;

		uint64_t primary_key() const { return publisher.value; }
		EOSLIB_SERIALIZE(config, (publisher)(max_elected_arbs)(election_duration)(election_start)
		    (fee_structure)(arb_term_length)(last_time_edited)(current_ballot_id)(auto_start_election))
	};

	/**
   * Holds instances of joinder cases.
   * @scope get_self().value
   * @key uint64_t join_id
   */
	struct [[eosio::table]] joinder
	{
		uint64_t join_id;
		vector<uint64_t> cases;
		uint32_t join_time;
		name joined_by;

		uint64_t primary_key() const { return join_id; }
		EOSLIB_SERIALIZE(joinder, (join_id)(cases)(join_time)(joined_by))
	};

	struct [[eosio::table("accounts")]] account
	{
		asset balance;

		uint64_t primary_key() const { return balance.symbol.code().raw(); }
		EOSLIB_SERIALIZE(account, (balance))
	};

	typedef multi_index<"nominees"_n, nominee> nominees_table;

	typedef multi_index<"arbitrators"_n, arbitrator> arbitrators_table;

	typedef multi_index<"casefiles"_n, casefile> casefiles_table;

	typedef multi_index<"joinedcases"_n, joinder> joinders_table;

	typedef multi_index<"claims"_n, claim> claims_table;

	typedef multi_index<"accounts"_n, account> accounts_table;

	typedef singleton<name("config"), config> config_singleton;
	config_singleton configs;
	config _config;

	using exec_claim = action_wrapper<"execaccept"_n, &arbitration::execclaim>;
	using exec_file = action_wrapper<"execfile"_n, &arbitration::execfile>;

#pragma endregion Tables and Structs

#pragma region Helpers

	config get_default_config();

	void validate_ipfs_url(string ipfs_url);

	void assert_string(string to_check, string error_msg);

	void start_new_election(uint8_t available_seats);

	bool has_available_seats(arbitrators_table & arbitrators, uint8_t & available_seats);

	bool is_arb(name account);

	void add_arbitrator(arbitrators_table & arbitrators, name arb_name, std::string credential_link);

	void del_claim(uint64_t claim_id);

	vector<permission_level_weight> get_arb_permissions();

	void set_permissions(vector<permission_level_weight> &perms);

	vector<claim>::iterator get_claim_at(string claim_hash, vector<claim>& claims);

	void del_claim_at(const string claim_hash, vector<claim> claims);

	void transfer_handler(name from, name to, asset quantity);

	void sub_balance(name owner, asset value)
	{
		accounts_table from_acnts(_self, owner.value);

		const auto &from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
		check(from.balance.amount >= value.amount, "overdrawn balance");

		if (from.balance - value == asset(0, value.symbol))
		{
			from_acnts.erase(from);
		}
		else
		{
			from_acnts.modify(from, owner, [&](auto &a) {
				a.balance -= value;
			});
		}
	}

	void add_balance(name owner, asset value, name ram_payer)
	{
		accounts_table to_acnts(_self, owner.value);
		auto to = to_acnts.find(value.symbol.code().raw());
		if (to == to_acnts.end())
		{
			to_acnts.emplace(ram_payer, [&](auto &a) {
				a.balance = value;
			});
		}
		else
		{
			to_acnts.modify(to, same_payer, [&](auto &a) {
				a.balance += value;
			});
		}
	}

	template <typename T, typename func>
	void map(vector<T> & arr, func && handler)
	{
		for (auto it = arr.begin(); it != arr.end(); ++it)
		{
			handler(it);
		}
	}

#pragma endregion Helpers
};
