/**
 * Arbitration Contract Implementation. See function bodies for further notes.
 * 
 * @author Craig Branscom, Peter Bue, Ed Silva
 * @copyright defined in telos/LICENSE.txt
 */

#include <eosio.arbitration/eosio.arbitration.hpp>

arbitration::arbitration(name s, name code, datastream<const char *> ds) : eosio::contract(s, code, ds),
																		   configs(_self, _self.value)
{
	_config = configs.exists() ? configs.get() : get_default_config();
}

arbitration::~arbitration()
{
	configs.set(_config, get_self());
}

void arbitration::setconfig(uint16_t max_elected_arbs, uint32_t election_duration,
							uint32_t election_start, uint32_t arbitrator_term_length, vector<int64_t> fees)
{
	require_auth("eosio"_n);
	check(max_elected_arbs > uint16_t(0), "Arbitrators must be greater than 0");

	_config = config {
		get_self(),		   		//publisher
		max_elected_arbs,  		//max_elected_arbs
		election_duration, 		//election_duration
		election_start,			//election_start
		fees,			   		//fee_structure
		arbitrator_term_length,
		current_time_point().sec_since_epoch(),
		_config.current_ballot_id,
		_config.auto_start_election
	};
}

#pragma region Arb_Elections

void arbitration::initelection()
{
	require_auth("eosio"_n);
	check(!_config.auto_start_election, "Election is on auto start mode.");

	ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
	_config.current_ballot_id = ballots.available_primary_key();
	_config.auto_start_election = true;

	arbitrators_table arbitrators(get_self(), get_self().value);

	uint8_t available_seats = 0;
	if (has_available_seats(arbitrators, available_seats))
	{
		start_new_election(available_seats);
	}
}

void arbitration::regarb(name nominee, string credentials_link)
{
	require_auth(nominee);
	validate_ipfs_url(credentials_link);

	nominees_table nominees(get_self(), get_self().value);
	auto nom_itr = nominees.find(nominee.value);
	check(nom_itr == nominees.end(), "Nominee is already an applicant");

	arbitrators_table arbitrators(get_self(), get_self().value);
	auto arb_itr = arbitrators.find(nominee.value);

	if (arb_itr != arbitrators.end())
	{
		check(current_time_point().sec_since_epoch() > arb_itr->term_expiration, "Nominee is already an Arbitrator and the seat hasn't expired");

		//NOTE: set arb_status to SEAT_EXPIRED until re-election
		arbitrators.modify(arb_itr, same_payer, [&](auto &row) {
			row.arb_status = SEAT_EXPIRED;
		});
	}

	nominees.emplace(get_self(), [&](auto &row) {
		row.nominee_name = nominee;
		row.credentials_link = credentials_link;
		row.application_time = current_time_point().sec_since_epoch();
	});
}

void arbitration::unregnominee(name nominee)
{
	require_auth(nominee);

	nominees_table nominees(get_self(), get_self().value);
	auto nom_itr = nominees.find(nominee.value);
	check(nom_itr != nominees.end(), "Nominee isn't an applicant");

	ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
	auto bal = ballots.get(_config.current_ballot_id, "Ballot doesn't exist");

	leaderboards_table leaderboards("eosio.trail"_n, "eosio.trail"_n.value);
	auto board = leaderboards.get(bal.reference_id, "Leaderboard doesn't exist");

	if (_config.auto_start_election)
		check(current_time_point().sec_since_epoch() < board.begin_time, "Cannot unregister while election is in progress");

	nominees.erase(nom_itr);
}

void arbitration::candaddlead(name nominee, string credentials_link)
{
	require_auth(nominee);
	validate_ipfs_url(credentials_link);
	check(_config.auto_start_election, "there is no active election");

	nominees_table nominees(get_self(), get_self().value);
	auto nom_itr = nominees.find(nominee.value);
	check(nom_itr != nominees.end(), "Nominee isn't an applicant. Use regarb action to register as a nominee");

	ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
	auto bal = ballots.get(_config.current_ballot_id, "Ballot doesn't exist");

	leaderboards_table leaderboards("eosio.trail"_n, "eosio.trail"_n.value);
	auto board = leaderboards.get(bal.reference_id, "Leaderboard doesn't exist");
	check(board.status != CLOSED, "A new election hasn't started. Use initelection action to start a new election.");

	action(permission_level{get_self(), "active"_n}, "eosio.trail"_n, "addcandidate"_n,
		   make_tuple(get_self(),
					  _config.current_ballot_id,
					  nominee,
					  credentials_link))
		.send();

	//print("\nArb Application: SUCCESS");
}

