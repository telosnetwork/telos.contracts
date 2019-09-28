#include <worker.proposals.hpp>
#include <eosio/symbol.hpp>
#include <eosio/print.hpp>

using namespace eosio;
using eosio::print;

workerproposal::workerproposal(name self, name code, datastream<const char*> ds) : contract(self, code, ds), wpenv(self, self.value) {
    if (!wpenv.exists()) {

        wp_env_struct = wp_env{
			_self,
            uint32_t(2500000),    // cycle duration in seconds (default 2,500,000 or 5,000,000 blocks or ~29 days)
            uint16_t(3),          // percent from requested amount (default 3%)
            uint64_t(500000),     // minimum fee amount (default 50 TLOS)
			uint32_t(864000000),      // delay before voting starts on a submission in seconds (~30 years)
            double(5),          // % of all registered voters to pass (minimum, including exactly this value)
            double(50),         // % yes over no, to consider it passed (strictly over this value)
            double(4),          // % of all registered voters to refund fee (minimum, including exactly this value)
            double(20)          // % of yes to give fee back
        };

        wpenv.set(wp_env_struct, _self);
    } else {
        wp_env_struct = wpenv.get();     
    }
}

workerproposal::~workerproposal() { }

void workerproposal::setenv(wp_env new_environment) {
	check(new_environment.cycle_duration > 0, "cycle duration must be a non-zero number");
	check(new_environment.fee_percentage > 0, "fee_percentage must be a non-zero number");
	check(new_environment.start_delay > 0, "start_delay must be a non-zero number");
	check(new_environment.fee_min > 0, "fee_min must be a non-zero number");
	check(new_environment.threshold_pass_voters >= 0 && new_environment.threshold_pass_voters <= 100, "threshold pass_voters must be between 0 and 100");
	check(new_environment.threshold_pass_votes >= 0 && new_environment.threshold_pass_votes <= 100, "threshold pass_votes must be between 0 and 100");
	check(new_environment.threshold_fee_voters >= 0 && new_environment.threshold_fee_voters <= 100, "threshold fee_voters must be between 0 and 100");
	check(new_environment.threshold_fee_votes >= 0 && new_environment.threshold_fee_votes <= 100, "threshold fee_votes must be between 0 and 100");
	require_auth(_self);
	wpenv.set(new_environment, _self);
}

void workerproposal::getdeposit(name owner) {
    require_auth(owner);
	deposits_table deposits(_self, _self.value);
	auto d_itr = deposits.find(owner.value);
    check(d_itr != deposits.end(), "Deposit not found");

	auto d = *d_itr;
    require_auth(d.owner);

	action(permission_level{_self, "active"_n}, "eosio.token"_n, "transfer"_n, make_tuple(
		_self,
		d.owner,
		d.escrow,
		std::string("return unused deposit")
	)).send();
	
	deposits.erase(d_itr);
}

void workerproposal::submit(name proposer, std::string title, uint16_t cycles, std::string ipfs_location, asset amount, name receiver) {
    require_auth(proposer);
	
	print("\nCalculating fee");
	uint64_t fee_amount = uint64_t(amount.amount) * uint64_t( wp_env_struct.fee_percentage ) / uint64_t(100);
    fee_amount = fee_amount > wp_env_struct.fee_min ? fee_amount :  wp_env_struct.fee_min;
	asset fee = asset(fee_amount, symbol("TLOS", 4));

	print("\nFinding deposit");
	deposits_table deposits(_self, _self.value);
	auto d_itr = deposits.find(proposer.value);
	check(d_itr != deposits.end(), "Deposit not found, please transfer your TLOS fee");
	auto d = *d_itr;
	check(d.escrow >= fee, "Deposit amount is less than fee, please transfer more TLOS");
	
	if(d.escrow > fee) {
		print("\nModify deposit");
	    asset outstanding = d.escrow - fee;
		deposits.modify(d_itr, same_payer, [&](auto& depo) {
			depo.escrow = outstanding;
		});
	} else {
		print("\ndelete deposit");
		deposits.erase(d_itr);
	}

	ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
	uint32_t begin_time = current_time_point().sec_since_epoch() + wp_env_struct.start_delay;
	uint32_t end_time = current_time_point().sec_since_epoch() + wp_env_struct.start_delay + wp_env_struct.cycle_duration;
	uint64_t next_ballot_id = ballots.available_primary_key();
	action(permission_level{_self, "active"_n}, "eosio.trail"_n, "regballot"_n, make_tuple(
		_self,
		uint8_t(0),
		symbol("VOTE",4),
		begin_time,
		end_time,
		ipfs_location
	)).send();

    submissions_table submissions(_self, _self.value);
    submissions.emplace( proposer, [&]( submission& info ){
        info.id             = submissions.available_primary_key();
        info.ballot_id      = next_ballot_id;
        info.proposer       = proposer;
        info.receiver       = receiver;
        info.title          = title;
        info.ipfs_location  = ipfs_location;
        info.cycles         = cycles + 1;
        info.amount         = uint64_t(amount.amount);
        info.fee            = fee_amount;
    });
}

