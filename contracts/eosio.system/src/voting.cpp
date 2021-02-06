#include <eosio.system/eosio.system.hpp>

#include <eosio/eosio.hpp>
#include <eosio/datastream.hpp>
#include <eosio/serialize.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/privileged.hpp>
#include <eosio/singleton.hpp>
#include <eosio/transaction.hpp>
#include <eosio.token/eosio.token.hpp>

#include <boost/container/flat_map.hpp>

#include "system_rotation.cpp"

#include <algorithm>
#include <cmath>

namespace eosiosystem {

   using eosio::const_mem_fun;
   using eosio::current_time_point;
   using eosio::indexed_by;
   using eosio::microseconds;
   using eosio::singleton;

   void system_contract::regproducer( const name& producer, const eosio::public_key& producer_key, const std::string& url, uint16_t location ) {
      check( url.size() < 512, "url too long" );
      check( producer_key != eosio::public_key(), "public key should not be the default value" );
      require_auth( producer );
      const auto ct = current_time_point();

      auto prod = _producers.find(producer.value);
      if ( prod != _producers.end() ) {
         _producers.modify( prod, producer, [&]( producer_info& info ) {
            auto now = block_timestamp(current_time_point());
            block_timestamp penalty_expiration_time = block_timestamp(info.last_time_kicked.to_time_point() + time_point(hours(int64_t(info.kick_penalty_hours))));
            
            check(now.slot > penalty_expiration_time.slot,
               std::string("Producer is not allowed to register at this time. Please fix your node and try again later in: " 
               + std::to_string( uint32_t((penalty_expiration_time.slot - now.slot) / 2 ))  
               + " seconds").c_str());
               
            info.producer_key = producer_key;
            info.url          = url;
            info.location     = location;
            info.is_active    = true;
            info.unreg_reason = "";
         });
      } else {
         _producers.emplace( producer, [&]( producer_info& info ){
            info.owner           = producer;
            info.total_votes     = 0;
            info.producer_key    = producer_key;
            info.is_active       = true;
            info.url             = url;
            info.location        = location;
            info.last_claim_time = ct;
            info.unreg_reason    = "";
         });
      }

   }

   void system_contract::unregprod( const name& producer ) {
      require_auth( producer );

      const auto& prod = _producers.get( producer.value, "producer not found" );
      _producers.modify( prod, same_payer, [&]( producer_info& info ){
         info.deactivate();
      });
   }
   
   void system_contract::unregreason( name producer, std::string reason ) {
      check( reason.size() < 255, "The reason is too long. Reason should not have more than 255 characters.");
      require_auth( producer );

      const auto& prod = _producers.get( producer.value, "producer not found" );
      _producers.modify( prod, same_payer, [&]( producer_info& info ){
         info.deactivate();
         info.unreg_reason = reason;
      });
   }

   void system_contract::update_elected_producers( const block_timestamp& block_time ) {
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<"prototalvote"_n>();

      uint32_t totalActiveVotedProds = uint32_t(std::distance(idx.begin(), idx.end()));
      totalActiveVotedProds = totalActiveVotedProds > MAX_PRODUCERS ? MAX_PRODUCERS : totalActiveVotedProds;

      std::vector<eosio::producer_key> prods;
      prods.reserve(size_t(totalActiveVotedProds));

      for ( auto it = idx.cbegin(); it != idx.cend() && prods.size() < totalActiveVotedProds && it->total_votes > 0 && it->active(); ++it ) {
         prods.emplace_back( eosio::producer_key{it->owner, it->producer_key} );
      }

      std::vector<eosio::producer_key> top_producers = check_rotation_state(prods, block_time);

      /// sort by producer name
      std::sort( top_producers.begin(), top_producers.end() );

      auto schedule_version = set_proposed_producers(top_producers);
      if (schedule_version >= 0) {
        print("\n**new schedule was proposed**");
        
        _gstate.last_proposed_schedule_update = block_time;

        _gschedule_metrics.producers_metric.erase( _gschedule_metrics.producers_metric.begin(), _gschedule_metrics.producers_metric.end());
        
        std::vector<producer_metric> psm;
        std::for_each(top_producers.begin(), top_producers.end(), [&psm](auto &tp) {
          auto bp_name = tp.producer_name;
          psm.emplace_back(producer_metric{ bp_name, 12 });
        });
 
        _gschedule_metrics.producers_metric = psm;
        
        _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>(top_producers.size());
      }
   }

