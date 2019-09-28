/**
 * This contract implements the TIP-5 Single Token Interface.
 * 
 * @author Craig Branscom
 */

#include <token.registry.hpp>

registry::registry(name self, name code, datastream<const char*> ds) : contract(self, code, ds), _config(self, self.value) {
    if (!_config.exists()) {

        config = tokenconfig{
            get_self(), //publisher
            TOKEN_NAME, //token_name
            INITIAL_MAX_SUPPLY, //max_supply
            INITIAL_SUPPLY //supply
        };

        _config.set(config, get_self());
    } else {
        config = _config.get();
    }
}

registry::~registry() {
    if (_config.exists()) {
        _config.set(config, config.publisher);
    }
}

void registry::mint(name recipient, asset tokens) {
    require_auth(config.publisher);
    check(is_account(recipient), "recipient account does not exist");
    check(config.supply + tokens <= config.max_supply, "minting would exceed allowed maximum supply");

    add_balance(recipient, tokens, config.publisher);

    config.supply = (config.supply + tokens);
}

void registry::transfer(name sender, name recipient, asset tokens) {
    require_auth(sender);
    check(is_account(recipient), "recipient account does not exist");
    check(sender != recipient, "cannot transfer to self");
    check(tokens.is_valid(), "invalid token");
    check(tokens.amount > 0, "must transfer positive quantity");
    
    add_balance(recipient, tokens, sender);
    sub_balance(sender, tokens);
}

void registry::allot(name sender, name recipient, asset tokens) {
    require_auth(sender);
    check(is_account(recipient), "recipient account does not exist");
    check(sender != recipient, "cannot allot tokens to self");
    check(tokens.is_valid(), "invalid token");
    check(tokens.amount > 0, "must allot positive quantity");

    sub_balance(sender, tokens);
    add_allot(sender, recipient, tokens, sender);
}

void registry::unallot(name sender, name recipient, asset tokens) {
    require_auth(sender);
    check(tokens.is_valid(), "invalid token");
    check(tokens.amount > 0, "must allot positive quantity");

    sub_allot(sender, recipient, tokens);
    add_balance(sender, tokens, sender);
}

void registry::claimallot(name sender, name recipient, asset tokens) {
    require_auth(recipient);
    check(is_account(sender), "sender account does not exist");
    check(tokens.is_valid(), "invalid token");
    check(tokens.amount > 0, "must transfer positive quantity");
    
    sub_allot(sender, recipient, tokens);
    add_balance(recipient, tokens, recipient);
}

void registry::createwallet(name recipient) {
    require_auth(recipient);

    balances_table balances(config.publisher, recipient.value);
    auto itr = balances.find(recipient.value);

    check(itr == balances.end(), "Wallet already exists for given account");

    balances.emplace(recipient, [&]( auto& a ){
        a.owner = recipient;
        a.tokens = asset(int64_t(0), config.max_supply.symbol);
    });
}

void registry::deletewallet(name owner) {
    require_auth(owner);

    balances_table balances(config.publisher, owner.value);
    const auto& b = balances.get(owner.value, "Given account does not have a wallet");

    check(b.tokens.amount == 0, "Cannot delete wallet unless balance is zero");

    balances.erase(b);
}

void registry::add_balance(name recipient, asset tokens, name payer) {
    balances_table balances(config.publisher, recipient.value);
    const auto& b = balances.get(recipient.value, "No wallet found for recipient");

    balances.modify(b, same_payer, [&]( auto& a ) {
        a.tokens += tokens;
    });
}

void registry::sub_balance(name owner, asset tokens) {
    balances_table balances(config.publisher, owner.value);
    const auto& b = balances.get(owner.value, "transaction would overdraw balance");

    balances.modify(b, same_payer, [&]( auto& a ) {
        a.tokens -= tokens;
    });
}

void registry::add_allot(name sender, name recipient, asset tokens, name payer) {
    
    allotments_table allotments(config.publisher, sender.value);
    auto itr = allotments.find(recipient.value);

    if(itr == allotments.end() ) {
        allotments.emplace(payer, [&]( auto& a ){
            a.recipient = recipient;
            a.sender = sender;
            a.tokens = tokens;
        });
   } else {
        allotments.modify(itr, same_payer, [&]( auto& a ) {
            a.tokens += tokens;
        });
   }
}

void registry::sub_allot(name owner, name recipient, asset tokens) {
    allotments_table allotments(config.publisher, owner.value);
    auto itr = allotments.find(recipient.value);
    const auto& al = allotments.get(recipient.value, "transaction would overdraw balance");

    if(al.tokens.amount == tokens.amount ) {
        allotments.erase(itr);
    } else {
        allotments.modify(itr, same_payer, [&]( auto& a ) {
            a.tokens -= tokens;
        });
    }
}