void arbitration::candrmvlead(name nominee)
{
	require_auth(nominee);

	nominees_table nominees(get_self(), get_self().value);
	auto nom_itr = nominees.find(nominee.value);
	check(nom_itr != nominees.end(), "Nominee isn't an applicant.");

	action(permission_level{get_self(), "active"_n}, "eosio.trail"_n, "rmvcandidate"_n,
		   make_tuple(get_self(),
					  _config.current_ballot_id,
					  nominee))
		.send();

	//print("\nCancel Application: SUCCESS");
}

void arbitration::endelection(name nominee) //NOTE: required eosio.arb@eosio.code
{
	require_auth(nominee);

	ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
	auto bal = ballots.get(_config.current_ballot_id, "Ballot doesn't exist");

	leaderboards_table leaderboards("eosio.trail"_n, "eosio.trail"_n.value);
	auto board = leaderboards.get(bal.reference_id, "Leaderboard doesn't exist");
	check(current_time_point().sec_since_epoch() > board.end_time,
		  std::string("Election hasn't ended. Please check again after the election is over in " + std::to_string(uint32_t(board.end_time - current_time_point().sec_since_epoch()))
					  + " seconds")
			  .c_str());

	//sort board candidates by votes
	auto board_candidates = board.candidates;
	sort(board_candidates.begin(), board_candidates.end(), [](const auto &c1, const auto &c2) { return c1.votes > c2.votes; });

	//resolve tie clonficts
	if (board_candidates.size() > board.available_seats)
	{
		auto first_cand_out = board_candidates[board.available_seats];
		board_candidates.resize(board.available_seats);

		//count candidates that are tied with first_cand_out
		uint8_t tied_cands = 0;
		for (int i = board_candidates.size() - 1; i >= 0; i--)
		{
			if (board_candidates[i].votes == first_cand_out.votes)
				tied_cands++;
		}

		//remove all tied candidates
		if (tied_cands > 0)
			board_candidates.resize(board_candidates.size() - tied_cands);
	}

	nominees_table nominees(get_self(), get_self().value);
	auto nom_itr = nominees.find(nominee.value);
	check(nom_itr != nominees.end(), "Nominee isn't an applicant.");

	arbitrators_table arbitrators(get_self(), get_self().value);
	std::vector<permission_level_weight> arbs_perms;

	//in case there are still candidates (not all tied)
	if (board_candidates.size() > 0)
	{
		for (int i = 0; i < board_candidates.size(); i++)
		{
			name cand_name = board_candidates[i].member;
			auto c = nominees.find(cand_name.value);

			if (c != nominees.end())
			{
				auto cand_credential = board_candidates[i].info_link;
				auto cand_votes = board_candidates[i].votes;
				auto threshold_votes = asset(MIN_VOTE_THRESHOLD, cand_votes.symbol);
				print("\ncand_votes: ", cand_votes);
				print("\nthreshold_votes: ", threshold_votes);
				if (cand_votes < threshold_votes) {
					print("\nskipping candidate: ", cand_name, " because they have no votes");
					continue;
				}
					

				//remove candidates from candidates table / arbitration contract
				nominees.erase(c);

				//add candidates to arbitration table / arbitration contract
				add_arbitrator(arbitrators, cand_name, cand_credential);
			}
			else
			{
				print("\ncandidate: ", cand_name, " was not found.");
			}
		}

		//add current arbitrators to permission list
		for (const auto &a : arbitrators)
		{
			if (a.arb_status != SEAT_EXPIRED || a.arb_status != REMOVED)
			{
				arbs_perms.emplace_back(permission_level_weight{permission_level{a.arb, "active"_n}, 1});
			}
		}

		set_permissions(arbs_perms);
	}

	//close ballot action.
	action(permission_level{get_self(), "active"_n}, "eosio.trail"_n, "closeballot"_n,
		   make_tuple(
			   get_self(),
			   _config.current_ballot_id,
			   CLOSED))
		.send();

	//start new election with remaining candidates
	//and new candidates that registered after past election had started.
	uint8_t available_seats = 0;
	auto remaining_candidates = distance(nominees.begin(), nominees.end());
	print("\nremaining_cands: ", remaining_candidates);
	if (remaining_candidates > 0 && has_available_seats(arbitrators, available_seats))
	{
		_config.current_ballot_id = ballots.available_primary_key();

		start_new_election(available_seats);

		for (const auto &c : nominees)
		{
			action(permission_level{get_self(), "active"_n}, "eosio.trail"_n, "addcandidate"_n,
				   make_tuple(
					   get_self(),
					   _config.current_ballot_id,
					   c,
					   c.credentials_link))
				.send();
		}

		//print("\nA new election has started.");
	}
	else
	{
		for (auto i = nominees.begin(); i != nominees.end(); i = nominees.erase(i))
			;
		_config.auto_start_election = false;
		//print("\nThere aren't enough seats available or candidates to start a new election.\nUse init action to start a new election.");
	}
}

