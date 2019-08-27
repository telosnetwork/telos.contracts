#include <eosio.amend.hpp>
#include <eosio/symbol.hpp>
#include <eosio/print.hpp>

ratifyamend::ratifyamend(name self, name code, datastream<const char*> ds) : contract(self, code, ds), configs(self, self.value) {
    if (!configs.exists()) {
        configs_struct = config{
            _self, //publisher
            uint32_t(2500000),      // cycle duration in seconds (default 2,500,000 or 5,000,000 blocks or ~29 days)
            uint64_t(1000000),      // default fee amount 100 TLOS
			uint32_t(864000000),    // delay before voting starts on a submission in seconds (~1 day) // or building time ~30 years
            double(5),              // % of all registered voters to pass (minimum, including exactly this value)
            double(66.67),          // % yes over no, to consider it passed (minimum, including exactly this value)
            double(4),              // % of all registered voters to refund fee (minimum, including exactly this value)
            double(25)              // % of yes to give fee back
        };
        configs.set(configs_struct, _self);
    } else {
        configs_struct = configs.get();
    }
}

ratifyamend::~ratifyamend() { }

void ratifyamend::setenv(config new_environment) {
	check(new_environment.expiration_length > 0, "expiration_length must be a non-zero number");
	check(new_environment.start_delay > 0, "start_delay must be a non-zero number");
	check(new_environment.fee > 0, "fee must be a non-zero number");
	check(new_environment.threshold_pass_voters >= 0 && new_environment.threshold_pass_voters <= 100, "threshold pass_voters must be between 0 and 100");
	check(new_environment.threshold_pass_votes >= 0 && new_environment.threshold_pass_votes <= 100, "threshold pass_votes must be between 0 and 100");
	check(new_environment.threshold_fee_voters >= 0 && new_environment.threshold_fee_voters <= 100, "threshold fee_voters must be between 0 and 100");
	check(new_environment.threshold_fee_votes >= 0 && new_environment.threshold_fee_votes <= 100, "threshold fee_votes must be between 0 and 100");
	require_auth(_self);
	configs.set(new_environment, _self);
}

void ratifyamend::getdeposit(name owner) {
    require_auth(owner);
	deposits_table deposits(_self, _self.value);

	const auto& d = deposits.get(owner.value, "Deposit not found");
    require_auth(d.owner);

	action(permission_level{_self, "active"_n}, "eosio.token"_n, "transfer"_n, make_tuple(
		_self,
		d.owner,
		d.escrow,
		std::string("return unused deposit")
	)).send();
	
	deposits.erase(d);
}

void ratifyamend::transfer_handler(name from, name to, asset quantity, string memo) {
	require_auth(from);

	if(to != _self) {
		return;
	}

	if(quantity.symbol == symbol("TLOS", 4)) {
		deposits_table deposits(_self, _self.value);
		auto d = deposits.find(from.value);

		if(d == deposits.end()) {
			deposits.emplace(get_self(), [&](auto& depo) {
				depo.owner = from;
				depo.escrow = quantity;
			});
		} else {
			deposits.modify(d, same_payer, [&](auto& depo) {
				depo.escrow += quantity;
			});
		}
	}

	print("\nDeposit Complete");
}

void ratifyamend::insertdoc(string title, vector<string> clauses) {
    require_auth(_self); //only contract owner can insert new document
    
    documents_table documents(_self, _self.value);

    uint64_t doc_id = documents.available_primary_key();

    documents.emplace(_self, [&]( auto& a ){
        a.document_id = doc_id;
        a.document_title = title;
        a.clauses = clauses;
    });

    print("\nDocument Insertion: SUCCESS");
    print("\nAssigned Document ID: ", doc_id);
}

