#include <eosio.system/eosio.system.hpp>
#include <eosio.token/eosio.token.hpp>

#include <eosio/crypto.hpp>
#include <eosio/dispatcher.hpp>

// TELOS BEGIN
#include <eosio.evm/tables.hpp>
#include <eosio/crypto_ext.hpp>
#include <eosio.evm/util.hpp>
#include <intx/intx.hpp>
#include <intx/base.hpp>
#include <rlp/rlp.hpp>
// TELOS END
#include <cmath>

namespace eosiosystem {

   using eosio::current_time_point;
   using eosio::token;

   double get_continuous_rate(int64_t annual_rate) {
      return std::log1p(double(annual_rate)/double(100*inflation_precision));
   }

   system_contract::system_contract( name s, name code, datastream<const char*> ds )
   :native(s,code,ds),
    _voters(get_self(), get_self().value),
    _producers(get_self(), get_self().value),
    _producers2(get_self(), get_self().value),
    _global(get_self(), get_self().value),
    _global2(get_self(), get_self().value),
    _global3(get_self(), get_self().value),
    _global4(get_self(), get_self().value),
    _rammarket(get_self(), get_self().value),
    _rexpool(get_self(), get_self().value),
    _rexretpool(get_self(), get_self().value),
    _rexretbuckets(get_self(), get_self().value),
    _rexfunds(get_self(), get_self().value),
    _rexbalance(get_self(), get_self().value),
    _rexorders(get_self(), get_self().value),
    // TELOS BEGIN
    _schedule_metrics(_self, _self.value),
    _rotation(_self, _self.value),
    _payrate(_self, _self.value),
    _payments(_self, _self.value),
    _evm_votes(_self, _self.value),
    _voting_config(_self, _self.value)
    // TELOS END
   {
      _gstate  = _global.exists() ? _global.get() : get_default_parameters();
      _gstate2 = _global2.exists() ? _global2.get() : eosio_global_state2{};
      _gstate3 = _global3.exists() ? _global3.get() : eosio_global_state3{};
      _gstate4 = _global4.exists() ? _global4.get() : get_default_inflation_parameters();
      // TELOS BEGIN
      _gschedule_metrics = _schedule_metrics.get_or_create(_self, schedule_metrics_state{ name(0), 0, std::vector<producer_metric>() });
      _grotation = _rotation.get_or_create(_self, rotation_state{ name(0), name(0), 21, 75, block_timestamp(), block_timestamp() });
      _gpayrate = _payrate.get_or_create(_self, payrates{ max_bpay_rate, max_worker_monthly_amount });
      _gvoting_config = _voting_config.get_or_create(_self, votingconfig{ eosio::checksum160(), 0, 0, 0 });
      // TELOS END
   }

   eosio_global_state system_contract::get_default_parameters() {
      eosio_global_state dp;
      get_blockchain_parameters(dp);
      return dp;
   }

   eosio_global_state4 system_contract::get_default_inflation_parameters() {
      eosio_global_state4 gs4;
      gs4.continuous_rate      = get_continuous_rate(default_annual_rate);
      gs4.inflation_pay_factor = default_inflation_pay_factor;
      gs4.votepay_factor       = default_votepay_factor;
      return gs4;
   }

   symbol system_contract::core_symbol()const {
      const static auto sym = get_core_symbol( _rammarket );
      return sym;
   }

   system_contract::~system_contract() {
      _global.set( _gstate, get_self() );
      _global2.set( _gstate2, get_self() );
      _global3.set( _gstate3, get_self() );
      _global4.set( _gstate4, get_self() );
      // TELOS BEGIN
      _schedule_metrics.set(_gschedule_metrics, _self);
      _rotation.set(_grotation, _self);
      _payrate.set(_gpayrate, _self);
      _voting_config.set(_gvoting_config, _self);
      // TELOS END
   }

