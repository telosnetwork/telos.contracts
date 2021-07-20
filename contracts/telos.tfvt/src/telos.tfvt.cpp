#include <telos.tfvt.hpp>
#include <eosio/symbol.hpp>

tfvt::tfvt(name self, name code, datastream<const char*> ds)
: contract(self, code, ds), configs(get_self(), get_self().value) {
	print("\n exists?: ", configs.exists());
	_config = configs.exists() ? configs.get() : get_default_config();
}

tfvt::~tfvt() {
	if(configs.exists()) configs.set(_config, get_self());
}

tfvt::config tfvt::get_default_config() {
	auto c = config {
		get_self(),			//publisher
		uint8_t(12),		//max seats
		uint8_t(12),        //open seats
		name(),				//open_election_id
		uint32_t(5), 		//holder_quorum_divisor
		uint32_t(2), 		//board_quorum_divisor
		uint32_t(2000000),	//issue_duration
		uint32_t(1200),  	//start_delay
		uint32_t(2000000),  //leaderboard_duration
		uint32_t(14515200),	//election_frequency
		uint32_t(0),		//last_board_election_time
		uint32_t(0),		//active election min time to start
		false				//is_active_election
	};
	configs.set(c, get_self());
	return c;
}

#pragma region Actions

void tfvt::setconfig(name member, config new_config) { 
    require_auth("tf"_n);
	configs.remove();
    check(new_config.max_board_seats >= new_config.open_seats, "can't have more open seats than max seats");
	check(new_config.holder_quorum_divisor > 0, "holder_quorum_divisor must be a non-zero number");
	check(new_config.board_quorum_divisor > 0, "board_quorum_divisor must be a non-zero number");
	check(new_config.issue_duration > 0, "issue_duration must be a non-zero number");
	check(new_config.start_delay > 0, "start_delay must be a non-zero number");
	check(new_config.leaderboard_duration > 0, "leaderboard_duration must be a non-zero number");
	check(new_config.election_frequency > 0, "election_frequency must be a non-zero number");

	// NOTE : this will break an ongoing election check for makeelection 
	if(new_config.max_board_seats >= _config.max_board_seats){
		new_config.open_seats = new_config.max_board_seats - _config.max_board_seats + _config.open_seats;
	}else if(new_config.max_board_seats > _config.max_board_seats - _config.open_seats){
		new_config.open_seats = new_config.max_board_seats - (_config.max_board_seats - _config.open_seats);
	}else{
		new_config.open_seats = 0;
	}

	new_config.publisher = _config.publisher;
	new_config.open_election_id = _config.open_election_id;
	new_config.last_board_election_time = _config.last_board_election_time;
	new_config.is_active_election = _config.is_active_election;

	_config = new_config;
	configs.set(_config, get_self());
}

void tfvt::nominate(name nominee, name nominator) {
    require_auth(nominator);
	check(is_account(nominee), "nominee account must exist");
	check(!is_board_member(nominee) || is_term_expired(), "nominee is a board member, nominee's term must be expired");

    nominees_table noms(get_self(), get_self().value);
    auto n = noms.find(nominee.value);
    check(n == noms.end(), "nominee has already been nominated");

    noms.emplace(get_self(), [&](auto& m) {
        m.nominee = nominee;
    });
}

void tfvt::makeelection(name holder) {
	require_auth(holder);
	check(!_config.is_active_election, "there is already an election in progress");
	check(_config.open_seats > 0 || is_term_expired(), "it isn't time for the next election");
	
	ballots_table ballots(TELOS_DECIDE_N, TELOS_DECIDE_N.value);

	_config.open_election_id = get_next_ballot_id();

	_config.active_election_min_start_time = current_time_point().sec_since_epoch() + _config.start_delay;

    action(permission_level{get_self(), name("active")}, TELOS_DECIDE_N, name("newballot"), make_tuple(
		name(_config.open_election_id), // ballot name
		name("leaderboard"), // type
		get_self(), // publisher
		symbol("VOTE", 4), // treasury symbol
		name("1tokennvote"), // voting method
		vector<name>() // initial options
	)).send();

	// Todo: set details of the ballot
	if(is_term_expired()) {
		_config.open_seats = _config.max_board_seats;
	}

	//NOTE: this prevents makeelection from being called multiple times.
	//NOTE2 : this gets overwritten by setconfig
	_config.is_active_election = true;
}