#pragma endregion Arb_Elections

#pragma region Case_Setup
void arbitration::withdraw(name owner) //NOTE: requires eosio.arb@eosio.code
{
	require_auth(owner);

	accounts_table accounts(get_self(), owner.value);
	const auto &bal = accounts.get(native_sym.code().raw(), "balance does not exist");

	action(permission_level{get_self(), "active"_n}, "eosio.token"_n, "transfer"_n,
		   make_tuple(get_self(),
					  owner,
					  bal.balance,
					  std::string("eosio.arb withdrawal")))
		.send();

	accounts.erase(bal);
}

//QUESTION: Does cleos/teclos still not support optional serialization?
void arbitration::filecase(name claimant, string claim_link, vector<uint8_t> lang_codes, std::optional<name> respondant)
{
	require_auth(claimant);
	validate_ipfs_url(claim_link);

	casefiles_table casefiles(get_self(), get_self().value);
	print("respondant: ", *respondant);
	if(respondant) {
		check(is_account(*respondant), "respondant must be an account");
	}

	vector<claim> unr_claims = {claim{uint64_t(0), claim_link, ""}};
	uint64_t new_case_id = casefiles.available_primary_key();

	casefiles.emplace(claimant, [&](auto &row) {
		row.case_id = new_case_id;
		row.case_status = CASE_SETUP;
		row.claimant = claimant;
		row.respondant = *respondant;
		row.arbitrators = {};
		row.approvals = {};
		row.required_langs = lang_codes;
		row.unread_claims = unr_claims;
		row.accepted_claims = {};
		row.case_ruling = std::string("");
		row.last_edit = current_time_point().sec_since_epoch();
	});

	exec_file exec("eosio.arb"_n, {get_self(), "active"_n});
	exec.send(new_case_id, claimant, claim_link, lang_codes, *respondant);
}

void arbitration::execfile(uint64_t new_case_id, name claimant, string claim_link,
			  vector<uint8_t> lang_codes, name respondant) {
	require_auth(get_self());
}

void arbitration::addclaim(uint64_t case_id, string claim_link, name claimant)
{
	require_auth(claimant);
	validate_ipfs_url(claim_link);

	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "Case Not Found");

	check(cf.case_status == CASE_SETUP, "claims cannot be added after CASE_SETUP is complete.");

	auto new_claims = cf.unread_claims;

	check(new_claims.size() < MAX_UNREAD_CLAIMS, "case file has reached maximum number of claims");
	check(claimant == cf.claimant, "you are not the claimant of this case.");
	auto claim_it = get_claim_at(claim_link, new_claims);
	check(claim_it == new_claims.end(), "ipfs hash exists in another claim");

	claim new_claim = claim{ uint64_t(0), claim_link, "" };
	
	new_claims.emplace_back(new_claim);
	casefiles.modify(cf, same_payer, [&](auto &row) {
		row.unread_claims = new_claims;
	});
}

