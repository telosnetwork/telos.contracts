#include "../include/eosio.trail.hpp"

trail::trail(name self, name code, datastream<const char*> ds) : contract(self, code, ds), environment(self, self.value) {
    if (!environment.exists()) {
        vector<uint64_t> new_totals = {0,0,0};

        env_struct = env{
            self, //publisher
            new_totals, //totals
            current_time_point().sec_since_epoch(), //time_now
            0 //last_ballot_id
        };

        environment.set(env_struct, self);
    } else {
        env_struct = environment.get();
        env_struct.time_now = current_time_point().sec_since_epoch();
    }
}

trail::~trail() {
    if (environment.exists()) {
        environment.set(env_struct, env_struct.publisher);
    }
}

#pragma region Token_Registration

void trail::regtoken(asset max_supply, name publisher, string info_url) {
    require_auth(publisher);

    auto sym = max_supply.symbol;

    registries_table registries(_self, _self.value);
    auto r = registries.find(sym.code().raw());
    check(r == registries.end(), "Token Registry with that symbol already exists in Trail");

    token_settings default_settings;

    registries.emplace(publisher, [&]( auto& a ){
        a.max_supply = max_supply;
        a.supply = asset(0, sym);
        a.total_voters = uint32_t(0);
        a.total_proxies = uint32_t(0);
        a.publisher = publisher;
        a.info_url = info_url;
        a.settings = default_settings;
    });

    print("\nToken Registration: SUCCESS");
}

void trail::initsettings(name publisher, symbol token_symbol, token_settings new_settings) {
    require_auth(publisher);

    registries_table registries(_self, _self.value);
    auto r = registries.find(token_symbol.code().raw());
    check(r != registries.end(), "Token Registry with that symbol doesn't exist");
    auto reg = *r;

    check(reg.publisher == publisher, "cannot change settings of another account's registry");
    check(new_settings.counterbal_decay_rate > 0, "cannot have a counterbalance with zero decay");

    if (reg.settings.is_initialized) {
        check(!reg.settings.lock_after_initialize, "settings have been locked");
    } else {
        new_settings.is_initialized = true;
    }

    registries.modify(r, same_payer, [&]( auto& a ) {
        a.settings = new_settings;
    });

    print("\nToken Settings Update: SUCCESS");
}

void trail::unregtoken(symbol token_symbol, name publisher) {
    require_auth(publisher);
    
    registries_table registries(_self, _self.value);
    auto r = registries.find(token_symbol.code().raw());
    check(r != registries.end(), "No Token Registry found matching given symbol");
    auto reg = *r;

    check(reg.settings.is_destructible == true, "Token Registry has been set as indestructible");

    registries.erase(r);

    print("\nToken Unregistration: SUCCESS");
}

#pragma endregion Token_Registration


#pragma region Token_Actions

void trail::issuetoken(name publisher, name recipient, asset tokens, bool airgrab) {
    require_auth(publisher);
    check(tokens > asset(0, tokens.symbol), "must issue more than 0 tokens");

    registries_table registries(_self, _self.value);
    auto r = registries.find(tokens.symbol.code().raw());
    check(r != registries.end(), "registry doesn't exist for that token");
    auto reg = *r;
    check(reg.publisher == publisher, "only publisher can issue tokens");

    asset new_supply = (reg.supply + tokens);
    check(new_supply <= reg.max_supply, "Issuing tokens would breach max supply");

    registries.modify(r, same_payer, [&]( auto& a ) { //NOTE: update supply
        a.supply = new_supply;
    });

    if (airgrab) { //NOTE: place in airgrabs table to be claimed by recipient
        airgrabs_table airgrabs(_self, publisher.value);
        auto g = airgrabs.find(recipient.value);

        if (g == airgrabs.end()) { //NOTE: new airgrab
            airgrabs.emplace(publisher, [&]( auto& a ){
                a.recipient = recipient;
                a.tokens = tokens;
            });
        } else { //NOTE: add to existing airgrab
            airgrabs.modify(g, same_payer, [&]( auto& a ) {
                a.tokens += tokens;
            });
        }

        print("\nToken Airgrab: SUCCESS");
    } else { //NOTE: publisher pays RAM cost if recipient has no balance entry (airdrop)
        balances_table balances(_self, tokens.symbol.code().raw());
        auto b = balances.find(recipient.value);

        if (b == balances.end()) { //NOTE: new balance
            balances.emplace(publisher, [&]( auto& a ){
                a.owner = recipient;
                a.tokens = tokens;
            });
        } else { //NOTE: add to existing balance
            balances.modify(b, same_payer, [&]( auto& a ) {
                a.tokens += tokens;
            });
        }

        print("\nToken Airdrop: SUCCESS");
    }

    //TODO: add counterbalance to issue? or only transfers?

    print("\nAmount: ", tokens);
    print("\nRecipient: ", recipient);
}

//TODO: remove pulisher as param? is findable through token symbol (implemented, just need to remove from signature)
void trail::claimairgrab(name claimant, name publisher, symbol token_symbol) {
    require_auth(claimant);

    registries_table registries(_self, _self.value);
    auto r = registries.find(token_symbol.code().raw());
    check(r != registries.end(), "Token Registry with that symbol doesn't exist in Trail");
    auto reg = *r;

    airgrabs_table airgrabs(_self, reg.publisher.value);
    auto g = airgrabs.find(claimant.value);
    check(g != airgrabs.end(), "no airgrab to claim");
    auto grab = *g;
    check(grab.recipient == claimant, "cannot claim another account's airdrop");

    balances_table balances(_self, token_symbol.code().raw());
    auto b = balances.find(claimant.value);

    if (b == balances.end()) { //NOTE: create a wallet, RAM paid by claimant
        balances.emplace(claimant, [&]( auto& a ){
            a.owner = claimant;
            a.tokens = grab.tokens;
        });
    } else { //NOTE: add to existing balance
        balances.modify(b, same_payer, [&]( auto& a ) {
            a.tokens += grab.tokens;
        });
    }

    airgrabs.erase(g); //NOTE: erase airgrab

    print("\nAirgrab Claim: SUCCESS");
}