   void system_contract::setram( uint64_t max_ram_size ) {
      require_auth( get_self() );

      check( _gstate.max_ram_size < max_ram_size, "ram may only be increased" ); /// decreasing ram might result market maker issues
      check( max_ram_size < 1024ll*1024*1024*1024*1024, "ram size is unrealistic" );
      check( max_ram_size > _gstate.total_ram_bytes_reserved, "attempt to set max below reserved" );

      auto delta = int64_t(max_ram_size) - int64_t(_gstate.max_ram_size);
      auto itr = _rammarket.find(ramcore_symbol.raw());

      /**
       *  Increase the amount of ram for sale based upon the change in max ram size.
       */
      _rammarket.modify( itr, same_payer, [&]( auto& m ) {
         m.base.balance.amount += delta;
      });

      _gstate.max_ram_size = max_ram_size;
   }

   void system_contract::update_ram_supply() {
      auto cbt = eosio::current_block_time();

      if( cbt <= _gstate2.last_ram_increase ) return;

      auto itr = _rammarket.find(ramcore_symbol.raw());
      auto new_ram = (cbt.slot - _gstate2.last_ram_increase.slot)*_gstate2.new_ram_per_block;
      _gstate.max_ram_size += new_ram;

      /**
       *  Increase the amount of ram for sale based upon the change in max ram size.
       */
      _rammarket.modify( itr, same_payer, [&]( auto& m ) {
         m.base.balance.amount += new_ram;
      });
      _gstate2.last_ram_increase = cbt;
   }

   void system_contract::setramrate( uint16_t bytes_per_block ) {
      require_auth( get_self() );

      update_ram_supply();
      _gstate2.new_ram_per_block = bytes_per_block;
   }

#ifdef SYSTEM_BLOCKCHAIN_PARAMETERS
   extern "C" [[eosio::wasm_import]] void set_parameters_packed(const void*, size_t);
#endif

   void system_contract::setparams( const blockchain_parameters_t& params ) {
      require_auth( get_self() );
      (eosio::blockchain_parameters&)(_gstate) = params;
      check( 3 <= _gstate.max_authority_depth, "max_authority_depth should be at least 3" );
#ifndef SYSTEM_BLOCKCHAIN_PARAMETERS
      set_blockchain_parameters( params );
#else
      constexpr size_t param_count = 18;
      // an upper bound on the serialized size
      char buf[1 + sizeof(params) + param_count];
      datastream<char*> stream(buf, sizeof(buf));

      stream << uint8_t(17);
      stream << uint8_t(0) << params.max_block_net_usage
             << uint8_t(1) << params.target_block_net_usage_pct
             << uint8_t(2) << params.max_transaction_net_usage
             << uint8_t(3) << params.base_per_transaction_net_usage
             << uint8_t(4) << params.net_usage_leeway
             << uint8_t(5) << params.context_free_discount_net_usage_num
             << uint8_t(6) << params.context_free_discount_net_usage_den

             << uint8_t(7) << params.max_block_cpu_usage
             << uint8_t(8) << params.target_block_cpu_usage_pct
             << uint8_t(9) << params.max_transaction_cpu_usage
             << uint8_t(10) << params.min_transaction_cpu_usage

             << uint8_t(11) << params.max_transaction_lifetime
             << uint8_t(12) << params.deferred_trx_expiration_window
             << uint8_t(13) << params.max_transaction_delay
             << uint8_t(14) << params.max_inline_action_size
             << uint8_t(15) << params.max_inline_action_depth
             << uint8_t(16) << params.max_authority_depth;
      if(params.max_action_return_value_size)
      {
         stream << uint8_t(17) << params.max_action_return_value_size.value();
         ++buf[0];
      }

      set_parameters_packed(buf, stream.tellp());
#endif
   }

#ifdef SYSTEM_CONFIGURABLE_WASM_LIMITS

   // The limits on contract WebAssembly modules
   struct wasm_parameters
   {
      uint32_t max_mutable_global_bytes;
      uint32_t max_table_elements;
      uint32_t max_section_elements;
      uint32_t max_linear_memory_init;
      uint32_t max_func_local_bytes;
      uint32_t max_nested_structures;
      uint32_t max_symbol_bytes;
      uint32_t max_module_bytes;
      uint32_t max_code_bytes;
      uint32_t max_pages;
      uint32_t max_call_depth;
   };

