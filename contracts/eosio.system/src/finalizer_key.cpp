#include <eosio.system/eosio.system.hpp>

#include <eosio/crypto_bls_ext.hpp>
#include <eosio/eosio.hpp>

#include <algorithm>

namespace eosiosystem {
   finalizer_auth_info::finalizer_auth_info(const finalizer_info& finalizer)
      : key_id(finalizer.active_key_id)
      , fin_authority( eosio::finalizer_authority{
         .description = finalizer.finalizer_name.to_string(),
         .weight      = 1,
         .public_key  = finalizer.active_key_binary })
   {
   }

   bool system_contract::is_savanna_consensus() {
      return !get_last_proposed_finalizers().empty();
   }

   bool system_contract::has_active_finalizer_key( const name& producer ) const {
      auto finalizer = _finalizers.find( producer.value );
      return finalizer != _finalizers.end() && !finalizer->active_key_binary.empty();
   }

   eosio::bls_g1 to_binary(const std::string& finalizer_key) {
      check(finalizer_key.compare(0, 7, "PUB_BLS") == 0, "finalizer key does not start with PUB_BLS: " + finalizer_key);
      return eosio::decode_bls_public_key_to_g1(finalizer_key);
   }

   static eosio::checksum256 get_finalizer_key_hash(const eosio::bls_g1& finalizer_key_binary) {
      return eosio::sha256(finalizer_key_binary.data(), finalizer_key_binary.size());
   }

   static eosio::checksum256 get_finalizer_key_hash(const std::string& finalizer_key) {
      const auto fin_key_g1 = to_binary(finalizer_key);
      return get_finalizer_key_hash(fin_key_g1);
   }

   finalizers_table::const_iterator system_contract::get_finalizer_itr( const name& finalizer_name ) const {
      auto finalizer_itr = _finalizers.find(finalizer_name.value);
      check( finalizer_itr != _finalizers.end(), "finalizer " + finalizer_name.to_string() + " has not registered any finalizer keys" );
      check( finalizer_itr->finalizer_key_count > 0, "finalizer " + finalizer_name.to_string() + " must have at least one registered finalizer key" );

      return finalizer_itr;
   }

   void system_contract::set_proposed_finalizers( std::vector<finalizer_auth_info> proposed_finalizers ) {
      std::sort( proposed_finalizers.begin(), proposed_finalizers.end(), []( const finalizer_auth_info& lhs, const finalizer_auth_info& rhs ) {
         return lhs.key_id < rhs.key_id;
      } );

      const auto& last_proposed_finalizers = get_last_proposed_finalizers();
      if( proposed_finalizers == last_proposed_finalizers ) {
         return;
      }

      std::vector<eosio::finalizer_authority> finalizer_authorities;
      finalizer_authorities.reserve(proposed_finalizers.size());
      for( const auto& k: proposed_finalizers ) {
         finalizer_authorities.emplace_back(k.fin_authority);
      }

      eosio::finalizer_policy fin_policy {
         .threshold  = ( finalizer_authorities.size() * 2 ) / 3 + 1,
         .finalizers = std::move(finalizer_authorities)
      };

      eosio::set_finalizers(std::move(fin_policy));

      auto itr = _last_prop_finalizers.begin();
      if( itr == _last_prop_finalizers.end() ) {
         _last_prop_finalizers.emplace( get_self(), [&]( auto& f ) {
            f.last_proposed_finalizers = proposed_finalizers;
         });
      } else {
         _last_prop_finalizers.modify(itr, same_payer, [&]( auto& f ) {
            f.last_proposed_finalizers = proposed_finalizers;
         });
      }

      if( _last_prop_finalizers_cached.has_value() ) {
         std::swap(*_last_prop_finalizers_cached, proposed_finalizers);
      } else {
         _last_prop_finalizers_cached.emplace(std::move(proposed_finalizers));
      }
   }

   const std::vector<finalizer_auth_info>& system_contract::get_last_proposed_finalizers() {
      if( !_last_prop_finalizers_cached.has_value() ) {
         const auto finalizers_itr = _last_prop_finalizers.begin();
         if( finalizers_itr == _last_prop_finalizers.end() ) {
            _last_prop_finalizers_cached = {};
         } else {
            _last_prop_finalizers_cached = finalizers_itr->last_proposed_finalizers;
         }
      }

      return *_last_prop_finalizers_cached;
   }