//NOTE: only balance owner can burn tokens
void trail::burntoken(name balance_owner, asset amount) {
    require_auth(balance_owner);
    check(amount > asset(0, amount.symbol), "must claim more than 0 tokens");

    registries_table registries(_self, _self.value);
    auto r = registries.find(amount.symbol.code().raw());
    check(r != registries.end(), "registry doesn't exist for given token");
    auto reg = *r;

    //TODO: make is_burnable_by_publisher/is_burnable_by_holder?
    check(reg.settings.is_burnable == true, "token registry doesn't allow burning");

    balances_table balances(_self, amount.symbol.code().raw());
    auto b = balances.find(balance_owner.value);
    check(b != balances.end(), "balance owner has no balance to burn");
    auto bal = *b;

    asset new_supply = (reg.supply - amount);
    asset new_balance = bal.tokens - amount;

    check(new_balance >= asset(0, bal.tokens.symbol), "cannot burn more tokens than are owned");

    registries.modify(r, same_payer, [&]( auto& a ) {
        a.supply = new_supply;
    });

    balances.modify(b, same_payer, [&]( auto& a ) { //NOTE: subtract amount from balance
        a.tokens -= amount;
    });
    
    print("\nToken Burn: SUCCESS");
}

//TODO: allow seizing if registry doesn't exist?
void trail::seizetoken(name publisher, name owner, asset tokens) {
    require_auth(publisher);
    check(publisher != owner, "cannot seize your own tokens");
    check(tokens > asset(0, tokens.symbol), "must seize greater than 0 tokens");

    registries_table registries(_self, _self.value);
    auto r = registries.find(tokens.symbol.code().raw());
    check(r != registries.end(), "registry doesn't exist for given token");
    auto reg = *r;

    check(reg.publisher == publisher, "only publisher can seize tokens");
    check(reg.settings.is_seizable == true, "token registry doesn't allow seizing");

    balances_table ownerbals(_self, tokens.symbol.code().raw());
    auto ob = ownerbals.find(owner.value);
    check(ob != ownerbals.end(), "user has no balance to seize");
    auto obal = *ob;
    check(obal.tokens - tokens >= asset(0, obal.tokens.symbol), "cannot seize more tokens than user owns");

    ownerbals.modify(ob, same_payer, [&]( auto& a ) { //NOTE: subtract amount from balance
        a.tokens -= tokens;
    });

    balances_table publisherbal(_self, tokens.symbol.code().raw());
    auto pb = publisherbal.find(publisher.value);
    check(pb != publisherbal.end(), "publisher has no balance to hold seized tokens");
    auto pbal = *pb;

    publisherbal.modify(pb, same_payer, [&]( auto& a ) { //NOTE: add seized tokens to publisher balance
        a.tokens += tokens;
    });

    print("\nToken Seizure: SUCCESS");
}

void trail::seizeairgrab(name publisher, name recipient, asset amount) {
    require_auth(publisher);
    check(amount > asset(0, amount.symbol), "must seize greater than 0 tokens");

    registries_table registries(_self, _self.value);
    auto r = registries.find(amount.symbol.code().raw());
    check(r != registries.end(), "registry doesn't exist for given token");
    auto reg = *r;

    check(reg.publisher == publisher, "only publisher can seize airgrabs");
    check(reg.settings.is_seizable == true, "token registry doesn't allow seizing");

    airgrabs_table airgrabs(_self, publisher.value);
    auto g = airgrabs.find(recipient.value);
    check(g != airgrabs.end(), "recipient has no airgrab");
    auto grab = *g;

    check(grab.tokens - amount >= asset(0, grab.tokens.symbol), "cannot seize more tokens than airgrab holds");

    if (amount - grab.tokens == asset(0, grab.tokens.symbol)) { //NOTE: all tokens seized :(
        airgrabs.erase(g);
    } else { //NOTE: airgrab still has balance after seizure
        airgrabs.modify(g, same_payer, [&]( auto& a ) {
            a.tokens -= amount;
        });
    }

    balances_table publisherbal(_self, amount.symbol.code().raw());
    auto pb = publisherbal.find(publisher.value);
    check(pb != publisherbal.end(), "publisher has no balance to hold seized tokens");
    auto pbal = *pb;

    publisherbal.modify(pb, same_payer, [&]( auto& a ) { //NOTE: add seized tokens to publisher balance
        a.tokens += amount;
    });

    print("\nAirgrab Seizure: SUCCESS");
}

void trail::seizebygroup(name publisher, vector<name> group, asset tokens) {
    require_auth(publisher);
    //check(publisher != owner, "cannot seize your own tokens");
    check(tokens > asset(0, tokens.symbol), "must seize greater than 0 tokens");

    registries_table registries(_self, _self.value);
    auto r = registries.find(tokens.symbol.code().raw());
    check(r != registries.end(), "registry doesn't exist for given token");
    auto reg = *r;

    check(reg.publisher == publisher, "only publisher can seize tokens");
    check(reg.settings.is_seizable == true, "token registry doesn't allow seizing");

    for (name n : group) {

        balances_table ownerbals(_self, tokens.symbol.code().raw());
        auto ob = ownerbals.find(n.value);
        check(ob != ownerbals.end(), "user has no balance to seize");
        auto obal = *ob;
        check(obal.tokens - tokens >= asset(0, obal.tokens.symbol), "cannot seize more tokens than user owns");

        ownerbals.modify(ob, same_payer, [&]( auto& a ) { //NOTE: subtract amount from balance
            a.tokens -= tokens;
        });

        balances_table publisherbal(_self, tokens.symbol.code().raw());
        auto pb = publisherbal.find(publisher.value);
        check(pb != publisherbal.end(), "publisher has no balance to hold seized tokens");
        auto pbal = *pb;

        publisherbal.modify(pb, same_payer, [&]( auto& a ) { //NOTE: add seized tokens to publisher balance
            a.tokens += tokens;
        });
    } 

    print("\nToken Seizure: SUCCESS");
}

void trail::raisemax(name publisher, asset amount) {
    require_auth(publisher);
    check(amount > asset(0, amount.symbol), "amount must be greater than 0");

    registries_table registries(_self, _self.value);
    auto r = registries.find(amount.symbol.code().raw());
    check(r != registries.end(), "registry doesn't exist for given token");
    auto reg = *r;

    check(reg.publisher == publisher, "cannot raise another registry's max supply");
    check(reg.settings.is_max_mutable == true, "token registry doesn't allow raising max supply");

    registries.modify(r, same_payer, [&]( auto& a ) {
        a.max_supply += amount;
    });

    print("\nRaise Max Supply: SUCCESS");
}

