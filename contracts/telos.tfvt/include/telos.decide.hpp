#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/symbol.hpp>
#include <eosio/name.hpp>

using namespace eosio;
using namespace std;

#define TELOS_DECIDE_N name("telos.decide")

TABLE ballot {
    name ballot_name;
    name category; //proposal, referendum, election, poll, leaderboard
    name publisher;
    name status; //setup, voting, closed, cancelled, archived

    string title; //markdown
    string description; //markdown
    string content; //IPFS link to content or markdown

    symbol treasury_symbol; //treasury used for counting votes
    name voting_method; //1acct1vote, 1tokennvote, 1token1vote, 1tsquare1v, quadratic
    uint8_t min_options; //minimum options per voter
    uint8_t max_options; //maximum options per voter
    map<name, asset> options; //option name -> total weighted votes

    uint32_t total_voters; //number of voters who voted on ballot
    uint32_t total_delegates; //number of delegates who voted on ballot
    asset total_raw_weight; //total raw weight cast on ballot
    uint32_t cleaned_count; //number of expired vote receipts cleaned
    map<name, bool> settings; //setting name -> on/off
    
    time_point_sec begin_time; //time that voting begins
    time_point_sec end_time; //time that voting closes

    uint64_t primary_key() const { return ballot_name.value; }
    uint64_t by_category() const { return category.value; }
    uint64_t by_status() const { return status.value; }
    uint64_t by_symbol() const { return treasury_symbol.code().raw(); }
    uint64_t by_end_time() const { return static_cast<uint64_t>(end_time.utc_seconds); }
    
    EOSLIB_SERIALIZE(ballot, 
        (ballot_name)(category)(publisher)(status)
        (title)(description)(content)
        (treasury_symbol)(voting_method)(min_options)(max_options)(options)
        (total_voters)(total_delegates)(total_raw_weight)(cleaned_count)(settings)
        (begin_time)(end_time))
};

typedef multi_index<name("ballots"), ballot,
    indexed_by<name("bycategory"), const_mem_fun<ballot, uint64_t, &ballot::by_category>>,
    indexed_by<name("bystatus"), const_mem_fun<ballot, uint64_t, &ballot::by_status>>,
    indexed_by<name("bysymbol"), const_mem_fun<ballot, uint64_t, &ballot::by_symbol>>,
    indexed_by<name("byendtime"), const_mem_fun<ballot, uint64_t, &ballot::by_end_time>>
> ballots_table;