   uint64_t system_contract::get_next_finalizer_key_id() {
      uint64_t next_id = 0;
      auto itr = _fin_key_id_generator.begin();

      if( itr == _fin_key_id_generator.end() ) {
         _fin_key_id_generator.emplace( get_self(), [&]( auto& f ) {
            f.next_finalizer_key_id = next_id;
         });
      } else {
         next_id = itr->next_finalizer_key_id + 1;
         _fin_key_id_generator.modify(itr, same_payer, [&]( auto& f ) {
            f.next_finalizer_key_id = next_id;
         });
      }

      return next_id;
   }

   std::vector<finalizer_auth_info> system_contract::get_finalizers_for_producers( const std::vector<producer_location_pair>& producers ) const {
      std::vector<finalizer_auth_info> proposed_finalizers;
      proposed_finalizers.reserve(producers.size());

      for( const auto& item: producers ) {
         const auto& producer_name = item.first.producer_name;
         auto finalizer = _finalizers.find(producer_name.value);
         check( finalizer != _finalizers.end() && !finalizer->active_key_binary.empty(),
                "producer " + producer_name.to_string() + " does not have an active finalizer key" );

         proposed_finalizers.emplace_back(*finalizer);
      }

      return proposed_finalizers;
   }

   bool system_contract::active_schedule_matches_last_scheduled_producers( const std::vector<name>& active_schedule ) const {
      if( active_schedule.size() != _gschedule_metrics.producers_metric.size() ) {
         return false;
      }

      std::vector<name> sorted_active_schedule = active_schedule;
      std::vector<name> sorted_scheduled_producers;
      sorted_scheduled_producers.reserve(_gschedule_metrics.producers_metric.size());

      for( const auto& metric: _gschedule_metrics.producers_metric ) {
         sorted_scheduled_producers.emplace_back(metric.bp_name);
      }

      std::sort(sorted_active_schedule.begin(), sorted_active_schedule.end());
      std::sort(sorted_scheduled_producers.begin(), sorted_scheduled_producers.end());

      return sorted_active_schedule == sorted_scheduled_producers;
   }

   std::vector<producer_location_pair> system_contract::get_last_scheduled_producers() const {
      std::vector<producer_location_pair> scheduled_producers;
      scheduled_producers.reserve(_gschedule_metrics.producers_metric.size());

      for( const auto& metric: _gschedule_metrics.producers_metric ) {
         const auto prod = _producers.find(metric.bp_name.value);
         check( prod != _producers.end(), "scheduled producer " + metric.bp_name.to_string() + " is not registered" );
         check( prod->active(), "scheduled producer " + metric.bp_name.to_string() + " is not active" );

         scheduled_producers.emplace_back(
            eosio::producer_authority{
               .producer_name = prod->owner,
               .authority     = prod->get_producer_authority()
            },
            prod->location
         );
      }

      return scheduled_producers;
   }

   void system_contract::switchtosvnn() {
      require_auth(get_self());

      check(!is_savanna_consensus(), "switchtosvnn can be run only once");
      check(_gstate.last_producer_schedule_size > 0, "producer schedule must be established before switching to Savanna");

      const auto scheduled_producers = get_last_scheduled_producers();
      check( scheduled_producers.size() == _gstate.last_producer_schedule_size,
             "Telos scheduled producer metrics do not match last producer schedule size" );

      const auto active_schedule = eosio::get_active_producers();
      check( active_schedule.size() == _gstate.last_producer_schedule_size,
             "active producer schedule size does not match last producer schedule size" );
      check( active_schedule_matches_last_scheduled_producers(active_schedule),
             "active producer schedule does not match Telos scheduled producer metrics" );

      auto proposed_finalizers = get_finalizers_for_producers(scheduled_producers);
      check( proposed_finalizers.size() == _gstate.last_producer_schedule_size,
             "not enough scheduled producers have registered finalizer keys, has " + std::to_string(proposed_finalizers.size()) +
             ", require " + std::to_string(_gstate.last_producer_schedule_size) );

      set_proposed_finalizers(std::move(proposed_finalizers));
      check(is_savanna_consensus(), "switching to Savanna failed");
   }