void trail::lowermax(name publisher, asset amount) {
    require_auth(publisher);
    check(amount > asset(0, amount.symbol), "amount must be greater than 0");

    registries_table registries(_self, _self.value);
    auto r = registries.find(amount.symbol.code().raw());
    check(r != registries.end(), "registry doesn't exist for that token");
    auto reg = *r;

    check(reg.publisher == publisher, "cannot lower another account's max supply");
    check(reg.settings.is_max_mutable == true, "token settings don't allow lowering max supply");
    check(reg.supply <= reg.max_supply - amount, "cannot lower max supply below circulating supply");
    check(reg.max_supply - amount >= asset(0, amount.symbol), "cannot lower max supply below 0");

    registries.modify(r, same_payer, [&]( auto& a ) {
        a.max_supply -= amount;
    });

    print("\nLower Max Supply: SUCCESS");
}

void trail::transfer(name sender, name recipient, asset amount) {
    require_auth(sender);
    check(sender != recipient, "cannot send tokens to yourself");
    check(amount > asset(0, amount.symbol), "must transfer grater than 0 tokens");

    registries_table registries(_self, _self.value);
    auto r = registries.find(amount.symbol.code().raw());
    check(r != registries.end(), "token registry doesn't exist");
    auto reg = *r;

    check(reg.settings.is_transferable == true, "token registry disallows transfers");

    balances_table senderbal(_self, amount.symbol.code().raw());
    auto sb = senderbal.find(sender.value);
    check(sb != senderbal.end(), "sender doesn't have a balance");
    auto sbal = *sb;
    check(sbal.tokens - amount >= asset(0, amount.symbol), "insufficient funds in sender's balance");

    balances_table recbal(_self, amount.symbol.code().raw());
    auto rb = recbal.find(recipient.value);
    check(rb != recbal.end(), "recipient doesn't have a balance to hold transferred funds");
    auto rbal = *rb;

    //NOTE: subtract amount from sender
    senderbal.modify(sb, same_payer, [&]( auto& a ) {
        a.tokens -= amount;
    });

    //NOTE: add amount to recipient
    recbal.modify(rb, same_payer, [&]( auto& a ) {
        a.tokens += amount;
    });

    //NOTE: calculating counterbalances and decays

    counterbalances_table sendercb(_self, amount.symbol.code().raw());
    auto scb = sendercb.find(sender.value);
    
    if (scb == sendercb.end()) { //NOTE: sender doesn't have a counterbalance yet
        sendercb.emplace(sender, [&]( auto& a ){
            a.owner = sender;
            a.decayable_cb = asset(0, amount.symbol);
            a.persistent_cb = asset(0, amount.symbol);
            a.last_decay = current_time_point().sec_since_epoch();
        });
    } else {
        auto scbal = *scb;
        asset s_decay_amount = get_decay_amount(sender, amount.symbol, reg.settings.counterbal_decay_rate);
        asset new_s_cbal = (scbal.decayable_cb - s_decay_amount) - amount;

        if (new_s_cbal < asset(0, scbal.decayable_cb.symbol)) { //NOTE: if scbal < 0, set to 0
            sendercb.modify(scb, same_payer, [&]( auto& a ) {
                a.decayable_cb = asset(0, scbal.decayable_cb.symbol);
            });
        } else {
            sendercb.modify(scb, same_payer, [&]( auto& a ) {
                a.decayable_cb = new_s_cbal;
            });
        }
    }

    counterbalances_table reccb(_self, amount.symbol.code().raw());
    auto rcb = reccb.find(recipient.value);

    if (rcb == reccb.end()) { //NOTE: recipient doesn't have a counterbalance yet
        reccb.emplace(sender, [&]( auto& a ){
            a.owner = recipient;
            a.decayable_cb = amount;
            a.persistent_cb = asset(0, amount.symbol);
            a.last_decay = current_time_point().sec_since_epoch();
        });
    } else {
        auto rcbal = *rcb;
        asset r_decay_amount = get_decay_amount(recipient, amount.symbol, reg.settings.counterbal_decay_rate);
        asset new_r_cbal = (rcbal.decayable_cb - r_decay_amount);

        if (new_r_cbal <= asset(0, rcbal.decayable_cb.symbol)) { //NOTE: only triggers if decayed below 0
            new_r_cbal = asset(0, rcbal.decayable_cb.symbol);
        }

        new_r_cbal += amount;
        
        reccb.modify(rcb, same_payer, [&]( auto& a ) {
            a.decayable_cb = new_r_cbal;
        });
    }

    print("\nToken Transfer: SUCCESS");
}

#pragma endregion Token_Actions


#pragma region Voter_Registration

//NOTE: effectively createwallet()
void trail::regvoter(name voter, symbol token_symbol) {
    require_auth(voter);

    //symbol core_symbol = symbol("VOTE", 4);

    registries_table registries(_self, _self.value);
    auto r = registries.find(token_symbol.code().raw());
    check(r != registries.end(), "registry doesn't exist for given token");

    balances_table balances(_self, token_symbol.code().raw());
    auto b = balances.find(voter.value);
    check(b == balances.end(), "Voter already exists");

    balances.emplace(voter, [&]( auto& a ){
        a.owner = voter;
        a.tokens = asset(0, token_symbol);
    });

    registries.modify(r, same_payer, [&]( auto& a ) {
        a.total_voters += uint32_t(1);
    });

    print("\nVoter Registration: SUCCESS ");
}

//NOTE: effectively deletewallet()
void trail::unregvoter(name voter, symbol token_symbol) {
    require_auth(voter);

    //symbol core_symbol = symbol("VOTE", 4);

    balances_table balances(_self, token_symbol.code().raw());
    auto b = balances.find(voter.value);
    check(b != balances.end(), "voter doesn't exist to unregister");
    auto bal = *b;

    registries_table registries(_self, _self.value);
    auto r = registries.find(token_symbol.code().raw());
    check(r != registries.end(), "registry doesn't exist");
    auto reg = *r;

    check(reg.settings.is_burnable == true, "token registry disallows burning of funds, transfer whole balance before attempting to unregister.");

    registries.modify(r, same_payer, [&]( auto& a ) {
        a.supply -= bal.tokens;
        a.total_voters -= uint32_t(1);
    });

    //TODO: check that voter has zero proxied tokens?

    balances.erase(b);

    print("\nVoter Unregistration: SUCCESS");
}