void ratifyamend::makeproposal(string sub_title, uint64_t doc_id, uint8_t new_clause_num, string new_ipfs_url, name proposer) {
    require_auth(proposer);

    documents_table documents(_self, _self.value);
    const auto& doc = documents.get(doc_id, "Document Not Found");

    check(new_clause_num <= doc.clauses.size() && new_clause_num >= 0, "new clause num is not valid");
	validate_ipfs_link(new_ipfs_url);

	deposits_table deposits(_self, _self.value);
    
	const auto& dep = deposits.get(proposer.value, "Deposit not found, please transfer your TLOS fee");
    asset fee = asset(configs_struct.fee, symbol("TLOS", 4));
	check(dep.escrow >= fee, "Deposit amount is less than fee, please transfer more TLOS");

	if(dep.escrow > fee) {
	    asset outstanding = dep.escrow - fee;
		deposits.modify(dep, same_payer, [&](auto& depo) {
			depo.escrow = outstanding;
		});
	} else  {
		deposits.erase(dep);
	}

	ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
	uint32_t begin_time = current_time_point().sec_since_epoch() + configs_struct.start_delay;
	uint32_t end_time = current_time_point().sec_since_epoch() + configs_struct.start_delay + configs_struct.expiration_length;
	uint64_t next_ballot_id = ballots.available_primary_key();
	action(permission_level{_self, "active"_n}, "eosio.trail"_n, "regballot"_n, make_tuple(
		_self,
		uint8_t(0),
		symbol("VOTE",4),
		begin_time,
		end_time,
		new_ipfs_url
	)).send();

    submissions_table submissions(_self, _self.value);
    auto sub_id = submissions.available_primary_key();

    vector<uint8_t> clause_nums; 
    clause_nums.push_back(new_clause_num);
    
    vector<string> clause_urls;
    clause_urls.push_back(new_ipfs_url);

    submissions.emplace(proposer, [&]( auto& a ){
        a.proposal_id = sub_id;
        a.ballot_id = next_ballot_id;
        a.proposer = proposer;

        a.document_id = doc_id;
        a.proposal_title = sub_title;
        a.new_clause_nums = clause_nums;
        a.new_ipfs_urls = clause_urls;
    });

    print("\nProposal: SUCCESS");
    print("\nAssigned Submission ID: ", sub_id);
}

void ratifyamend::addclause(uint64_t sub_id, uint8_t new_clause_num, string new_ipfs_url) {
    submissions_table submissions(_self, _self.value);
    const auto& sub = submissions.get(sub_id, "proposal doesn't exist");

    require_auth(sub.proposer);

    ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
    auto& bal = ballots.get(sub.ballot_id, "Ballot ID doesn't exist");
	
	proposals_table props_table("eosio.trail"_n, "eosio.trail"_n.value);
	auto& prop = props_table.get(bal.reference_id, "Proposal Not Found");

    check(prop.cycle_count == uint16_t(0), "proposal is no longer in building stage");

    documents_table documents(_self, _self.value);
    const auto& doc = documents.get(sub.document_id, "Document Not Found");

	validate_ipfs_link(new_ipfs_url);
    check(new_clause_num <= doc.clauses.size() && new_clause_num >= 0, "new clause num is not valid");

    bool does_clause_exist = find(sub.new_clause_nums.begin(), sub.new_clause_nums.end(), new_clause_num) != sub.new_clause_nums.end();
    check(!does_clause_exist, "Clause already exists in this submission");
    
    vector<uint8_t> new_clause_nums = sub.new_clause_nums;
    vector<string> new_ipfs_urls = sub.new_ipfs_urls;

    new_clause_nums.push_back(new_clause_num);
    new_ipfs_urls.push_back(new_ipfs_url);

    submissions.modify(sub, same_payer, [&]( auto& a ) {
        a.new_clause_nums = new_clause_nums;
        a.new_ipfs_urls = new_ipfs_urls;
    });

    print("\nAdd Clause: SUCCESS");
}

void ratifyamend::removeclause(uint64_t sub_id, uint8_t clause_to_remove)
{
    submissions_table submissions(_self, _self.value);
    const auto& sub = submissions.get(sub_id, "submission does not exist");

    require_auth(sub.proposer);
	
    ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
    const auto& bal = ballots.get(sub.ballot_id, "Ballot ID doesn't exist");

    proposals_table props_table("eosio.trail"_n, "eosio.trail"_n.value);
	const auto& prop = props_table.get(bal.reference_id, "Proposal Not Found");

    check(prop.cycle_count == uint16_t(0), "proposal is no longer in building stage");

    auto clauses = sub.new_clause_nums;
    auto links = sub.new_ipfs_urls;

    auto clause_it = find(clauses.begin(), clauses.end(), clause_to_remove);

    check(clause_it != clauses.end(), "Clause does not exist in submission");

    auto clause_pos = distance(clauses.begin(), clause_it);
    auto link_it = links.begin() + clause_pos;

    clauses.erase(clause_it);
    links.erase(link_it);

    submissions.modify(sub, same_payer, [&](auto& s) { 
        s.new_clause_nums = clauses;
        s.new_ipfs_urls = links;
    });
}

void ratifyamend::cancelsub(uint64_t sub_id) {
	submissions_table submissions(_self, _self.value);
	auto s_itr = submissions.find(sub_id);
    check(s_itr != submissions.end(), "Submission not found");
    auto s = *s_itr;

	require_auth(s.proposer);
	ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
	auto b = ballots.get(s.ballot_id, "Ballot not found on eosio.trail ballots_table");

	proposals_table proposals("eosio.trail"_n, "eosio.trail"_n.value);
	auto p = proposals.get(b.reference_id, "Prosal not found on eosio.trail proposals_table");

	check(p.cycle_count == uint16_t(0), "proposal is no longer in building stage");
    check(p.status == uint8_t(0), "Proposal is already closed");
	check(current_time_point().sec_since_epoch() < p.begin_time, "Proposal voting has already begun. Unable to cancel.");

	action(permission_level{ _self, "active"_n }, "eosio.trail"_n, "unregballot"_n, make_tuple(
        _self,
		s.ballot_id
    )).send();

	submissions.erase(s_itr);
}