void arbitration::removeclaim(uint64_t case_id, string claim_hash, name claimant)
{
	require_auth(claimant);

	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "Case Not Found");
	check(cf.case_status == CASE_SETUP, "Claims cannot be removed after CASE_SETUP is complete");
	check(cf.unread_claims.size() > 0, "No claims to remove");
	check(claimant == cf.claimant, "you are not the claimant of this case.");

	auto new_claims = cf.unread_claims;

	auto claim_it = get_claim_at(claim_hash, new_claims);
	check(claim_it != new_claims.end(), "Claim Hash not found in casefile");
	new_claims.erase(claim_it);

	casefiles.modify(cf, same_payer, [&](auto &row) {
		row.unread_claims = new_claims;
	});

	//print("\nClaim Removed");
}

void arbitration::shredcase(uint64_t case_id, name claimant)
{
	require_auth(claimant);

	casefiles_table casefiles(get_self(), get_self().value);
	auto c_itr = casefiles.find(case_id);
	check(c_itr != casefiles.end(), "Case Not Found");
	check(claimant == c_itr->claimant, "you are not the claimant of this case.");
	check(c_itr->case_status == CASE_SETUP, "cases can only be shredded during CASE_SETUP");

	casefiles.erase(c_itr);
}

void arbitration::readycase(uint64_t case_id, name claimant)
{
	require_auth(claimant);
	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "Case Not Found");
	check(cf.case_status == CASE_SETUP, "Cases can only be readied during CASE_SETUP");
	check(cf.unread_claims.size() >= 1, "Cases must have atleast one claim");
	check(claimant == cf.claimant, "you are not the claimant of this case.");

	sub_balance(claimant, asset(_config.fee_structure[0], native_sym));

	casefiles.modify(cf, get_self(), [&](auto &row) {
		row.case_status = AWAITING_ARBS;
		row.last_edit = current_time_point().sec_since_epoch();
	});
}

#pragma endregion Case_Setup

#pragma region Case_Progression

void arbitration::respond(uint64_t case_id, string claim_hash, name respondant, string response_link)
{
	require_auth(respondant);
	validate_ipfs_url(response_link);

	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "Case Not Found");

	check(cf.respondant != name(0), "case_id does not have a respondant");
	check(cf.respondant == respondant, "must be the respondant of this case_id");
	check(cf.case_status == CASE_INVESTIGATION, "case status does NOT allow responses at this time");

	auto delta_claims = cf.unread_claims;
    
	auto claim_it = get_claim_at(claim_hash, delta_claims);
	check(claim_it != delta_claims.end(), "claim does not exist in unread claims");

	claim_it->response_link = response_link;

	casefiles.modify(cf, same_payer, [&](auto& c) {
		c.unread_claims = delta_claims;
	});
}

void arbitration::addarbs(uint64_t case_id, name assigned_arb, uint8_t num_arbs_to_assign)
{
	require_auth(assigned_arb);
	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "Case Not Found");

	auto arb_it = std::find(cf.arbitrators.begin(), cf.arbitrators.end(), assigned_arb);
	check(arb_it != cf.arbitrators.end(), "arbitrator isn't assigned to this case_id");

	/* RATIONALE: If the following validations pass, the trx would be committed irreversible.
	Demux service watch from calls to this action, a side effect of this action being call 
	is to assigntocase N times. */
}

void arbitration::assigntocase(uint64_t case_id, name arb_to_assign)
{
	require_auth(permission_level("eosio.arb"_n, "assign"_n));

	arbitrators_table arbitrators(get_self(), get_self().value);
	const auto& arb = arbitrators.get(arb_to_assign.value, "actor is not a registered Arbitrator");
	check(arb.arb_status != REMOVED, "Arbitrator has been removed.");
	check(arb.arb_status == AVAILABLE, "Arb status isn't set to available, Arbitrator is unable to receive new cases");

	vector<uint64_t> new_open_cases = arb.open_case_ids;
	new_open_cases.emplace_back(case_id);

	arbitrators.modify(arb, same_payer, [&](auto &row) {
		row.open_case_ids = new_open_cases;
	});

	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "Case not found with given Case ID");
	check(cf.case_status >= AWAITING_ARBS, "case file is still in CASE_SETUP");
	check(cf.case_status < RESOLVED, "case file can not be RESOLVED or DISMISSED");

	//check(cf.arbitrators.size() == size_t(0), "Case already has an assigned arbitrator");
	check(std::find(cf.arbitrators.begin(), cf.arbitrators.end(), arb_to_assign) == cf.arbitrators.end(),
		  "Arbitrator is already assigned to this case");

	vector<name> new_arbs = cf.arbitrators;
	new_arbs.emplace_back(arb.arb);

	casefiles.modify(cf, same_payer, [&](auto &row) {
		row.arbitrators = new_arbs;

		if(cf.case_status == AWAITING_ARBS) {
			row.case_status = CASE_INVESTIGATION;
		}
	});
}