#pragma endregion Voter_Registration


#pragma region Voting_Actions

void trail::mirrorcast(name voter, symbol token_symbol) {
    require_auth(voter);

    //TODO: add ability to mirrorcast any eosio.token? casted tokens could still be transferable but would inherit the counterbalance system
    check(token_symbol == symbol("TLOS", 4), "feature in development. can only mirrorcast TLOS");
    auto vote_sym = symbol("VOTE", 4);

    asset max_votes = get_liquid_tlos(voter) + get_staked_tlos(voter);
	auto new_votes = asset(max_votes.amount, symbol("VOTE", 4)); //NOTE: converts TLOS balance to VOTE tokens
    check(max_votes.symbol == symbol("TLOS", 4), "only TLOS can be used to get VOTEs"); //NOTE: redundant?
    check(max_votes > asset(0, symbol("TLOS", 4)), "must get a positive amount of VOTEs"); //NOTE: redundant?

    balances_table balances(_self, new_votes.symbol.code().raw());
    auto b = balances.find(voter.value);
    check(b != balances.end(), "voter is not registered");
    auto bal = *b;

    //subtract old balance from supply
    registries_table registries(_self, _self.value);
    auto r = registries.find(vote_sym.code().raw());
    check(r != registries.end(), "Token Registry with that symbol doesn't exist in Trail");
    auto reg = *r;
    reg.supply -= bal.tokens;

    counterbalances_table counterbals(_self, new_votes.symbol.code().raw());
    auto cb = counterbals.find(voter.value);
    //asset cb_weight = asset(0, max_votes.symbol);
    counter_balance counter_bal;

    asset decay_amount = get_decay_amount(voter, new_votes.symbol, DECAY_RATE);
    
    if (cb != counterbals.end()) { //NOTE: if no cb found, give cb of 0
        auto counter_bal = *cb;
        //check(current_time_point().sec_since_epoch() - counter_bal.last_decay >= MIN_LOCK_PERIOD, "cannot get more votes until min lock period is over");
        asset new_cb = (counter_bal.decayable_cb - decay_amount); //subtracting total cb

		//TODO: should mirrorcasting add new_votes to counterbalance? same logically as adding when calling issuetokens

        if (new_cb < asset(0, symbol("VOTE", 4))) {
            new_cb = asset(0, symbol("VOTE", 4));
        }

        new_votes -= new_cb;

        counterbals.modify(cb, same_payer, [&]( auto& a ) {
            a.decayable_cb = new_cb;
        });
    }

    if (new_votes < asset(0, symbol("VOTE", 4))) { //NOTE: can't have less than 0 votes
        new_votes = asset(0, symbol("VOTE", 4));
    }

    balances.modify(b, same_payer, [&]( auto& a ) { //NOTE: allows decayed counterbalances into circulation
        a.tokens = new_votes;
    });

    //update supply
    reg.supply += new_votes;
    registries.modify(r, same_payer, [&]( auto& a ) {
        a.supply = reg.supply;
    });

    //TODO: trail vote update
    //1. subtract balance from supply
    //2. get new max votes
    //3. calc new counterbalance
    //4. subtract decay amount from new cb
    //5. update new cb
    //6. update balance with cb applied
    //7. update supply with new balance

    print("\nMirrorCast: SUCCESS");
}

void trail::castvote(name voter, uint64_t ballot_id, uint16_t direction) {
    require_auth(voter);

    ballots_table ballots(_self, _self.value);
    auto b = ballots.find(ballot_id);
    check(b != ballots.end(), "ballot with given ballot_id doesn't exist");
    auto bal = *b;

    //TODO: factor out get_weight?
    // balances_table balances(_self, _self.value);
    // auto v = balances.find(voter.value);
    // check(v != balances.end(), "voter is not registered");

    bool vote_success = false;

    switch (bal.table_id) {
        case 0 : 
            check(direction >= uint16_t(0) && direction <= uint16_t(2), "Invalid Vote. [0 = NO, 1 = YES, 2 = ABSTAIN]");
            vote_success = vote_for_proposal(voter, ballot_id, bal.reference_id, direction);
            break;
        case 1 : 
            check(true == false, "feature still in development...");
            //vote_success = vote_for_election(voter, ballot_id, bal.reference_id, direction);
            break;
        case 2 : 
            vote_success = vote_for_leaderboard(voter, ballot_id, bal.reference_id, direction);
            break;
    }

}

void trail::deloldvotes(name voter, uint16_t num_to_delete) {
    require_auth(voter);
    check(num_to_delete > uint16_t(0), "must delete greater than 0 receipts");

    votereceipts_table votereceipts(_self, voter.value);
    auto itr = votereceipts.begin();

    while (itr != votereceipts.end() && num_to_delete > 0) {
        if (itr->expiration < env_struct.time_now) { //NOTE: votereceipt has expired
            itr = votereceipts.erase(itr); //NOTE: returns iterator to next element
            num_to_delete--;
        } else {
            itr++;
        }
    }

}

#pragma endregion Voting_Actions


#pragma region Proxy_Registration

//void regproxy();

#pragma endregion Proxy_Registration


#pragma region Proxy_Actions



#pragma endregion Proxy_Actions


#pragma region Ballot_Registration

void trail::regballot(name publisher, uint8_t ballot_type, symbol voting_symbol, uint32_t begin_time, uint32_t end_time, string info_url) {
    require_auth(publisher);
    check(ballot_type >= 0 && ballot_type <= 2, "invalid ballot type"); //NOTE: update valid range as new ballot types are developed
    check(begin_time < end_time, "begin time must be less than end time");

    registries_table registries(_self, _self.value);
    auto r = registries.find(voting_symbol.code().raw());
    check(r != registries.end(), "Token registry with that symbol doesn't exist in Trail");

    uint64_t new_ref_id;

    switch (ballot_type) {
        case 0 : 
            new_ref_id = make_proposal(publisher, voting_symbol, begin_time, end_time, info_url);
            env_struct.totals[0]++;
            break;
        case 1 : 
            check(true == false, "feature still in development...");
            //new_ref_id = make_election(publisher, voting_symbol, begin_time, end_time, info_url);
            //env_struct.totals[1]++;
            break;
        case 2 : 
            new_ref_id = make_leaderboard(publisher, voting_symbol, begin_time, end_time, info_url);
            env_struct.totals[2]++;
            break;
    }

    ballots_table ballots(_self, _self.value);

    uint64_t new_ballot_id = ballots.available_primary_key();

    env_struct.last_ballot_id = new_ballot_id;

    ballots.emplace(publisher, [&]( auto& a ) {
        a.ballot_id = new_ballot_id;
        a.table_id = ballot_type;
        a.reference_id = new_ref_id;
    });

    print("\nBallot ID: ", new_ballot_id);
}