   void system_contract::regfinkey( const name& finalizer_name, const std::string& finalizer_key, const std::string& proof_of_possession ) {
      require_auth( finalizer_name );

      auto producer = _producers.find( finalizer_name.value );
      check( producer != _producers.end(), "finalizer " + finalizer_name.to_string() + " is not a registered producer");

      check(proof_of_possession.compare(0, 7, "SIG_BLS") == 0, "proof of possession signature does not start with SIG_BLS: " + proof_of_possession);

      const auto fin_key_g1 = to_binary(finalizer_key);
      const auto pop_g2 = eosio::decode_bls_signature_to_g2(proof_of_possession);

      const auto idx = _finalizer_keys.get_index<"byfinkey"_n>();
      const auto hash = get_finalizer_key_hash(fin_key_g1);
      check(idx.find(hash) == idx.end(), "duplicate finalizer key: " + finalizer_key);

      check(eosio::bls_pop_verify(fin_key_g1, pop_g2), "proof of possession check failed");

      const auto finalizer_key_itr = _finalizer_keys.emplace( finalizer_name, [&]( auto& k ) {
         k.id                   = get_next_finalizer_key_id();
         k.finalizer_name       = finalizer_name;
         k.finalizer_key        = finalizer_key;
         k.finalizer_key_binary = { fin_key_g1.begin(), fin_key_g1.end() };
      });

      auto finalizer = _finalizers.find(finalizer_name.value);
      if( finalizer == _finalizers.end() ) {
         _finalizers.emplace( finalizer_name, [&]( auto& f ) {
            f.finalizer_name      = finalizer_name;
            f.active_key_id       = finalizer_key_itr->id;
            f.active_key_binary   = finalizer_key_itr->finalizer_key_binary;
            f.finalizer_key_count = 1;
         });
      } else {
         _finalizers.modify( finalizer, same_payer, [&]( auto& f ) {
            ++f.finalizer_key_count;
         });
      }
   }

   void system_contract::actfinkey( const name& finalizer_name, const std::string& finalizer_key ) {
      require_auth( finalizer_name );

      const auto finalizer = get_finalizer_itr(finalizer_name);

      const auto idx = _finalizer_keys.get_index<"byfinkey"_n>();
      const auto hash = get_finalizer_key_hash(finalizer_key);
      const auto finalizer_key_itr = idx.find(hash);
      check(finalizer_key_itr != idx.end(), "finalizer key was not registered: " + finalizer_key);

      check(finalizer_key_itr->finalizer_name == name(finalizer_name), "finalizer key was not registered by the finalizer: " + finalizer_key);
      check(!finalizer_key_itr->is_active(finalizer->active_key_id), "finalizer key was already active: " + finalizer_key);

      const auto active_key_id = finalizer->active_key_id;

      _finalizers.modify( finalizer, same_payer, [&]( auto& f ) {
         f.active_key_id     = finalizer_key_itr->id;
         f.active_key_binary = finalizer_key_itr->finalizer_key_binary;
      });

      const auto& last_proposed_finalizers = get_last_proposed_finalizers();
      if( last_proposed_finalizers.empty() ) {
         return;
      }

      auto itr = std::lower_bound(last_proposed_finalizers.begin(), last_proposed_finalizers.end(), active_key_id, [](const finalizer_auth_info& key, uint64_t id) {
         return key.key_id < id;
      });

      if( itr != last_proposed_finalizers.end() && itr->key_id == active_key_id ) {
         auto proposed_finalizers = last_proposed_finalizers;
         auto& matching_entry = proposed_finalizers[itr - last_proposed_finalizers.begin()];

         matching_entry.key_id = finalizer_key_itr->id;
         matching_entry.fin_authority.public_key = finalizer_key_itr->finalizer_key_binary;

         set_proposed_finalizers(std::move(proposed_finalizers));
      }
   }

   void system_contract::delfinkey( const name& finalizer_name, const std::string& finalizer_key ) {
      require_auth( finalizer_name );

      auto finalizer = get_finalizer_itr(finalizer_name);

      auto idx = _finalizer_keys.get_index<"byfinkey"_n>();
      auto hash = get_finalizer_key_hash(finalizer_key);
      auto fin_key_itr = idx.find(hash);
      check(fin_key_itr != idx.end(), "finalizer key was not registered: " + finalizer_key);

      check(fin_key_itr->finalizer_name == name(finalizer_name), "finalizer key " + finalizer_key + " was not registered by the finalizer " + finalizer_name.to_string());

      if( fin_key_itr->is_active(finalizer->active_key_id) ) {
         check( finalizer->finalizer_key_count == 1, "cannot delete an active key unless it is the last registered finalizer key, has " + std::to_string(finalizer->finalizer_key_count) + " keys");
      }

      if( finalizer->finalizer_key_count == 1 ) {
         _finalizers.erase(finalizer);
      } else {
         _finalizers.modify(finalizer, same_payer, [&]( auto& f ) {
            --f.finalizer_key_count;
         });
      }

      idx.erase(fin_key_itr);
   }
} /// namespace eosiosystem