   /*
   * This function caculates the inverse weight voting. 
   * The maximum weighted vote will be reached if an account votes for the maximum number of registered producers (up to 30 in total).  
   */   
   double system_contract::inverse_vote_weight(double staked, double amountVotedProducers) {
     if (amountVotedProducers == 0.0) {
       return 0;
     }

     double percentVoted = amountVotedProducers / MAX_VOTE_PRODUCERS;
     double voteWeight = (sin(M_PI * percentVoted - M_PI_2) + 1.0) / 2.0;
     return (voteWeight * staked);
   }

   void system_contract::voteproducer( const name& voter_name, const name& proxy, const std::vector<name>& producers ) {
      require_auth( voter_name );
      vote_stake_updater( voter_name );
      update_votes( voter_name, proxy, producers, true );
      // auto rex_itr = _rexbalance.find( voter_name.value );            Remove requirement to vote 21 BPs or select a proxy to stake to REX
      // if( rex_itr != _rexbalance.end() && rex_itr->rex_balance.amount > 0 ) {
      //    check_voting_requirement( voter_name, "voter holding REX tokens must vote for at least 21 producers or for a proxy" );
      // }
   }

   void system_contract::update_votes( const name& voter_name, const name& proxy, const std::vector<name>& producers, bool voting ) {
      //validate input
      if ( proxy ) {
         check( producers.size() == 0, "cannot vote for producers and proxy at same time" );
         check( voter_name != proxy, "cannot proxy to self" );
      } else {
         check( producers.size() <= 30, "attempt to vote for too many producers" );
         for( size_t i = 1; i < producers.size(); ++i ) {
            check( producers[i-1] < producers[i], "producer votes must be unique and sorted" );
         }
      }

      auto voter = _voters.find( voter_name.value );
      check( voter != _voters.end(), "user must stake before they can vote" ); /// staking creates voter object
      check( !proxy || !voter->is_proxy, "account registered as a proxy is not allowed to use a proxy" );

      auto totalStaked = voter->staked;
      if(voter->is_proxy){
         totalStaked += voter->proxied_vote_weight;
      }

      // when unvoting, set the stake used for calculations to 0
      // since it is the equivalent to retracting your stake
      if(voting && !proxy && producers.size() == 0){
         totalStaked = 0;
      }

      // when a voter or a proxy votes or changes stake, the total_activated stake should be re-calculated
      // any proxy stake handling should be done when the proxy votes or on weight propagation
      // if(_gstate.thresh_activated_stake_time == 0 && !proxy && !voter->proxy){
      if(!proxy && !voter->proxy){
         _gstate.total_activated_stake += totalStaked - voter->last_stake;
      }

      auto new_vote_weight = inverse_vote_weight((double)totalStaked, (double) producers.size());
      boost::container::flat_map<name, std::pair< double, bool > > producer_deltas;

      // print("\n Voter : ", voter->last_stake, " = ", voter->last_vote_weight, " = ", proxy, " = ", producers.size(), " = ", totalStaked, " = ", new_vote_weight);
      
      //Voter from second vote
      if ( voter->last_stake > 0 ) {

         //if voter account has set proxy to another voter account
         if( voter->proxy ) { 
            auto old_proxy = _voters.find( voter->proxy.value );
            check( old_proxy != _voters.end(), "old proxy not found" ); //data corruption
            _voters.modify( old_proxy, same_payer, [&]( auto& vp ) {
               vp.proxied_vote_weight -= voter->last_stake;
            });

            // propagate weight here only when switching proxies
            // otherwise propagate happens in the case below
            if( proxy != voter->proxy ) {  
               _gstate.total_activated_stake += totalStaked - voter->last_stake;
               propagate_weight_change( *old_proxy );
            }
         } else {
            for( const auto& p : voter->producers ) {
               auto& d = producer_deltas[p];
               d.first -= voter->last_vote_weight;
               d.second = false;
            }
         }
      }

      if( proxy ) {
         auto new_proxy = _voters.find( proxy.value );
         check( new_proxy != _voters.end(), "invalid proxy specified" ); //if ( !voting ) { data corruption } else { wrong vote }
         check( !voting || new_proxy->is_proxy, "proxy not found" );
        
         _voters.modify( new_proxy, same_payer, [&]( auto& vp ) {
            vp.proxied_vote_weight += voter->staked;
         });
         
         if((*new_proxy).last_vote_weight > 0){
            _gstate.total_activated_stake += totalStaked - voter->last_stake;
            propagate_weight_change( *new_proxy );
         }
      } else {
         if( new_vote_weight >= 0 ) {
            for( const auto& p : producers ) {
               auto& d = producer_deltas[p]; 
               d.first += new_vote_weight;
               d.second = true;
            }
         }
      }

      for( const auto& pd : producer_deltas ) {
         auto pitr = _producers.find( pd.first.value );
         if( pitr != _producers.end() ) {
            if( voting && !pitr->active() && pd.second.second /* from new set */ ) {
               check( false, ( "producer " + pitr->owner.to_string() + " is not currently registered" ).data() );
            }
            _producers.modify( pitr, same_payer, [&]( auto& p ) {
               p.total_votes += pd.second.first;
               if ( p.total_votes < 0 ) { // floating point arithmetics can give small negative numbers
                  p.total_votes = 0;
               }
               _gstate.total_producer_vote_weight += pd.second.first;
               //check( p.total_votes >= 0, "something bad happened" );
            });
         } else {
            if( pd.second.second ) {
               check( false, ( "producer " + pd.first.to_string() + " is not registered" ).data() );
            }
         }
      }

      _voters.modify( voter, same_payer, [&]( auto& av ) {
         av.last_vote_weight = new_vote_weight;
         av.last_stake = int64_t(totalStaked);
         av.producers = producers;
         av.proxy     = proxy;
      });
   }