void trail::unregballot(name publisher, uint64_t ballot_id) {
    require_auth(publisher);

    ballots_table ballots(_self, _self.value);
    auto b = ballots.find(ballot_id);
    check(b != ballots.end(), "Ballot Doesn't Exist");
    auto bal = *b;

    bool del_success = false;

    switch (bal.table_id) {
        case 0 : 
            del_success = delete_proposal(bal.reference_id, publisher);
            env_struct.totals[0]--;
            break;
        case 1 : 
            check(true == false, "feature still in development...");
            //del_success = delete_election(bal.reference_id, publisher);
            //env_struct.totals[1]--;
            break;
        case 2 : 
            del_success = delete_leaderboard(bal.reference_id, publisher);
            env_struct.totals[2]--;
            break;
    }

    if (del_success) {
        ballots.erase(b);
    }

    print("\nBallot ID Deleted: ", bal.ballot_id);
}

#pragma endregion Ballot_Registration


#pragma region Ballot_Actions

//TODO: refactor for elections when implemented
void trail::addcandidate(name publisher, uint64_t ballot_id, name new_candidate, string info_link) {
    require_auth(publisher);
    check(is_account(new_candidate), "new candidate is not an account");

    ballots_table ballots(_self, _self.value);
    auto b = ballots.find(ballot_id);
    check(b != ballots.end(), "ballot with given ballot_id doesn't exist");
    auto bal = *b;
    check(bal.table_id == 2, "ballot type doesn't support candidates");

    leaderboards_table leaderboards(_self, _self.value);
    auto l = leaderboards.find(bal.reference_id);
    check(l != leaderboards.end(), "leaderboard doesn't exist");
    auto board = *l;
	check(board.available_seats > 0, "num_seats must be a non-zero number");
    check(board.publisher == publisher, "cannot add candidate to another account's leaderboard");
    check(current_time_point().sec_since_epoch() < board.begin_time , "cannot add candidates once voting has begun");

    auto existing_candidate = std::find_if(board.candidates.begin(), board.candidates.end(), [&new_candidate](const candidate &c) {
        return c.member == new_candidate; 
    });

    check(existing_candidate == board.candidates.end(), "candidate already in leaderboard");

    candidate new_candidate_struct = candidate{
        new_candidate,
        info_link,
        asset(0, board.voting_symbol),
        0
    };

    leaderboards.modify(*l, same_payer, [&]( auto& a ) {
        a.candidates.push_back(new_candidate_struct);
    });

    print("\nAdd Candidate: SUCCESS");
}

//TODO: refactor for elections when implemented
void trail::setallcands(name publisher, uint64_t ballot_id, vector<candidate> new_candidates) {
    require_auth(publisher);

    //TODO: add validations

    ballots_table ballots(_self, _self.value);
    auto b = ballots.find(ballot_id);
    check(b != ballots.end(), "ballot with given ballot_id doesn't exist");
    auto bal = *b;
    check(bal.table_id == 2, "ballot type doesn't support candidates");

    leaderboards_table leaderboards(_self, _self.value);
    auto l = leaderboards.find(bal.reference_id);
    check(l != leaderboards.end(), "leaderboard doesn't exist");
    auto board = *l;
    check(board.publisher == publisher, "cannot change candidates on another account's leaderboard");
    check(current_time_point().sec_since_epoch() < board.begin_time , "cannot change candidates once voting has begun");

    leaderboards.modify(l, same_payer, [&]( auto& a ) {
        a.candidates = new_candidates;
    });

    print("\nSet All Candidates: SUCCESS");
}

//TODO: refactor for elections when implemented
void trail::setallstats(name publisher, uint64_t ballot_id, vector<uint8_t> new_cand_statuses) {
    require_auth(publisher);

    ballots_table ballots(_self, _self.value);
    auto b = ballots.find(ballot_id);
    check(b != ballots.end(), "ballot with given ballot_id doesn't exist");
    auto bal = *b;
    check(bal.table_id == 2, "ballot type doesn't support candidates");

    leaderboards_table leaderboards(_self, _self.value);
    auto l = leaderboards.find(bal.reference_id);
    check(l != leaderboards.end(), "leaderboard doesn't exist");
    auto board = *l;
    check(board.publisher == publisher, "cannot change candidate statuses on another account's leaderboard");
    check(current_time_point().sec_since_epoch() > board.end_time , "cannot change candidate statuses until voting has ended");

    auto new_cands = set_candidate_statuses(board.candidates, new_cand_statuses);

    leaderboards.modify(l, same_payer, [&]( auto& a ) {
        a.candidates = new_cands;
    });

    print("\nSet All Candidate Statuses: SUCCESS");
}

//TODO: refactor for elections when implemented
void trail::rmvcandidate(name publisher, uint64_t ballot_id, name candidate) {
    require_auth(publisher);

    ballots_table ballots(_self, _self.value);
    auto b = ballots.find(ballot_id);
    check(b != ballots.end(), "ballot with given ballot_id doesn't exist");
    auto bal = *b;
    check(bal.table_id == 2, "ballot type doesn't support candidates");

    leaderboards_table leaderboards(_self, _self.value);
    auto l = leaderboards.find(bal.reference_id);
    check(l != leaderboards.end(), "leaderboard doesn't exist");
    auto board = *l;
    check(board.publisher == publisher, "cannot remove candidate from another account's leaderboard");
    check(current_time_point().sec_since_epoch() < board.begin_time, "cannot remove candidates once voting has begun");

    auto new_candidates = board.candidates;
    bool found = false;

    for (auto itr = new_candidates.begin(); itr != new_candidates.end(); itr++) {
        auto cand = *itr;
        if (cand.member == candidate) {
            new_candidates.erase(itr);
            found = true;
            break;
        }
    }

    check(found == true, "candidate not found in leaderboard list");

    leaderboards.modify(*l, same_payer, [&]( auto& a ) {
        a.candidates = new_candidates;
    });

    print("\nRemove Candidate: SUCCESS");
}