void tfvt::addcand(name candidate) {
	require_auth(candidate);
	check(is_nominee(candidate), "only nominees can be added to the election");
	check(_config.is_active_election, "no active election for board members at this time");
	check(!is_board_member(candidate) || is_term_expired(), "nominee can't already be a board member, or their term must be expired.");

    action(permission_level{get_self(), name("active")}, TELOS_DECIDE_N, name("addoption"), make_tuple(
		_config.open_election_id, 	//ballot_id
		candidate 					//new_candidate
	)).send();
}

void tfvt::removecand(name candidate) {
	require_auth(candidate);
	check(is_nominee(candidate), "candidate is not a nominee");

    action(permission_level{get_self(), name("active")}, TELOS_DECIDE_N, name("rmvoption"), make_tuple(
		_config.open_election_id, 	//ballot_id
		candidate 					//new_candidate
	)).send();
}

void tfvt::startelect(name holder) {
	require_auth(holder);
	check(_config.is_active_election, "there is no active election to start");
	check(current_time_point().sec_since_epoch() > _config.active_election_min_start_time, "It isn't time to start the election");

	uint32_t election_end_time = current_time_point().sec_since_epoch() + _config.leaderboard_duration;

	action(permission_level{get_self(), name("active")}, TELOS_DECIDE_N, name("openvoting"), make_tuple(
		_config.open_election_id, 	//ballot_id
		election_end_time
	)).send();
}

void tfvt::endelect(name holder) {
    require_auth(holder);
	check(_config.is_active_election, "there is no active election to end");
	uint8_t status = 1;

    ballots_table ballots(TELOS_DECIDE_N, TELOS_DECIDE_N.value);
    auto bal = ballots.get(_config.open_election_id.value);
	map<name, asset> candidates = bal.options;
	vector<pair<int64_t, name>> sorted_candidates;

	for (auto it = candidates.begin(); it != candidates.end(); ++it) {
		sorted_candidates.push_back(make_pair(it->second.amount, it->first));
	}
	sort(sorted_candidates.begin(), sorted_candidates.end(), [](const auto &c1, const auto &c2) { return c1 > c2; });
	
	// Remove candidates tied with the [available-seats] - This discards all the tied on the tail
	if (sorted_candidates.size() > _config.open_seats) {
		auto first_cand_out = sorted_candidates[_config.open_seats];
		sorted_candidates.resize(_config.open_seats);
		
		// count candidates that are tied with first_cand_out
		uint8_t tied_cands = 0;
		for(int i = sorted_candidates.size() - 1; i >= 0; i--) {
			if(sorted_candidates[i].first == first_cand_out.first) {
				tied_cands++;
			}
		}

		// remove all tied candidates
		if(tied_cands > 0) {
			sorted_candidates.resize(sorted_candidates.size() - tied_cands);
		}
	}

	if(sorted_candidates.size() > 0 && is_term_expired()) {
		remove_and_seize_all();
		_config.last_board_election_time = current_time_point().sec_since_epoch();
	}

    for (int n = 0; n < sorted_candidates.size(); n++) {
		if(sorted_candidates[n].first > 0) {
			add_to_tfboard(sorted_candidates[n].second);
		}
    }
    
	vector<permission_level_weight> currently_elected = perms_from_members(); //NOTE: needs testing

	if(currently_elected.size() > 0)
		set_permissions(currently_elected);
	
	members_table members(_self, _self.value);

	_config.open_seats = _config.max_board_seats - uint8_t(std::distance(members.begin(), members.end()));

	action(permission_level{get_self(), name("active")}, TELOS_DECIDE_N, name("closevoting"), make_tuple(
		_config.open_election_id
	)).send();
	_config.is_active_election = false;
}

void tfvt::removemember(name member_to_remove) {
	require_auth(get_self());

	remove_and_seize(member_to_remove);
	_config.open_seats++;
	
	auto perms = perms_from_members();
	set_permissions(perms);
}

void tfvt::resign(name member) {
	require_auth(member);

	remove_and_seize(member);
	_config.open_seats++;
	
	auto perms = perms_from_members();
	set_permissions(perms);
}

#pragma endregion Actions


#pragma region Helper_Functions