   static constexpr wasm_parameters default_limits = {
       .max_mutable_global_bytes = 1024,
       .max_table_elements = 1024,
       .max_section_elements = 8192,
       .max_linear_memory_init = 64*1024,
       .max_func_local_bytes = 8192,
       .max_nested_structures = 1024,
       .max_symbol_bytes = 8192,
       .max_module_bytes = 20*1024*1024,
       .max_code_bytes = 20*1024*1024,
       .max_pages = 528,
       .max_call_depth = 251
   };

   static constexpr wasm_parameters high_limits = {
       .max_mutable_global_bytes = 8192,
       .max_table_elements = 8192,
       .max_section_elements = 8192,
       .max_linear_memory_init = 16*64*1024,
       .max_func_local_bytes = 8192,
       .max_nested_structures = 1024,
       .max_symbol_bytes = 8192,
       .max_module_bytes = 20*1024*1024,
       .max_code_bytes = 20*1024*1024,
       .max_pages = 528,
       .max_call_depth = 1024
   };

   extern "C" [[eosio::wasm_import]] void set_wasm_parameters_packed( const void*, size_t );

   void set_wasm_parameters( const wasm_parameters& params )
   {
      char buf[sizeof(uint32_t) + sizeof(params)] = {};
      memcpy(buf + sizeof(uint32_t), &params, sizeof(params));
      set_wasm_parameters_packed( buf, sizeof(buf) );
   }

   void system_contract::wasmcfg( const name& settings )
   {
      require_auth( get_self() );
      if( settings == "default"_n || settings == "low"_n )
      {
         set_wasm_parameters( default_limits );
      }
      else if( settings == "high"_n )
      {
         set_wasm_parameters( high_limits );
      }
      else
      {
         check(false, "Unkown configuration");
      }
   }

#endif

   void system_contract::setpriv( const name& account, uint8_t ispriv ) {
      require_auth( get_self() );
      set_privileged( account, ispriv );
   }

   void system_contract::setalimits( const name& account, int64_t ram, int64_t net, int64_t cpu ) {
      require_auth( get_self() );

      user_resources_table userres( get_self(), account.value );
      auto ritr = userres.find( account.value );
      check( ritr == userres.end(), "only supports unlimited accounts" );

      auto vitr = _voters.find( account.value );
      if( vitr != _voters.end() ) {
         bool ram_managed = has_field( vitr->flags1, voter_info::flags1_fields::ram_managed );
         bool net_managed = has_field( vitr->flags1, voter_info::flags1_fields::net_managed );
         bool cpu_managed = has_field( vitr->flags1, voter_info::flags1_fields::cpu_managed );
         check( !(ram_managed || net_managed || cpu_managed), "cannot use setalimits on an account with managed resources" );
      }

      set_resource_limits( account, ram, net, cpu );
   }

   void system_contract::setacctram( const name& account, const std::optional<int64_t>& ram_bytes ) {
      require_auth( get_self() );

      int64_t current_ram, current_net, current_cpu;
      get_resource_limits( account, current_ram, current_net, current_cpu );

      int64_t ram = 0;

      if( !ram_bytes ) {
         auto vitr = _voters.find( account.value );
         check( vitr != _voters.end() && has_field( vitr->flags1, voter_info::flags1_fields::ram_managed ),
                "RAM of account is already unmanaged" );

         user_resources_table userres( get_self(), account.value );
         auto ritr = userres.find( account.value );

         ram = ram_gift_bytes;
         if( ritr != userres.end() ) {
            ram += ritr->ram_bytes;
         }

         _voters.modify( vitr, same_payer, [&]( auto& v ) {
            v.flags1 = set_field( v.flags1, voter_info::flags1_fields::ram_managed, false );
         });
      } else {
         check( *ram_bytes >= 0, "not allowed to set RAM limit to unlimited" );

         auto vitr = _voters.find( account.value );
         if ( vitr != _voters.end() ) {
            _voters.modify( vitr, same_payer, [&]( auto& v ) {
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::ram_managed, true );
            });
         } else {
            _voters.emplace( account, [&]( auto& v ) {
               v.owner  = account;
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::ram_managed, true );
            });
         }