void trail::setseats(name publisher, uint64_t ballot_id, uint8_t num_seats) {
    require_auth(publisher);
    check(num_seats > uint8_t(0), "num seats must be greater than 0");

    ballots_table ballots(_self, _self.value);
    auto b = ballots.find(ballot_id);
    check(b != ballots.end(), "ballot with given ballot_id doesn't exist");
    auto bal = *b;

    leaderboards_table leaderboards(_self, _self.value);
    auto l = leaderboards.find(bal.reference_id);
    check(l != leaderboards.end(), "leaderboard doesn't exist");
    auto board = *l;

    check(current_time_point().sec_since_epoch() < board.begin_time, "cannot set seats after voting has begun");
    check(board.publisher == publisher, "cannot set seats for another account's leaderboard");

    leaderboards.modify(l, same_payer, [&]( auto& a ) {
        a.available_seats = num_seats;
    });

    print("\nSet Available Seats: SUCCESS");
}

void trail::closeballot(name publisher, uint64_t ballot_id, uint8_t pass) {
    require_auth(publisher);

    ballots_table ballots(_self, _self.value);
    auto b = ballots.find(ballot_id);
    check(b != ballots.end(), "ballot with given ballot_id doesn't exist");
    auto bal = *b;

    bool close_success = false;

    switch (bal.table_id) {
        case 0 : 
            close_success = close_proposal(bal.reference_id, pass, publisher);
            break;
        case 1 : 
            check(true == false, "feature still in development...");
            //close_success = close_election(bal.reference_id, pass);
            break;
        case 2: 
            close_success = close_leaderboard(bal.reference_id, pass, publisher);
            break;
    }

    print("\nBallot ID Closed: ", bal.ballot_id);
}

void trail::nextcycle(name publisher, uint64_t ballot_id, uint32_t new_begin_time, uint32_t new_end_time) {
    require_auth(publisher);
    check(new_begin_time < new_end_time, "begin time must be less than end time");

    ballots_table ballots(_self, _self.value);
    auto b = ballots.find(ballot_id);
    check(b != ballots.end(), "Ballot Doesn't Exist");
    auto bal = *b;

    //TODO: support cycles for other ballot types?
    //NOTE: currently only supports proposals
    check(bal.table_id == 0, "ballot type doesn't support cycles");

    proposals_table proposals(_self, _self.value);
    auto p = proposals.find(bal.reference_id);
    check(p != proposals.end(), "proposal doesn't exist");
    auto prop = *p;

	check(env_struct.time_now < prop.begin_time || env_struct.time_now > prop.end_time, 
		"a proposal can only be cycled before begin_time or after end_time");

    auto sym = prop.no_count.symbol; //NOTE: uses same voting symbol as before

    proposals.modify(p, same_payer, [&]( auto& a ) {
        a.no_count = asset(0, sym);
        a.yes_count = asset(0, sym);
        a.abstain_count = asset(0, sym);
        a.unique_voters = uint32_t(0);
        a.begin_time = new_begin_time;
        a.end_time = new_end_time;
        a.cycle_count += 1;
        a.status = 0;
    });

    print("\nNext Cycle: SUCCESS");
}

#pragma endregion Ballot_Actions`

#pragma region Helper_Functions

uint64_t trail::make_proposal(name publisher, symbol voting_symbol, uint32_t begin_time, uint32_t end_time, string info_url) {

    proposals_table proposals(_self, _self.value);
    uint64_t new_prop_id = proposals.available_primary_key();

    proposals.emplace(publisher, [&]( auto& a ) {
        a.prop_id = new_prop_id;
        a.publisher = publisher;
        a.info_url = info_url;
        a.no_count = asset(0, voting_symbol);
        a.yes_count = asset(0, voting_symbol);
        a.abstain_count = asset(0, voting_symbol);
        a.unique_voters = uint32_t(0);
        a.begin_time = begin_time;
        a.end_time = end_time;
        a.cycle_count = 0;
        a.status = 0;
    });

    print("\nProposal Creation: SUCCESS");

    return new_prop_id;
}

bool trail::delete_proposal(uint64_t prop_id, name publisher) {
    proposals_table proposals(_self, _self.value);
    auto p = proposals.find(prop_id);
    check(p != proposals.end(), "proposal doesn't exist");
    auto prop = *p;

    check(current_time_point().sec_since_epoch() < prop.begin_time, "cannot delete proposal once voting has begun");
	check(prop.publisher == publisher, "cannot delete another account's proposal");
    check(prop.cycle_count == 0, "proposal must be on initial cycle to delete");
    //TODO: check that status > 0?

    proposals.erase(p);

    print("\nProposal Deletion: SUCCESS");

    return true;
}