void arbitration::dismissclaim(uint64_t case_id, name assigned_arb, string claim_hash, string memo)
{
	require_auth(assigned_arb);
	validate_ipfs_url(claim_hash);

	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "Case not found");

	check(cf.case_status < DECISION && cf.case_status > AWAITING_ARBS, "unable to dismiss claim while this case file is in this status");

	auto arb_case = std::find(cf.arbitrators.begin(), cf.arbitrators.end(), assigned_arb);
	check(arb_case != cf.arbitrators.end(), "Only an assigned arbitrator can dismiss a claim");

	assert_string(memo, std::string("memo must be greater than 0 and less than 255"));
	vector<claim> new_claims = cf.unread_claims;

	auto claim_it = get_claim_at(claim_hash, new_claims);
	check(claim_it != new_claims.end(), "Claim Hash not found in casefile");
	new_claims.erase(claim_it);

	casefiles.modify(cf, same_payer, [&](auto &cf) {
		cf.unread_claims = new_claims;
		cf.last_edit = current_time_point().sec_since_epoch();
	});
}

void arbitration::acceptclaim(uint64_t case_id, name assigned_arb, string claim_hash,
							  string decision_link, uint8_t decision_class)
{
	require_auth(assigned_arb);
	validate_ipfs_url(claim_hash);
	validate_ipfs_url(decision_link);
	
	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "Case not found");

	auto arb_case = std::find(cf.arbitrators.begin(), cf.arbitrators.end(), assigned_arb);
	check(arb_case != cf.arbitrators.end(), "Only the assigned arbitrator can accept a claim");

	check(decision_class > UNDECIDED && decision_class <= MISC, "decision_class must be valid [2 - 15]");
	check(cf.case_status < DECISION && cf.case_status > AWAITING_ARBS, "unable to dismiss claim while this case file is in this status");
	
	claims_table claims(get_self(), get_self().value);

	vector<claim> new_unread_claims = cf.unread_claims;

	auto claim_it = get_claim_at(claim_hash, new_unread_claims);
	check(claim_it != new_unread_claims.end(), "Claim Hash not found in casefile");
	auto response_link = claim_it->response_link;
	new_unread_claims.erase(claim_it);
	uint64_t new_claim_id = claims.available_primary_key();
	vector<uint64_t> new_accepted_claims = cf.accepted_claims;
	new_accepted_claims.emplace_back(new_claim_id);

	casefiles.modify(cf, same_payer, [&](auto &row) {
		row.unread_claims = new_unread_claims;
		row.accepted_claims = new_accepted_claims;
		row.last_edit = current_time_point().sec_since_epoch();
	});

	claims.emplace(get_self(), [&](auto &row) {
		row.claim_id = new_claim_id;
		row.claim_summary = claim_hash;
		row.decision_link = decision_link;
		row.decision_class = decision_class;
		row.response_link = response_link;
	});
	
	exec_claim exec(get_self(), { get_self(), "active"_n });
	exec.send(new_claim_id, case_id, assigned_arb, claim_hash, decision_link, decision_class);
}

void arbitration::execclaim(uint64_t new_claim_id, uint64_t case_id, name assigned_arb, string claim_hash,
				string decision_link, uint8_t decision_class) {
	require_auth(get_self());
}

void arbitration::setruling(uint64_t case_id, name assigned_arb, string case_ruling) {
	require_auth(assigned_arb);
	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "Case not found with given Case ID");
	check(cf.case_status == ENFORCEMENT, "case_status must be ENFORCEMENT");

	auto arb_it = std::find(cf.arbitrators.begin(), cf.arbitrators.end(), assigned_arb);
	check(arb_it != cf.arbitrators.end(), "arbitrator is not assigned to this case_id");
	validate_ipfs_url(case_ruling);

	casefiles.modify(cf, same_payer, [&](auto& row) {
		row.case_ruling = case_ruling;
	});
}