   void system_contract::regproxy( const name& proxy, bool isproxy ) {
      require_auth( proxy );
      auto pitr = _voters.find( proxy.value );
      if ( pitr != _voters.end() ) {
         check( isproxy != pitr->is_proxy, "action has no effect" );
         check( !isproxy || !pitr->proxy, "account that uses a proxy is not allowed to become a proxy" );
         _voters.modify( pitr, same_payer, [&]( auto& p ) {
          p.is_proxy = isproxy;
        });    
         update_votes(pitr->owner, pitr->proxy, pitr->producers, true);
      } else {
         _voters.emplace( proxy, [&]( auto& p ) {
            p.owner  = proxy;
            p.is_proxy = isproxy;
       });
      }
   }

   void system_contract::propagate_weight_change( const voter_info& voter ) {
      check( voter.proxy == name(0) || !voter.is_proxy, "account registered as a proxy is not allowed to use a proxy");
      
      auto totalStake = voter.staked;
      if(voter.is_proxy){
         totalStake += voter.proxied_vote_weight;
      } 
      double new_weight = inverse_vote_weight((double)totalStake, voter.producers.size());
      double delta = new_weight - voter.last_vote_weight;

      if (voter.proxy) { // this part should never happen since the function is called only on proxies
         if(voter.last_stake != totalStake){
            auto &proxy = _voters.get(voter.proxy.value, "proxy not found"); // data corruption
            _voters.modify(proxy, same_payer, [&](auto &p) { 
               p.proxied_vote_weight += totalStake - voter.last_stake;
            });
            
            propagate_weight_change(proxy);
         }
      } else {
         for (auto acnt : voter.producers) {
            auto &pitr = _producers.get(acnt.value, "producer not found"); // data corruption
            _producers.modify(pitr, same_payer, [&](auto &p) {
               p.total_votes += delta;
               _gstate.total_producer_vote_weight += delta;
            });
         }
      }
      
      _voters.modify(voter, same_payer, [&](auto &v) { 
         v.last_vote_weight = new_weight; 
         v.last_stake = totalStake;
      });
   }

   void system_contract::recalculate_votes(){
    if (_gstate.total_producer_vote_weight <= -0.1){ // -0.1 threshold for floating point calc ?
        _gstate.total_producer_vote_weight = 0;
        _gstate.total_activated_stake = 0;
        for(auto producer = _producers.begin(); producer != _producers.end(); ++producer){
            _producers.modify(producer, same_payer, [&](auto &p) {
                p.total_votes = 0;
            });
        }
        boost::container::flat_map< name, bool> processed_proxies;
        for (auto voter = _voters.begin(); voter != _voters.end(); ++voter) {
            if(voter->proxy && !processed_proxies[voter->proxy]){
                auto proxy = _voters.find(voter->proxy.value);
                _voters.modify( proxy, same_payer, [&]( auto& av ) {
                    av.last_vote_weight = 0;
                    av.last_stake = 0;
                    av.proxied_vote_weight = 0;
                });
                processed_proxies[voter->proxy] = true;
            }
            if(!voter->is_proxy || !processed_proxies[voter->owner]){
                _voters.modify( voter, same_payer, [&]( auto& av ) {
                    av.last_vote_weight = 0;
                    av.last_stake = 0;
                    av.proxied_vote_weight = 0;
                });
                processed_proxies[voter->owner] = true;
            }
            update_votes(voter->owner, voter->proxy, voter->producers, true);
        }
    }
}

} /// namespace eosiosystem