bool trail::vote_for_proposal(name voter, uint64_t ballot_id, uint64_t prop_id, uint16_t direction) {
    
    proposals_table proposals(_self, _self.value);
    auto p = proposals.find(prop_id);
    check(p != proposals.end(), "proposal doesn't exist");
    auto prop = *p;

	registries_table registries(_self, _self.value);
    auto r = registries.find(prop.no_count.symbol.code().raw());
    check(r != registries.end(), "Token Registry with that symbol doesn't exist");
    auto reg = *r;

    check(env_struct.time_now >= prop.begin_time && env_struct.time_now <= prop.end_time, "ballot voting window not open");

    votereceipts_table votereceipts(_self, voter.value);
    auto vr_itr = votereceipts.find(ballot_id);
    
    uint32_t new_voter = 1;
    asset vote_weight = get_vote_weight(voter, prop.no_count.symbol);
    check(vote_weight > asset(0, prop.no_count.symbol), "vote weight must be greater than 0"); //TODO: add to get_vote_weight?

    if (vr_itr == votereceipts.end()) { //NOTE: voter hasn't voted on ballot before

        vector<uint16_t> new_directions;
        new_directions.emplace_back(direction);

        votereceipts.emplace(voter, [&]( auto& a ){
            a.ballot_id = ballot_id;
            a.directions = new_directions;
            a.weight = vote_weight;
            a.expiration = prop.end_time;
        });

        print("\nVote Cast: SUCCESS");
        
    } else { //NOTE: vote for ballot_id already exists
        auto vr = *vr_itr;

        if (vr.expiration == prop.end_time) { //NOTE: vote is for same cycle

			check(reg.settings.is_recastable, "token registry disallows vote recasting");

            if (vr.directions[0] == direction) {
                vote_weight -= vr.weight;
            } else {
                switch (vr.directions[0]) { //NOTE: remove old vote weight from proposal
                    case 0 : prop.no_count -= vr.weight; break;
                    case 1 : prop.yes_count -= vr.weight; break;
                    case 2 : prop.abstain_count -= vr.weight; break;
                }

                vr.directions[0] = direction;

                votereceipts.modify(vr_itr, same_payer, [&]( auto& a ) {
                    a.directions = vr.directions;
                    a.weight = vote_weight;
                });
            }
            
            new_voter = 0;
            print("\nVote Recast: SUCCESS");
        } else if (vr.expiration < prop.end_time) { //NOTE: vote is for new cycle on same proposal
            
            vr.directions[0] = direction;

            votereceipts.modify(vr_itr, same_payer, [&]( auto& a ) {
                a.directions = vr.directions;
                a.weight = vote_weight;
                a.expiration = prop.end_time;
            });

            print("\nVote Cast For New Cycle: SUCCESS");
        }
    }

    switch (direction) { //NOTE: update proposal with new weight
        case 0 : prop.no_count += vote_weight; break;
        case 1 : prop.yes_count += vote_weight; break;
        case 2 : prop.abstain_count += vote_weight; break;
    }

    proposals.modify(p, same_payer, [&]( auto& a ) {
        a.no_count = prop.no_count;
        a.yes_count = prop.yes_count;
        a.abstain_count = prop.abstain_count;
        a.unique_voters += new_voter;
    });

    return true;
}

bool trail::close_proposal(uint64_t prop_id, uint8_t pass, name publisher) {
    proposals_table proposals(_self, _self.value);
    auto p = proposals.find(prop_id);
    check(p != proposals.end(), "proposal doesn't exist");
    auto prop = *p;

    check(current_time_point().sec_since_epoch() > prop.end_time, "can't close proposal while voting is still open");
	check(prop.publisher == publisher, "cannot close another account's proposal");

    proposals.modify(p, same_payer, [&]( auto& a ) {
        a.status = pass;
    });

    return true;
}


uint64_t trail::make_election(name publisher, symbol voting_symbol, uint32_t begin_time, uint32_t end_time, string info_url) {
    elections_table elections(_self, _self.value);

    uint64_t new_elec_id = elections.available_primary_key();
    vector<candidate> empty_candidate_list;

    elections.emplace(publisher, [&]( auto& a ) {
        a.election_id = new_elec_id;
        a.publisher = publisher;
        a.info_url = info_url;
        a.candidates = empty_candidate_list;
        a.unique_voters = 0;
        a.voting_symbol = voting_symbol;
        a.begin_time = begin_time;
        a.end_time = end_time;
    });

    print("\nElection Creation: SUCCESS");

    return new_elec_id;
}

bool trail::delete_election(uint64_t elec_id, name publisher) {
    elections_table elections(_self, _self.value);
    auto e = elections.find(elec_id);
    check(e != elections.end(), "election doesn't exist");
    auto elec = *e;

    check(current_time_point().sec_since_epoch() < elec.begin_time, "cannot delete election once voting has begun");
    check(elec.publisher == publisher, "cannot delete another account's election");

    elections.erase(e);

    print("\nElection Deletion: SUCCESS");

    return true;
}

bool close_election(uint64_t elec_id, uint8_t pass, name publisher) {
    //TODO: implement
    return true;
}

bool delete_election(uint64_t elec_id, name publisher) {
    //TODO: implement
    return true;
}


uint64_t trail::make_leaderboard(name publisher, symbol voting_symbol, uint32_t begin_time, uint32_t end_time, string info_url) {
    require_auth(publisher);

    leaderboards_table leaderboards(_self, _self.value);
    uint64_t new_board_id = leaderboards.available_primary_key();

    vector<candidate> candidates;

    leaderboards.emplace(publisher, [&]( auto& a ) {
        a.board_id = new_board_id;
        a.publisher = publisher;
        a.info_url = info_url;
        a.candidates = candidates;
        a.unique_voters = uint32_t(0);
        a.voting_symbol = voting_symbol;
        a.available_seats = 0;
        a.begin_time = begin_time;
        a.end_time = end_time;
        a.status = 0;
    });

    print("\nLeaderboard Creation: SUCCESS");

    return new_board_id;
}

bool trail::delete_leaderboard(uint64_t board_id, name publisher) {
    leaderboards_table leaderboards(_self, _self.value);
    auto b = leaderboards.find(board_id);
    check(b != leaderboards.end(), "leaderboard doesn't exist");
    auto board = *b;

    check(current_time_point().sec_since_epoch() < board.begin_time, "cannot delete leaderboard once voting has begun");
    check(board.publisher == publisher, "cannot delete another account's leaderboard");

    leaderboards.erase(b);

    print("\nLeaderboard Deletion: SUCCESS");

    return true;
}