         ram = *ram_bytes;
      }

      set_resource_limits( account, ram, current_net, current_cpu );
   }

   void system_contract::setacctnet( const name& account, const std::optional<int64_t>& net_weight ) {
      require_auth( get_self() );

      int64_t current_ram, current_net, current_cpu;
      get_resource_limits( account, current_ram, current_net, current_cpu );

      int64_t net = 0;

      if( !net_weight ) {
         auto vitr = _voters.find( account.value );
         check( vitr != _voters.end() && has_field( vitr->flags1, voter_info::flags1_fields::net_managed ),
                "Network bandwidth of account is already unmanaged" );

         user_resources_table userres( get_self(), account.value );
         auto ritr = userres.find( account.value );

         if( ritr != userres.end() ) {
            net = ritr->net_weight.amount;
         }

         _voters.modify( vitr, same_payer, [&]( auto& v ) {
            v.flags1 = set_field( v.flags1, voter_info::flags1_fields::net_managed, false );
         });
      } else {
         check( *net_weight >= -1, "invalid value for net_weight" );

         auto vitr = _voters.find( account.value );
         if ( vitr != _voters.end() ) {
            _voters.modify( vitr, same_payer, [&]( auto& v ) {
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::net_managed, true );
            });
         } else {
            _voters.emplace( account, [&]( auto& v ) {
               v.owner  = account;
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::net_managed, true );
            });
         }

         net = *net_weight;
      }

      set_resource_limits( account, current_ram, net, current_cpu );
   }

   void system_contract::setacctcpu( const name& account, const std::optional<int64_t>& cpu_weight ) {
      require_auth( get_self() );

      int64_t current_ram, current_net, current_cpu;
      get_resource_limits( account, current_ram, current_net, current_cpu );

      int64_t cpu = 0;

      if( !cpu_weight ) {
         auto vitr = _voters.find( account.value );
         check( vitr != _voters.end() && has_field( vitr->flags1, voter_info::flags1_fields::cpu_managed ),
                "CPU bandwidth of account is already unmanaged" );

         user_resources_table userres( get_self(), account.value );
         auto ritr = userres.find( account.value );

         if( ritr != userres.end() ) {
            cpu = ritr->cpu_weight.amount;
         }

         _voters.modify( vitr, same_payer, [&]( auto& v ) {
            v.flags1 = set_field( v.flags1, voter_info::flags1_fields::cpu_managed, false );
         });
      } else {
         check( *cpu_weight >= -1, "invalid value for cpu_weight" );

         auto vitr = _voters.find( account.value );
         if ( vitr != _voters.end() ) {
            _voters.modify( vitr, same_payer, [&]( auto& v ) {
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::cpu_managed, true );
            });
         } else {
            _voters.emplace( account, [&]( auto& v ) {
               v.owner  = account;
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::cpu_managed, true );
            });
         }

         cpu = *cpu_weight;
      }

      set_resource_limits( account, current_ram, current_net, cpu );
   }

   void system_contract::activate( const eosio::checksum256& feature_digest ) {
      require_auth( get_self() );
      preactivate_feature( feature_digest );
   }

   void system_contract::rmvproducer( const name& producer ) {
      require_auth( get_self() );
      auto prod = _producers.find( producer.value );
      check( prod != _producers.end(), "producer not found" );
      _producers.modify( prod, same_payer, [&](auto& p) {
            p.deactivate();
         });
   }

   void system_contract::updtrevision( uint8_t revision ) {
      require_auth( get_self() );
      check( _gstate2.revision < 255, "can not increment revision" ); // prevent wrap around
      check( revision == _gstate2.revision + 1, "can only increment revision by one" );
      check( revision <= 1, // set upper bound to greatest revision supported in the code
             "specified revision is not yet supported by the code" );
      _gstate2.revision = revision;
   }

   void system_contract::setinflation( int64_t annual_rate, int64_t inflation_pay_factor, int64_t votepay_factor ) {
      require_auth(get_self());
      check(annual_rate >= 0, "annual_rate can't be negative");
      if ( inflation_pay_factor < pay_factor_precision ) {
         check( false, "inflation_pay_factor must not be less than " + std::to_string(pay_factor_precision) );
      }
      if ( votepay_factor < pay_factor_precision ) {
         check( false, "votepay_factor must not be less than " + std::to_string(pay_factor_precision) );
      }
      _gstate4.continuous_rate      = get_continuous_rate(annual_rate);
      _gstate4.inflation_pay_factor = inflation_pay_factor;
      _gstate4.votepay_factor       = votepay_factor;
      _global4.set( _gstate4, get_self() );
   }

   /**
    *  Called after a new account is created. This code enforces resource-limits rules
    *  for new accounts as well as new account naming conventions.
    *
    *  Account names containing '.' symbols must have a suffix equal to the name of the creator.
    *  This allows users who buy a premium name (shorter than 12 characters with no dots) to be the only ones
    *  who can create accounts with the creator's name as a suffix.
    *
    */
   void native::newaccount( const name&       creator,
                            const name&       new_account_name,
                            ignore<authority> owner,
                            ignore<authority> active ) {

      if( creator != get_self() ) {
         uint64_t tmp = new_account_name.value >> 4;
         bool has_dot = false;

         for( uint32_t i = 0; i < 12; ++i ) {
           has_dot |= !(tmp & 0x1f);
           tmp >>= 5;
         }
         if( has_dot ) { // or is less than 12 characters
            auto suffix = new_account_name.suffix();
            if( suffix == new_account_name ) {
               name_bid_table bids(get_self(), get_self().value);
               auto current = bids.find( new_account_name.value );
               check( current != bids.end(), "no active bid for name" );
               check( current->high_bidder == creator, "only highest bidder can claim" );
               check( current->high_bid < 0, "auction for name is not closed yet" );
               bids.erase( current );
            } else {
               check( creator == suffix, "only suffix may create this account" );
            }
         }
      }

      user_resources_table  userres( get_self(), new_account_name.value );

      userres.emplace( new_account_name, [&]( auto& res ) {
        res.owner = new_account_name;
        res.net_weight = asset( 0, system_contract::get_core_symbol() );
        res.cpu_weight = asset( 0, system_contract::get_core_symbol() );
      });

      set_resource_limits( new_account_name, 0, 0, 0 );
   }

   void native::setabi( const name& acnt, const std::vector<char>& abi,
                        const binary_extension<std::string>& memo ) {
      eosio::multi_index< "abihash"_n, abi_hash >  table(get_self(), get_self().value);
      auto itr = table.find( acnt.value );
      if( itr == table.end() ) {
         table.emplace( acnt, [&]( auto& row ) {
            row.owner = acnt;
            row.hash = eosio::sha256(const_cast<char*>(abi.data()), abi.size());
         });
      } else {
         table.modify( itr, same_payer, [&]( auto& row ) {
            row.hash = eosio::sha256(const_cast<char*>(abi.data()), abi.size());
         });
      }
   }

   void system_contract::init( unsigned_int version, const symbol& core ) {
      require_auth( get_self() );
      check( version.value == 0, "unsupported version for init action" );

      auto itr = _rammarket.find(ramcore_symbol.raw());
      check( itr == _rammarket.end(), "system contract has already been initialized" );

      auto system_token_supply   = eosio::token::get_supply(token_account, core.code() );
      check( system_token_supply.symbol == core, "specified core symbol does not exist (precision mismatch)" );

      check( system_token_supply.amount > 0, "system token supply must be greater than 0" );
      _rammarket.emplace( get_self(), [&]( auto& m ) {
         m.supply.amount = 100000000000000ll;
         m.supply.symbol = ramcore_symbol;
         m.base.balance.amount = int64_t(_gstate.free_ram());
         m.base.balance.symbol = ram_symbol;
         m.quote.balance.amount = system_token_supply.amount / 1000;
         m.quote.balance.symbol = core;
      });

      token::open_action open_act{ token_account, { {get_self(), active_permission} } };
      open_act.send( rex_account, core, get_self() );
   }

   // TELOS BEGIN
   void system_contract::votebpout(name bp, uint32_t penalty_hours) {
      require_auth(_self);
      check(penalty_hours != 0, "The penalty should be greater than zero.");

      auto pitr = _producers.find(bp.value);
      check(pitr != _producers.end(), "Producer account was not found");

      _producers.modify(pitr, same_payer, [&](auto &p) {
        p.kick(kick_type::BPS_VOTING, penalty_hours);
      });
   }

   void system_contract::setpayrates(uint64_t bpay, uint64_t worker) {
      require_auth(_self);
      check(worker <= max_worker_monthly_amount, "WPS rate exceeds the max");
      check(bpay <= max_bpay_rate, "BPAY rate exceeds the max");
      _gpayrate.bpay_rate = bpay;
      _gpayrate.worker_amount = worker;
   }

   void system_contract::distviarex(name from, asset amount) {
      system_contract::channel_to_rex(from, amount);
   }

   void system_contract::setvotedecay( uint64_t decay_start_epoch, uint64_t decay_increase_yearly ) {
      require_auth(_self);
      eosio::check(_gvoting_config.evm_voting_contract != eosio::checksum160(), "EVM voting contract not set");
      _gvoting_config.decay_start_epoch = decay_start_epoch;
      _gvoting_config.decay_increase_yearly = decay_increase_yearly;

      // Create a EVM TX for to update the status of BP in EVM
      std::array<uint8_t, 68> tx_data{};
      // Fill the 4-byte checksum of function 0x5d32ff31
      //    setDecayParameters(uint256 _decayStartEpoch, uint256 _decayIncreaseYearly)
      tx_data[0] = 0x5d;
      tx_data[1] = 0x32;
      tx_data[2] = 0xff;
      tx_data[3] = 0x31;

      // helper – write an uint64 into the last 8 bytes of a 32-byte word
      auto write_u64_be = [](uint8_t* word, uint64_t value) {
         for (int i = 0; i < 8; ++i)         // write from the right
            word[31 - i] = static_cast<uint8_t>(value >> (i * 8));
      };

      uint64_t decay_increase_scaled = decay_increase_yearly * 10000000000000000ULL; // 10^16
      write_u64_be(tx_data.data() + 4,  decay_start_epoch);
      write_u64_be(tx_data.data() + 36, decay_increase_scaled);

      // Get eosio EVM address nonce
      eosio_evm::account_table account(evm_account, evm_account.value);
      std::array<uint8_t, 20> evm_voting_contract_address = _gvoting_config.evm_voting_contract.extract_as_byte_array();
      auto accounts_byaccount = account.get_index<eosio::name("byaccount")>();
      auto eosio_account = accounts_byaccount.find(get_self().value);
      std::optional<eosio::checksum160> eosio_account_address = eosio_account->address;
      eosio::check(eosio_account != accounts_byaccount.end(),"eosio EVM address not found");

      // REGISTER 159071ec UNREGISTER ab2e2ac4
      auto tx_hex = rlp::encode(
         uint256_t(eosio_account->nonce), // Nonce
         uint256_t(0), // Gas price
         uint256_t(100000), // Gas limit
         evm_voting_contract_address, // To
         uint256_t(0), // Value
         tx_data, // Data
         uint8_t(0), // V
         uint256_t(0), // R
         uint256_t(0) // S
      );

      eosio::action(
         eosio::permission_level{get_self(),active_permission},
         evm_account,
         "raw"_n,
         std::make_tuple(evm_account, tx_hex, false, eosio_account_address)
      ).send();
   }

   void system_contract::setvotecontr( eosio::checksum160 contract ) {
      require_auth(_self);
      _gvoting_config.evm_voting_contract = contract;
   }

   void system_contract::getevmvote( std::vector<eosio::name> bps ) {

      eosio::check(_gvoting_config.evm_voting_contract != eosio::checksum160(), "EVM voting contract not set");

      check( bps.size() <= 30, "attempt to uodate EVM vote for too many BPs" );
      for( size_t i = 1; i < bps.size(); ++i ) {
         check( bps[i-1] < bps[i], "list of BPs that need to be updated must be unique and sorted" );
      }

      // Get the EVM voting contract
      
      eosio_evm::account_table account(evm_account, evm_account.value);
      auto accounts_byaddress = account.get_index<eosio::name("byaddress")>();
      std::array<uint8_t, 20> evm_voting_contract_address = _gvoting_config.evm_voting_contract.extract_as_byte_array();
      std::array<uint8_t, 32> evm_voting_contract_address_256{};
      std::copy(evm_voting_contract_address.begin(), evm_voting_contract_address.end(), evm_voting_contract_address_256.begin() + 12);
      auto evm_voting_account = accounts_byaddress.find(eosio::checksum256(evm_voting_contract_address_256));
      eosio::check(evm_voting_account != accounts_byaddress.end(),"EVM voting contract not found");
      
      // Get the access to the state
      eosio_evm::account_state_table accounts_states(evm_account, evm_voting_account->primary_key());
      auto accounts_states_bykey = accounts_states.get_index<eosio::name("bykey")>();

      // Add a flag to make sure at least a single vote is changed
      bool is_changed = false;

      for (eosio::name bp : bps) {

         // Access the vote of the BP in the EVM voting contract internal state
         std::array<uint8_t, 64> vote_memory_location = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
         std::array<uint8_t, 8> bp_name_bytes{};
         for (size_t i = 0; i < 8; ++i) {
            bp_name_bytes[7 - i] = static_cast<uint8_t>(bp.value >> (i * 8));
         }
         std::copy(std::begin(bp_name_bytes), std::end(bp_name_bytes), vote_memory_location.begin() + 24);
         eosio::checksum256 vote_memory_location_key = eosio::keccak((char*)vote_memory_location.data(), 64);
         
         // Get the total votes of the BP
         auto total_votes_of_bp = accounts_states_bykey.find(vote_memory_location_key);

         // Search for the BP in the `evmvotes` table
         auto evmvotes_byname = _evm_votes.get_index<eosio::name("byname")>();
         auto evm_vote = evmvotes_byname.find(bp.value);
         const uint256_t ten_power_14 = intx::from_string<uint256_t>("0x5af3107a4000");

         if (evm_vote == evmvotes_byname.end()) {
            // First time EVM vote

            is_changed = true;

            // Apply the EVM votes of the BP
            auto pitr = _producers.find( bp.value );
            eosio::check( pitr != _producers.end(), "BP not found" );
            _producers.modify( pitr, same_payer, [&]( auto& p ) {
               uint256_t current_vote = eosio_evm::checksum256ToValue(total_votes_of_bp->value);
               uint256_t current_vote_normalized = current_vote / ten_power_14; // Divide be 1e14
               uint64_t current_vote_normalized_u64 = current_vote_normalized.lo.lo;
               p.total_votes += double(current_vote_normalized_u64);
               _gstate.total_producer_vote_weight += double(current_vote_normalized_u64);
            });

            // Add a new row to store the new BP vote data
            _evm_votes.emplace(_self, [&](auto &a) {
               a.bp = bp;
               a.total_vote = total_votes_of_bp->value;
            });
         } else {
            // Old EVM vote updated

            // Check whether the list of voted BPs or the vote weight has changed
            if (!is_changed) {
               is_changed = evm_vote->total_vote != total_votes_of_bp->value;
            }            

            // Apply the EVM votes of the BP
            auto pitr = _producers.find( bp.value );
            eosio::check( pitr != _producers.end(), "BP not found" );
            _producers.modify( pitr, same_payer, [&]( auto& p ) {
               uint256_t previous_vote = eosio_evm::checksum256ToValue(evm_vote->total_vote);
               uint256_t previous_vote_normalized = previous_vote / ten_power_14; // Divide be 1e14
               uint64_t previous_vote_normalized_u64 = previous_vote_normalized.lo.lo;
               uint256_t current_vote = eosio_evm::checksum256ToValue(total_votes_of_bp->value);
               uint256_t current_vote_normalized = current_vote / ten_power_14; // Divide be 1e14
               uint64_t current_vote_normalized_u64 = current_vote_normalized.lo.lo;
               p.total_votes += double(current_vote_normalized_u64 - previous_vote_normalized_u64);
               if ( p.total_votes < 0 ) {
                  p.total_votes = 0;
               }
               _gstate.total_producer_vote_weight += double(current_vote_normalized_u64 - previous_vote_normalized_u64);
            });
            
            // Apply the updated vote to the existing row
            evmvotes_byname.modify(evm_vote, same_payer, [&](auto& a) {
               a.total_vote = total_votes_of_bp->value;
             });
         }

      }  

      eosio::check(is_changed, "None of the BPs EVM votes has been changed");
   }
   void system_contract::setbpevmstat( eosio::name bp ) {

      eosio::check(_gvoting_config.evm_voting_contract != eosio::checksum160(), "EVM voting contract not set");

      auto pitr = _producers.find( bp.value );
      eosio::check(pitr != _producers.end(), "BP not found");

      // Get the EVM voting contract
      eosio_evm::account_table account(evm_account, evm_account.value);
      auto accounts_byaddress = account.get_index<eosio::name("byaddress")>();
      std::array<uint8_t, 20> evm_voting_contract_address = _gvoting_config.evm_voting_contract.extract_as_byte_array();
      std::array<uint8_t, 32> evm_voting_contract_address_256{};
      std::copy(evm_voting_contract_address.begin(), evm_voting_contract_address.end(), evm_voting_contract_address_256.begin() + 12);
      auto evm_voting_account = accounts_byaddress.find(eosio::checksum256(evm_voting_contract_address_256));
      eosio::check(evm_voting_account != accounts_byaddress.end(),"EVM voting contract not found");

      // Get the access to the state
      eosio_evm::account_state_table accounts_states(evm_account, evm_voting_account->primary_key());
      auto accounts_states_bykey = accounts_states.get_index<eosio::name("bykey")>();

      // Access the status of the BP in the EVM voting contract internal state
      std::array<uint8_t, 64> bp_status_location = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4};
      std::array<uint8_t, 8> bp_name_bytes{};
      for (size_t i = 0; i < 8; ++i) {
         bp_name_bytes[7 - i] = static_cast<uint8_t>(bp.value >> (i * 8));
      }
      std::copy(std::begin(bp_name_bytes), std::end(bp_name_bytes), bp_status_location.begin() + 24);
      eosio::checksum256 bp_status_location_key = eosio::keccak((char*)bp_status_location.data(), 64);
      
      // Get the status of the BP
      auto status_of_bp = accounts_states_bykey.find(bp_status_location_key);
      // Get status of BP in EVM
      bool bp_status_in_evm = status_of_bp->value.extract_as_byte_array()[31];

      eosio::check(pitr->active() != bp_status_in_evm, "The status of BP is not changed");

      // Create a EVM TX for to update the status of BP in EVM
      std::array<uint8_t, 36> tx_data{};
      // Fill the 4-byte checksum of function
      if (pitr->active()) {
         // Call registerBP 0x159071ec
         tx_data[0] = 21;
         tx_data[1] = 144;
         tx_data[2] = 113;
         tx_data[3] = 236;
      } else {
         // Call unregisterBP 0xab2e2ac4
         tx_data[0] = 171;
         tx_data[1] = 46;
         tx_data[2] = 42;
         tx_data[3] = 196;
      }
      std::copy(std::begin(bp_name_bytes), std::end(bp_name_bytes), tx_data.begin() + 28);
      // Get eosio EVM address nonce
      auto accounts_byaccount = account.get_index<eosio::name("byaccount")>();
      auto eosio_account = accounts_byaccount.find(get_self().value);
      std::optional<eosio::checksum160> eosio_account_address = eosio_account->address;
      eosio::check(eosio_account != accounts_byaccount.end(),"eosio EVM address not found");

      // REGISTER 159071ec UNREGISTER ab2e2ac4
      auto tx_hex = rlp::encode(
         uint256_t(eosio_account->nonce), // Nonce
         uint256_t(0), // Gas price
         uint256_t(100000), // Gas limit
         evm_voting_contract_address, // To
         uint256_t(0), // Value
         tx_data, // Data
         uint8_t(0), // V
         uint256_t(0), // R
         uint256_t(0) // S
      );

      eosio::action(
         eosio::permission_level{get_self(),active_permission},
         evm_account,
         "raw"_n,
         std::make_tuple(evm_account, tx_hex, false, eosio_account_address)
      ).send();

   }
      
   void system_contract::setselfstake( uint64_t self_stake_boost_multiplier ) {
      require_auth(_self);
      _gvoting_config.self_stake_boost_multiplier = self_stake_boost_multiplier;
   }
   // TELOS END
} /// eosio.system