void tfvt::add_to_tfboard(name nominee) {
    nominees_table noms(get_self(), get_self().value);
    auto n = noms.find(nominee.value);
    check(n != noms.end(), "nominee doesn't exist in table");

    members_table mems(get_self(), get_self().value);
    auto m = mems.find(nominee.value);
    check(m == mems.end(), "nominee is already a board member"); //NOTE: change if error occurs in live environment

    noms.erase(n); //NOTE remove from nominee table

    mems.emplace(get_self(), [&](auto& m) { //NOTE: emplace in boardmembers table
        m.member = nominee;
    });
}

void tfvt::rmv_from_tfboard(name member) {
    members_table mems(get_self(), get_self().value);
    auto m = mems.find(member.value);
    check(m != mems.end(), "member is not on the board");

    mems.erase(m);
}

void tfvt::addseats(name member, uint8_t num_seats) {
    require_auth(get_self());

    config_table configs(get_self(), get_self().value);
    auto c = configs.get();

	c.max_board_seats += num_seats;
	c.open_seats += num_seats;

    configs.set(c, get_self());
}

bool tfvt::is_board_member(name user) {
    members_table mems(get_self(), get_self().value);
    auto m = mems.find(user.value);
	
    return m != mems.end();
}

bool tfvt::is_nominee(name user) {
    nominees_table noms(get_self(), get_self().value);
    auto n = noms.find(user.value);

    return n != noms.end();
}

bool tfvt::is_term_expired() {
	return current_time_point().sec_since_epoch() - _config.last_board_election_time > _config.election_frequency;
}

void tfvt::remove_and_seize_all() {
	members_table members(get_self(), get_self().value);

	auto itr = members.begin();
	vector<name> to_seize;
	while(itr != members.end()) {
		to_seize.emplace_back(itr->member);
		itr = members.erase(itr);
	}
}

void tfvt::remove_and_seize(name member) {
	members_table members(get_self(), get_self().value);
	auto m = members.get(member.value, "board member not found");

	members.erase(m);
}

void tfvt::set_permissions(vector<permission_level_weight> perms) {
	auto self = get_self();
	uint16_t active_weight = perms.size() < 3 ? 1 : ((perms.size() / 3) * 2);

	perms.emplace_back(
		permission_level_weight{ permission_level{
				self,
				"eosio.code"_n
			}, active_weight}
	);
	sort(perms.begin(), perms.end(), [](const auto &first, const auto &second) { return first.permission.actor.value < second.permission.actor.value; });
	
	action(permission_level{get_self(), "owner"_n }, "eosio"_n, "updateauth"_n,
		std::make_tuple(
			get_self(), 
			name("active"), 
			name("owner"),
			authority {
				active_weight, 
				std::vector<key_weight>{},
				perms,
				std::vector<wait_weight>{}
			}
		)
	).send();

	auto tf_it = std::find_if(perms.begin(), perms.end(), [&self](const permission_level_weight &lvlw) {
        return lvlw.permission.actor == self; 
    });
	perms.erase(tf_it);
	uint16_t minor_weight = perms.size() < 4 ? 1 : (perms.size() / 4);
	action(permission_level{get_self(), "owner"_n }, "eosio"_n, "updateauth"_n,
		std::make_tuple(
			get_self(), 
			name("minor"), 
			name("owner"),
			authority {
				minor_weight, 
				std::vector<key_weight>{},
				perms,
				std::vector<wait_weight>{}
			}
		)
	).send();
}

vector<tfvt::permission_level_weight> tfvt::perms_from_members() {
	members_table members(get_self(), get_self().value);
	auto itr = members.begin();
	
	vector<permission_level_weight> perms;
	while(itr != members.end()) {
			perms.emplace_back(permission_level_weight{ permission_level{
				itr->member,
				"active"_n
			}, 1});
		itr++;
	}

	return perms;
}

name tfvt::get_next_ballot_id() {
	name ballot_id = _config.open_election_id;
	if (ballot_id == name()) {
		ballot_id = name("tfvt.");
	}

	ballots_table ballots(TELOS_DECIDE_N, TELOS_DECIDE_N.value);
	// Check 500 ballots ahead
	for (size_t i = 0; i < 500; ++i) {
		ballot_id = name(ballot_id.value + 1);
		auto bal = ballots.find(ballot_id.value);
		if (bal == ballots.end()) {
			return ballot_id;
		}
	}

	check(false, "couldn't secure a ballot_id");
	// silence the compiler
	return name();
}

#pragma endregion Helper_Functions