void workerproposal::openvoting(uint64_t sub_id) {
	print("\n look for sub id");
	submissions_table submissions(_self, _self.value);
	auto s = submissions.get(sub_id, "Submission not found");

	require_auth(s.proposer);

	print("\n getting ballot");
	ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
	auto b = ballots.get(s.ballot_id, "Ballot not found on eosio.trail ballots_table");

	print("\n getting proposal");
	proposals_table proposals("eosio.trail"_n, "eosio.trail"_n.value);
	auto p = proposals.get(b.reference_id, "Prosal not found on eosio.trail proposals_table");

	check(p.cycle_count == uint16_t(0), "proposal is no longer in building stage");
    check(p.status == uint8_t(0), "Proposal is already closed");

	uint32_t begin_time = current_time_point().sec_since_epoch();
	uint32_t end_time = current_time_point().sec_since_epoch() + wp_env_struct.cycle_duration;
	print("\n sending inline trx");
    action(permission_level{ _self, "active"_n }, "eosio.trail"_n, "nextcycle"_n, make_tuple(
        _self,
        s.ballot_id,
        begin_time,
        end_time
    )).send();
	print("\n after inline trx");

    print("\nReady Proposal: SUCCESS");
}

void workerproposal::cancelsub(uint64_t sub_id) {
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

	action(permission_level{ _self, "active"_n }, "eosio.trail"_n, "unregballot"_n, make_tuple(
        _self,
		s.ballot_id
    )).send();

	submissions.erase(s_itr);
}

void workerproposal::claim(uint64_t sub_id) {
    submissions_table submissions(_self, _self.value);
    const auto& sub = submissions.get(sub_id, "Worker Proposal Not Found");

	require_auth(sub.proposer);

    ballots_table ballots("eosio.trail"_n, "eosio.trail"_n.value);
    const auto& bal = ballots.get(sub.ballot_id, "Ballot ID doesn't exist");
	
	proposals_table props_table("eosio.trail"_n, "eosio.trail"_n.value);
	const auto& prop = props_table.get(bal.reference_id, "Proposal Not Found");

    check(prop.end_time < current_time_point().sec_since_epoch(), "Cycle is still open");
    check(prop.status == uint8_t(0), "Proposal is closed");

    registries_table registries("eosio.trail"_n, "eosio.trail"_n.value);
    auto e = registries.find(symbol("VOTE", 4).code().raw());

    asset outstanding = asset(0, symbol("TLOS", 4));
    asset total_votes = prop.yes_count + prop.no_count + prop.abstain_count; //total votes cast on proposal
    asset non_abstain_votes = prop.yes_count + prop.no_count; 

    //pass thresholds
    asset quorum = e->supply * wp_env_struct.threshold_pass_voters / 100;
    asset votes_pass_thresh = non_abstain_votes * wp_env_struct.threshold_pass_votes / 100;

    //fee refund thresholds
    asset voters_fee_thresh = e->supply * wp_env_struct.threshold_fee_voters / 100; 
    asset votes_fee_thresh = total_votes * wp_env_struct.threshold_fee_votes / 100; 

    auto updated_fee = sub.fee;

    // print("\n GET FEE BACK WHEN <<<< ", prop.yes_count, " >= ", votes_fee_thresh," && ", total_votes, " >= ", voters_fee_thresh);
    if(sub.fee && prop.yes_count.amount > 0 && prop.yes_count >= votes_fee_thresh && total_votes >= voters_fee_thresh) {
        asset the_fee = asset(int64_t(sub.fee), symbol("TLOS", 4));
        if(sub.receiver == sub.proposer) {
            outstanding += the_fee;
        } else {
            action(permission_level{ _self, "active"_n }, "eosio.token"_n, "transfer"_n, make_tuple(
                _self,
                sub.proposer,
                the_fee,
                std::string("Worker proposal funds")
            )).send();
        }
        updated_fee = 0;
    }

    // print("\n GET MUNI WHEN <<<< ", prop.yes_count, " > ", votes_pass_thresh, " && ", total_votes, " >= ", voters_pass_thresh);
    if(prop.yes_count > votes_pass_thresh && total_votes >= quorum) {
        outstanding += asset(int64_t(sub.amount), symbol("TLOS", 4));
    }
    
    // print("\n numbers : ", total_votes, " * ", wp_env_struct.threshold_pass_voters, " | ", wp_env_struct.threshold_fee_voters, " ---- ", total_votes, " ", prop.yes_count, " ", prop.no_count);
    if(outstanding.amount > 0) {
        action(permission_level{ _self, "active"_n }, "eosio.token"_n, "transfer"_n, make_tuple(
            _self,
            sub.receiver,
            outstanding,
            std::string("Worker proposal funds") //TODO: improve memo messaging
        )).send();
    }

    if(prop.cycle_count == uint16_t(sub.cycles - 1)) { //Close ballot because it was the last cycle for the submission.
        uint8_t new_status = 1;
		action(permission_level{ _self, "active"_n }, "eosio.trail"_n, "closeballot"_n, make_tuple(
			_self,
			sub.ballot_id,
			new_status
		)).send();
    } else if(prop.cycle_count < uint16_t(sub.cycles - 1)) { //Start next cycle because number of cycles hasn't been reached.
		uint32_t begin_time = current_time_point().sec_since_epoch();
		uint32_t end_time = current_time_point().sec_since_epoch() + wp_env_struct.cycle_duration;
		action(permission_level{ _self, "active"_n }, "eosio.trail"_n, "nextcycle"_n, make_tuple(
			_self,
			sub.ballot_id,
			begin_time,
			end_time
		)).send();
	}

    submissions.modify(sub, same_payer, [&]( auto& a ) {
        a.fee = updated_fee;
    });
}

// note : this gets called when eosio.saving transfers OUT tokens too 
// => deposits.owner eosio.saving will contain the entire sum of what was paid out , EVER (including fees and everything)
void workerproposal::transfer_handler(name from, name to, asset quantity, string memo) {
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