void arbitration::advancecase(uint64_t case_id, name assigned_arb)
{
	require_auth(assigned_arb);

	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "Case not found with given Case ID");
	check(cf.case_status > AWAITING_ARBS, "case_status must be greater than AWAITING_ARBS");
	check(cf.case_status < RESOLVED && cf.case_status != DISMISSED, "Case has already been resolved or dismissed");
	if (cf.case_status + 1 == RESOLVED) {
		check(cf.case_ruling != string(""), "Case Ruling must be set before advancing case to RESOLVED status");
	}

	auto arb_it = std::find(cf.arbitrators.begin(), cf.arbitrators.end(), assigned_arb);
	check(arb_it != cf.arbitrators.end(), "actor is not assigned to this case_id");

	auto approval_it = std::find(cf.approvals.begin(), cf.approvals.end(), assigned_arb);
	check(approval_it == cf.approvals.end(), "arbitrator has already approved advancing this case");

	auto case_status = cf.case_status;
	auto approvals = cf.approvals;

	if (approvals.size() + 1 < cf.arbitrators.size()) {
		approvals.emplace_back(assigned_arb);
	} else if (cf.approvals.size() + 1 >= cf.arbitrators.size()) {
		case_status++;
		approvals.clear();
	}

	casefiles.modify(cf, same_payer, [&](auto &row) {
		row.case_status = case_status;
		row.approvals = approvals;
	});
}

void arbitration::dismisscase(uint64_t case_id, name assigned_arb, string ruling_link)
{
	require_auth(assigned_arb);
	validate_ipfs_url(ruling_link);

	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "No case found with given case_id");

	auto arb_case = std::find(cf.arbitrators.begin(), cf.arbitrators.end(), assigned_arb);
	check(arb_case != cf.arbitrators.end(), "Arbitrator isn't selected for this case");
	check(cf.case_status == CASE_INVESTIGATION, "Case is already dismissed or complete");

	casefiles.modify(cf, same_payer, [&](auto &row) {
		row.case_status = DISMISSED;
		row.case_ruling = ruling_link;
		row.last_edit = current_time_point().sec_since_epoch();
	});
}

void arbitration::recuse(uint64_t case_id, string rationale, name assigned_arb)
{
	require_auth(assigned_arb);

	casefiles_table casefiles(get_self(), get_self().value);
	const auto& cf = casefiles.get(case_id, "No case found for given case_id");

	check(cf.case_status > AWAITING_ARBS && cf.case_status < RESOLVED, 
		"unable to recuse if the case is resolved");	

	auto arb_case = std::find(cf.arbitrators.begin(), cf.arbitrators.end(), assigned_arb);
	check(arb_case != cf.arbitrators.end(), "Arbitrator isn't selected for this case.");

	assert_string(rationale, std::string("rationale must be greater than 0 and less than 255"));

	vector<name> new_arbs = cf.arbitrators;
	auto arb_it = find(new_arbs.begin(), new_arbs.end(), assigned_arb);
	new_arbs.erase(arb_it);

	casefiles.modify(cf, same_payer, [&](auto &row) {
		row.arbitrators = new_arbs;
		row.last_edit = current_time_point().sec_since_epoch();
	});
}

#pragma endregion Case_Progression

#pragma region Arb_Actions

void arbitration::newarbstatus(uint8_t new_status, name arbitrator)
{
	require_auth(arbitrator);

	arbitrators_table arbitrators(_self, _self.value);
	const auto& arb = arbitrators.get(arbitrator.value, "Arbitrator not found");

	check(new_status >= 0 && new_status <= 2, "Supplied status code is invalid for this action");

	arbitrators.modify(arb, same_payer, [&](auto &row) {
		row.arb_status = new_status;
	});
}

void arbitration::setlangcodes(name arbitrator, vector<uint8_t> lang_codes)
{
	require_auth(arbitrator);
	arbitrators_table arbitrators(get_self(), get_self().value);
	const auto& arb = arbitrators.get(arbitrator.value, "arbitrator not found");

	check(current_time_point().sec_since_epoch() < arb.term_expiration, "arbitrator term expired");

	arbitrators.modify(arb, same_payer, [&](auto& a) {
		a.languages = lang_codes;
	});
}