bool trail::vote_for_leaderboard(name voter, uint64_t ballot_id, uint64_t board_id, uint16_t direction) {
    leaderboards_table leaderboards(_self, _self.value);
    auto b = leaderboards.find(board_id);
    check(b != leaderboards.end(), "leaderboard doesn't exist");
    auto board = *b;
	print("\nboard.candidates.size(): ", board.candidates.size());
	check(direction < board.candidates.size(), "direction must map to an existing candidate in the leaderboard struct");
    check(env_struct.time_now >= board.begin_time && env_struct.time_now <= board.end_time, "ballot voting window not open");

    votereceipts_table votereceipts(_self, voter.value);
    auto vr_itr = votereceipts.find(ballot_id);

	registries_table registries(_self, _self.value);
	auto r = registries.find(board.voting_symbol.code().raw());
	check(r != registries.end(), "token registry does not exist");
    
    uint32_t new_voter = 1;
    asset vote_weight = get_vote_weight(voter, board.voting_symbol);
	print("\nvote weight amount: ", vote_weight);
	check(vote_weight > asset(0, board.voting_symbol), "vote weight must be greater than 0");

    if (vr_itr == votereceipts.end()) { //NOTE: voter hasn't voted on ballot before

        vector<uint16_t> new_directions;
        new_directions.emplace_back(direction);

        votereceipts.emplace(voter, [&]( auto& a ){
            a.ballot_id = ballot_id;
            a.directions = new_directions;
            a.weight = vote_weight;
            a.expiration = board.end_time;
        });

        print("\nVote Cast: SUCCESS");
        
    } else { //NOTE: vote for ballot_id already exists
        auto vr = *vr_itr;
		auto reg = *r;

        if (vr.expiration == board.end_time && !has_direction(direction, vr.directions)) { //NOTE: hasn't voted for candidate before
            new_voter = 0;
            vr.directions.emplace_back(direction);

            votereceipts.modify(vr_itr, same_payer, [&]( auto& a ) {
                a.directions = vr.directions;
            });

            print("\nVote Recast: SUCCESS");

        } else if (vr.expiration == board.end_time && has_direction(direction, vr.directions)) { //NOTE: vote already exists for candidate (recasting)
			check(reg.settings.is_recastable, "token registry disallows vote recasting");
            check(true == false, "Feature currently disabled"); //NOTE: temp fix
            new_voter = 0;
		}
        
    }

    //NOTE: update leaderboard with new weight
    board.candidates[direction].votes += vote_weight;

    leaderboards.modify(b, same_payer, [&]( auto& a ) {
        a.candidates = board.candidates;
        a.unique_voters += new_voter;
    });

    return true;
}

bool trail::close_leaderboard(uint64_t board_id, uint8_t pass, name publisher) {
    leaderboards_table leaderboards(_self, _self.value);
    auto b = leaderboards.find(board_id);
    check(b != leaderboards.end(), "leaderboard doesn't exist");
    auto board = *b;

    check(current_time_point().sec_since_epoch() > board.end_time, "cannot close leaderboard while voting is still open");
    check(board.publisher == publisher, "cannot close another account's leaderboard");

    leaderboards.modify(b, same_payer, [&]( auto& a ) {
        a.status = pass;
    });

    return true;
}

asset trail::get_vote_weight(name voter, symbol voting_symbol) {

    balances_table balances(_self, voting_symbol.code().raw());
    auto b = balances.find(voter.value);

    if (b == balances.end()) { //NOTE: no balance found, returning 0
		//print("\n no balance object found!");
        return asset(0, voting_symbol);
    } else {
        auto bal = *b;
		//print("\n bal.tokens: ", bal.tokens);
        return bal.tokens;
    }
}

bool trail::has_direction(uint16_t direction, vector<uint16_t> direction_list) {

    for (uint16_t item : direction_list) {
        if (item == direction) {
            return true;
        }
    }

    return false;
}

vector<candidate> trail::set_candidate_statuses(vector<candidate> candidate_list, vector<uint8_t> new_status_list) {
    check(candidate_list.size() == new_status_list.size(), "status list does not correctly map to candidate list");

    for (int idx = 0; idx < candidate_list.size(); idx++) {
        candidate_list[idx].status = new_status_list[idx];
    }

    return candidate_list;
}

#pragma endregion Helper_Functions


#pragma region Reactions

void trail::transfer_handler(const name &from, const name &to, const asset &quantity, const string &memo) {
    update_from_cb(from, asset(quantity.amount, symbol("VOTE", 4)));
    update_to_cb(to, asset(quantity.amount, symbol("VOTE", 4)));
}

void trail::update_from_cb(const name &from, const asset &amount) {
    counterbalances_table fromcbs(_self, amount.symbol.code().raw());
    auto cb_itr = fromcbs.find(from.value);
    
    if (cb_itr == fromcbs.end()) {
		uint32_t new_now = current_time_point().sec_since_epoch();
        fromcbs.emplace(_self, [&]( auto& a ){ //TODO: change ram payer to user? may prevent TLOS transfers
            a.owner = from;
            a.decayable_cb = asset(0, symbol("VOTE", 4));
			a.persistent_cb = asset(0, symbol("VOTE", 4));
            a.last_decay = new_now;
        });
    } else {
        auto from_cb = *cb_itr;
        asset new_cb = from_cb.decayable_cb - amount;

        if (new_cb < asset(0, symbol("VOTE", 4))) {
            new_cb = asset(0, symbol("VOTE", 4));
        }

        fromcbs.modify(cb_itr, same_payer, [&]( auto& a ) {
            a.decayable_cb = new_cb;
        });
    }
}

void trail::update_to_cb(const name &to, const asset &amount) {
    counterbalances_table tocbs(_self, amount.symbol.code().raw());
    auto cb_itr = tocbs.find(to.value);

    if (cb_itr == tocbs.end()) {
		print("\ntime_now: ", env_struct.time_now);
        tocbs.emplace(_self, [&]( auto& a ){ //TODO: change ram payer to user? may prevent TLOS transfers
            a.owner = to;
            a.decayable_cb = asset(amount.amount, symbol("VOTE", 4));
			a.persistent_cb = asset(0, symbol("VOTE", 4));
            a.last_decay = env_struct.time_now;
        });
    } else {
        auto to_cb = *cb_itr;
        asset new_cb = to_cb.decayable_cb + amount;

        tocbs.modify(cb_itr, same_payer, [&]( auto& a ) {
            a.decayable_cb = new_cb;
        });
    }
}

asset trail::get_decay_amount(name voter, symbol token_symbol, uint32_t decay_rate) {
    counterbalances_table counterbals(_self, token_symbol.code().raw());
    auto cb_itr = counterbals.find(voter.value);

    uint32_t time_delta;

    int prec = token_symbol.precision();
    int val = 1;

    for (int i = prec; i > 0; i--) {
        val *= 10;
    }

    if (cb_itr != counterbals.end()) {
        auto cb = *cb_itr;
        time_delta = env_struct.time_now - cb.last_decay;
        return asset(int64_t(time_delta / decay_rate) * val, token_symbol);
    }

    return asset(0, token_symbol);
}

#pragma endregion Reactionsx