void ratifyamend::openvoting(uint64_t sub_id) {
    submissions_table submissions(_self, _self.value);
    auto& sub = submissions.get(sub_id, "Proposal Not Found");

	require_auth(sub.proposer);

    ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
    auto& bal = ballots.get(sub.ballot_id, "Ballot ID doesn't exist");

	proposals_table props_table("eosio.trail"_n, "eosio.trail"_n.value);
	auto& prop = props_table.get(bal.reference_id, "Proposal Not Found");

    check(prop.cycle_count == uint16_t(0), "proposal is no longer in building stage");
    check(prop.status == uint8_t(0), "Proposal is already closed");
    check(sub.new_clause_nums.size() > 0, "Submission must have at least one clause edit");

	uint32_t begin_time = current_time_point().sec_since_epoch();
	uint32_t end_time = current_time_point().sec_since_epoch() + configs_struct.expiration_length;
    action(permission_level{ _self, "active"_n }, "eosio.trail"_n, "nextcycle"_n, make_tuple(
        _self,
        sub.ballot_id,
        begin_time,
        end_time
    )).send();

    print("\nReady Proposal: SUCCESS");
}

void ratifyamend::closeprop(uint64_t sub_id) {
    submissions_table submissions(_self, _self.value);
    const auto& sub = submissions.get(sub_id, "Proposal Not Found");

	require_auth(sub.proposer);

    ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
    const auto& bal = ballots.get(sub.ballot_id, "Ballot ID doesn't exist");
	
	proposals_table props_table("eosio.trail"_n, "eosio.trail"_n.value);
	const auto& prop = props_table.get(bal.reference_id, "Proposal Not Found");

    check(prop.end_time < current_time_point().sec_since_epoch(), "Proposal is still open");
    check(prop.status == uint8_t(0), "Proposal is already closed");

    registries_table registries("eosio.trail"_n, "eosio.trail"_n.value);
    auto e = registries.find(symbol("VOTE", 4).code().raw());

    asset total_votes = prop.yes_count + prop.no_count + prop.abstain_count;
    asset non_abstain_votes = prop.yes_count + prop.no_count; 

    //pass thresholds
    asset quorum = e->supply * configs_struct.threshold_pass_voters / 100;
    asset votes_pass_thresh = non_abstain_votes * configs_struct.threshold_pass_votes / 100;

    //fee refund thresholds
    asset voters_fee_thresh = e->supply * configs_struct.threshold_fee_voters / 100; 
    asset votes_fee_thresh = total_votes * configs_struct.threshold_fee_votes / 100; 

    if(prop.yes_count >= votes_fee_thresh && total_votes >= voters_fee_thresh) {
        action(permission_level{ _self, "active"_n }, "eosio.token"_n, "transfer"_n, make_tuple(
            _self,
            sub.proposer,
            asset(int64_t(configs_struct.fee), symbol("TLOS", 4)),
            std::string("Ratify/Amend Proposal Fee Refund")
        )).send();
    }

    uint8_t new_status = uint8_t(2);
    if(prop.yes_count > votes_pass_thresh && total_votes >= quorum ) {
        update_doc(sub.document_id, sub.new_clause_nums, sub.new_ipfs_urls);
        new_status = uint8_t(1);
    }
    
    action(permission_level{ _self, "active"_n }, "eosio.trail"_n, "closeballot"_n, make_tuple(
        _self,
        sub.ballot_id,
        new_status
    )).send();
}

#pragma region Helper_Functions

void ratifyamend::update_doc(uint64_t document_id, vector<uint8_t> new_clause_nums, vector<string> new_ipfs_urls) {
    documents_table documents(_self, _self.value);
    auto d = documents.find(document_id);
    auto doc = *d;

    auto doc_size = doc.clauses.size();
    for (int i = 0; i < new_clause_nums.size(); i++) {
        if (new_clause_nums[i] < doc.clauses.size()) { //update existing clause
            doc.clauses[new_clause_nums[i]] = new_ipfs_urls.at(i);
        } else { //add new clause
            doc.clauses.push_back(new_ipfs_urls.at(i));
        }
    }

    documents.modify(d, same_payer, [&]( auto& a ) {
        a.clauses = doc.clauses;
    });
}

#pragma endregion Helper_Functions