void arbitration::deletecase(uint64_t case_id)
{
	require_auth( permission_level{ "eosio.arb"_n, "major"_n } );

	casefiles_table casefiles(get_self(), get_self().value);

	const auto& cf = casefiles.get(case_id, "case file not found");
	//check(cf.case_status >= RESOLVED, "case must either be RESOLVED or DISMISSED");
	casefiles.erase(cf);

	auto claim_ids = cf.accepted_claims;
	
	for(auto& id : claim_ids) {
		del_claim(id);
	}
}

#pragma endregion Arb_Actions

#pragma region BP_Multisig_Actions

	void arbitration::dismissarb(name arb, bool remove_from_cases)
	{
		require_auth("eosio"_n);
		check(is_account(arb), "arb must be account");

		arbitrators_table arbitrators(get_self(), get_self().value);

		const auto& to_dismiss = arbitrators.get(arb.value, "arbitrator not found");

		check(to_dismiss.arb_status != SEAT_EXPIRED && to_dismiss.arb_status != REMOVED, 
			"arbitrator is already removed or their seat has expired");

		arbitrators.modify(to_dismiss, same_payer, [&](auto& a) {
			a.arb_status = REMOVED;
		});

		auto perms = get_arb_permissions();
		set_permissions(perms);

		if(remove_from_cases) {
			casefiles_table casefiles(get_self(), get_self().value);

			for(const auto &id: to_dismiss.open_case_ids) {
				auto cf_it = casefiles.find(id);

				if (cf_it != casefiles.end()) {
					auto case_arbs = cf_it->arbitrators;
					auto arb_it = find(case_arbs.begin(), case_arbs.end(), to_dismiss.arb);

					if (arb_it != case_arbs.end() && cf_it->case_status < RESOLVED) {
						case_arbs.erase(arb_it);
						casefiles.modify(cf_it, same_payer, [&](auto &row) {
							row.arbitrators = case_arbs;
						});
					}
				}
			}

			auto open_ids = to_dismiss.open_case_ids;
			open_ids.clear();

			arbitrators.modify(to_dismiss, same_payer, [&](auto& a) {
				a.arb_status = REMOVED;
				a.open_case_ids = open_ids;
			});
		}
	}
#pragma endregion BP_Multisig_Actions

#pragma region Helpers

typedef arbitration::claim claim;

bool arbitration::is_arb(name account)
{
	arbitrators_table arbitrators(get_self(), get_self().value);
	return arbitrators.find(account.value) != arbitrators.end();
}

vector<claim>::iterator arbitration::get_claim_at(string hash, vector<claim>& claims)
{
	return std::find_if(claims.begin(), claims.end(), [&](auto &claim) {
		return claim.claim_summary == hash;
	});
}

void arbitration::validate_ipfs_url(string ipfs_url)
{
	check(ipfs_url.length() == 46 || ipfs_url.length() == 49, "invalid ipfs string, valid schema: <hash>");
}

void arbitration::assert_string(string to_check, string error_msg)
{
	check(to_check.length() > 0 && to_check.length() < 255, error_msg.c_str());
}

arbitration::config arbitration::get_default_config()
{
	vector<int64_t> fees{2000000};
	auto c = config{
		get_self(),			// publisher
		uint16_t(21),		// max_elected_arbs
		uint32_t(2505600),  // election_duration
		uint32_t(604800),   // election_start
		fees,				// fee_structure
		uint32_t(31536000), // arb_term_length
		current_time_point().sec_since_epoch(),
		uint64_t(0), 		// current_ballot_id
		bool(0),	 		// auto_start_election
	};

	return c;
}

void arbitration::start_new_election(uint8_t available_seats)
{
	uint32_t begin_time = current_time_point().sec_since_epoch() + _config.election_start;
	uint32_t end_time = begin_time + _config.election_duration;

	action(permission_level{get_self(), "active"_n}, "eosio.trail"_n, "regballot"_n,
		   make_tuple(get_self(),		 	// publisher
					  uint8_t(2),		 	// ballot_type (2 == leaderboard)
					  symbol("VOTE", 4), 	// voting_symbol
					  begin_time,		 	// begin_time
					  end_time,			 	// end_time
					  std::string("")		// info_url
					  ))
		.send();

	action(permission_level{get_self(), "active"_n}, "eosio.trail"_n, "setseats"_n,
		   make_tuple(get_self(),
					  _config.current_ballot_id,
					  available_seats))
		.send();

	print("\nNew election has started.");
}

bool arbitration::has_available_seats(arbitrators_table &arbitrators, uint8_t &available_seats)
{
	uint8_t occupied_seats = 0;

	for (auto &arb : arbitrators)
	{
		// check if arb seat is expired
		if (current_time_point().sec_since_epoch() > arb.term_expiration && arb.arb_status != uint16_t(SEAT_EXPIRED))
		{
			arbitrators.modify(arb, same_payer, [&](auto &a) {
				a.arb_status = uint16_t(SEAT_EXPIRED);
			});
		}

		if (arb.arb_status != uint16_t(SEAT_EXPIRED))
			occupied_seats++;
	}
	available_seats = uint8_t(_config.max_elected_arbs - occupied_seats);

	return available_seats > 0;
}

vector<arbitration::permission_level_weight> arbitration::get_arb_permissions() {
	arbitrators_table arbitrators(get_self(), get_self().value);
	vector<permission_level_weight> perms;
	for(const auto &a: arbitrators) {
		if (a.arb_status != SEAT_EXPIRED || a.arb_status != REMOVED)
		{
			perms.emplace_back(permission_level_weight{permission_level{a.arb, "active"_n}, 1});
		}
	}
	return perms;
}

void arbitration::set_permissions(vector<permission_level_weight> &perms) {
	//review update auth permissions and weights.
	if (perms.size() > 0)
	{
		sort(perms.begin(), perms.end(), [](const auto &first, const auto &second) 
			{ return first.permission.actor.value < second.permission.actor.value; });

		uint32_t weight = perms.size() > 3 ? (((2 * perms.size()) / uint32_t(3)) + 1) : 1;

		action(permission_level{get_self(), "owner"_n}, "eosio"_n, "updateauth"_n,
				std::make_tuple(
					get_self(),
					"major"_n,
					"owner"_n,
					authority{
						weight,
						std::vector<key_weight>{},
						perms,
						std::vector<wait_weight>{}}))
			.send();
	}
}

void arbitration::add_arbitrator(arbitrators_table &arbitrators, name arb_name, string credential_link)
{
	auto arb = arbitrators.find(arb_name.value);
	if (arb == arbitrators.end())
	{
		arbitrators.emplace(_self, [&](auto &a) {
			a.arb = arb_name;
			a.arb_status = uint16_t(UNAVAILABLE);
			a.elected_time = current_time_point().sec_since_epoch();
			a.term_expiration = current_time_point().sec_since_epoch() + _config.arb_term_length;
			a.open_case_ids = vector<uint64_t>();
			a.closed_case_ids = vector<uint64_t>();
			a.credentials_link = credential_link;
		});
	}
	else
	{
		arbitrators.modify(arb, same_payer, [&](auto &a) {
			a.arb_status = uint16_t(UNAVAILABLE);
			a.elected_time = current_time_point().sec_since_epoch();
			a.term_expiration = current_time_point().sec_since_epoch() + _config.arb_term_length;
			a.credentials_link = credential_link;
		});
	}
}

void arbitration::del_claim(uint64_t claim_id) {
	claims_table claims(get_self(), get_self().value);
	const auto& claim = claims.get(claim_id, "claim not found");
	claims.erase(claim);
}

void arbitration::transfer_handler(name from, name to, asset quantity, string memo)
{
	require_auth(from);

	check(quantity.is_valid(), "Invalid quantity");
	check(quantity.symbol == symbol("TLOS", 4), "only TLOS tokens are accepted by this contract");

	if (from == get_self())
		return;

	check(to == get_self(), "to must be self");

	accounts_table accounts(get_self(), from.value);
	const auto &from_bal = accounts.find(quantity.symbol.code().raw());

	add_balance(from, quantity, get_self());

	print("\nDeposit Complete");
}

#pragma endregion Helpers