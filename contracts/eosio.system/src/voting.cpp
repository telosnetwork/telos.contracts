#include <eosio/crypto.hpp>
#include <eosio/datastream.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/permission.hpp>
#include <eosio/privileged.hpp>
#include <eosio/serialize.hpp>
#include <eosio/singleton.hpp>

#include <eosio.system/eosio.system.hpp>
#include <eosio.token/eosio.token.hpp>

#include <type_traits>
#include <limits>
#include <set>
#include <algorithm>
#include <cmath>

// TELOS BEGIN
#include <boost/container/flat_map.hpp>
#include "system_rotation.cpp"
// TELOS END

namespace eosiosystem {

   using eosio::const_mem_fun;
   using eosio::current_time_point;
   using eosio::indexed_by;
   using eosio::microseconds;
   using eosio::singleton;

   void system_contract::register_producer( const name& producer, const eosio::block_signing_authority& producer_authority, const std::string& url, uint16_t location ) {
      auto prod = _producers.find( producer.value );
      const auto ct = current_time_point();

      eosio::public_key producer_key{};

      std::visit( [&](auto&& auth ) {
         if( auth.keys.size() == 1 ) {
            // if the producer_authority consists of a single key, use that key in the legacy producer_key field
            producer_key = auth.keys[0].key;
         }
      }, producer_authority );

      if ( prod != _producers.end() ) {
         _producers.modify( prod, producer, [&]( producer_info& info ){
            info.producer_key       = producer_key;
            info.is_active          = true;
            info.url                = url;
            info.location           = location;
            info.producer_authority.emplace( producer_authority );
            if ( info.last_claim_time == time_point() )
               info.last_claim_time = ct;
         });

         auto prod2 = _producers2.find( producer.value );
         if ( prod2 == _producers2.end() ) {
            _producers2.emplace( producer, [&]( producer_info2& info ){
               info.owner                     = producer;
               info.last_votepay_share_update = ct;
            });
            update_total_votepay_share( ct, 0.0, prod->total_votes );
            // When introducing the producer2 table row for the first time, the producer's votes must also be accounted for in the global total_producer_votepay_share at the same time.
         }
      } else {
         _producers.emplace( producer, [&]( producer_info& info ){
            info.owner              = producer;
            info.total_votes        = 0;
            info.producer_key       = producer_key;
            info.is_active          = true;
            info.url                = url;
            info.location           = location;
            info.last_claim_time    = ct;
            info.producer_authority.emplace( producer_authority );
         });
         _producers2.emplace( producer, [&]( producer_info2& info ){
            info.owner                     = producer;
            info.last_votepay_share_update = ct;
         });
      }

   }

   void system_contract::regproducer( const name& producer, const eosio::public_key& producer_key, const std::string& url, uint16_t location ) {
      require_auth( producer );
      check( url.size() < 512, "url too long" );

      register_producer( producer, convert_to_block_signing_authority( producer_key ), url, location );
   }

   void system_contract::regproducer2( const name& producer, const eosio::block_signing_authority& producer_authority, const std::string& url, uint16_t location ) {
      require_auth( producer );
      check( url.size() < 512, "url too long" );

      std::visit( [&](auto&& auth ) {
         check( auth.is_valid(), "invalid producer authority" );
      }, producer_authority );

      register_producer( producer, producer_authority, url, location );
   }

   void system_contract::unregprod( const name& producer ) {
      require_auth( producer );

      const auto& prod = _producers.get( producer.value, "producer not found" );
      _producers.modify( prod, same_payer, [&]( producer_info& info ){
         info.deactivate();
      });
   }

   // TELOS BEGIN
   void system_contract::unregreason( const name& producer, std::string reason ) {
      check( reason.size() < 255, "The reason is too long. Reason should not have more than 255 characters.");
      require_auth( producer );

      const auto& prod = _producers.get( producer.value, "producer not found" );
      _producers.modify( prod, same_payer, [&]( producer_info& info ){
         info.deactivate();
         info.unreg_reason = reason;
      });
   }
   // TELOS END

   void system_contract::update_elected_producers( const block_timestamp& block_time ) {
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<"prototalvote"_n>();

      // TELOS BEGIN
      uint32_t totalActiveVotedProds = uint32_t(std::distance(idx.begin(), idx.end()));
      totalActiveVotedProds = totalActiveVotedProds > MAX_PRODUCERS ? MAX_PRODUCERS : totalActiveVotedProds;

      std::vector< producer_location_pair > active_producers, top_producers;
      active_producers.reserve(totalActiveVotedProds);

      for( auto it = idx.cbegin(); it != idx.cend() && active_producers.size() < totalActiveVotedProds /*TELOS*/ && 0 < it->total_votes && it->active(); ++it ) {
         active_producers.emplace_back(
            eosio::producer_authority{
               .producer_name = it->owner,
               .authority     = it->get_producer_authority()
            },
            it->location
         );
      }

      if( active_producers.size() == 0 || active_producers.size() < _gstate.last_producer_schedule_size ) {
         return;
      }

      top_producers = check_rotation_state(active_producers, block_time);
      // TELOS END

      std::sort( top_producers.begin(), top_producers.end(), []( const producer_location_pair& lhs, const producer_location_pair& rhs ) {
         //return lhs.first.producer_name < rhs.first.producer_name; // sort by producer name
         return lhs.second < rhs.second; // TELOS sort by location
      } );

      std::vector<eosio::producer_authority> producers;

      producers.reserve(top_producers.size());
      for( auto& item : top_producers )
         producers.push_back( std::move(item.first) );

      // TELOS BEGIN
      auto schedule_version = set_proposed_producers(producers);
      if (schedule_version >= 0) {
        print("\n**new schedule was proposed**");

        _gstate.last_proposed_schedule_update = block_time;

        _gschedule_metrics.producers_metric.erase( _gschedule_metrics.producers_metric.begin(), _gschedule_metrics.producers_metric.end());

        std::vector<producer_metric> psm;
        std::for_each(top_producers.begin(), top_producers.end(), [&psm](auto &tp) {
          auto bp_name = tp.first.producer_name;
          psm.emplace_back(producer_metric{ bp_name, 12 });
        });

        _gschedule_metrics.producers_metric = psm;

        _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>(top_producers.size());
      }
      // TELOS END
   }

   double stake2vote( int64_t staked ) {
      /// TODO subtract 2080 brings the large numbers closer to this decade
      double weight = int64_t( (current_time_point().sec_since_epoch() - (block_timestamp::block_timestamp_epoch / 1000)) / (seconds_per_day * 7) )  / double( 52 );
      return double(staked) * std::pow( 2, weight );
   }

   // TELOS BEGIN
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
   // TELOS END

   double system_contract::update_total_votepay_share( const time_point& ct,
                                                       double additional_shares_delta,
                                                       double shares_rate_delta )
   {
      double delta_total_votepay_share = 0.0;
      if( ct > _gstate3.last_vpay_state_update ) {
         delta_total_votepay_share = _gstate3.total_vpay_share_change_rate
                                       * double( (ct - _gstate3.last_vpay_state_update).count() / 1E6 );
      }

      delta_total_votepay_share += additional_shares_delta;
      if( delta_total_votepay_share < 0 && _gstate2.total_producer_votepay_share < -delta_total_votepay_share ) {
         _gstate2.total_producer_votepay_share = 0.0;
      } else {
         _gstate2.total_producer_votepay_share += delta_total_votepay_share;
      }

      if( shares_rate_delta < 0 && _gstate3.total_vpay_share_change_rate < -shares_rate_delta ) {
         _gstate3.total_vpay_share_change_rate = 0.0;
      } else {
         _gstate3.total_vpay_share_change_rate += shares_rate_delta;
      }

      _gstate3.last_vpay_state_update = ct;

      return _gstate2.total_producer_votepay_share;
   }

   double system_contract::update_producer_votepay_share( const producers_table2::const_iterator& prod_itr,
                                                          const time_point& ct,
                                                          double shares_rate,
                                                          bool reset_to_zero )
   {
      double delta_votepay_share = 0.0;
      if( shares_rate > 0.0 && ct > prod_itr->last_votepay_share_update ) {
         delta_votepay_share = shares_rate * double( (ct - prod_itr->last_votepay_share_update).count() / 1E6 ); // cannot be negative
      }

      double new_votepay_share = prod_itr->votepay_share + delta_votepay_share;
      _producers2.modify( prod_itr, same_payer, [&](auto& p) {
         if( reset_to_zero )
            p.votepay_share = 0.0;
         else
            p.votepay_share = new_votepay_share;

         p.last_votepay_share_update = ct;
      } );

      return new_votepay_share;
   }

   void system_contract::voteproducer( const name& voter_name, const name& proxy, const std::vector<name>& producers ) {
      // TELOS BEGIN
      require_auth( voter_name);
      /*
      if ( voter_name == "b1"_n ) {
         require_auth("eosio"_n);
      } else {
         require_auth( voter_name );
      }
      */
      // TELOS END

      vote_stake_updater( voter_name );
      update_votes( voter_name, proxy, producers, true );
      /* TELOS Remove requirement to vote 21 BPs or select a proxy to stake to REX
      auto rex_itr = _rexbalance.find( voter_name.value );
      if( rex_itr != _rexbalance.end() && rex_itr->rex_balance.amount > 0 ) {
         check_voting_requirement( voter_name, "voter holding REX tokens must vote for at least 21 producers or for a proxy" );
      }
      */
   }

   void system_contract::voteupdate( const name& voter_name ) {
      auto voter = _voters.find( voter_name.value );
      check( voter != _voters.end(), "no voter found" );

      int64_t new_staked = 0;

      updaterex(voter_name);

      // get rex bal
      auto rex_itr = _rexbalance.find( voter_name.value );
      if( rex_itr != _rexbalance.end() && rex_itr->rex_balance.amount > 0 ) {
         new_staked += rex_itr->vote_stake.amount;
      }
      del_bandwidth_table     del_tbl( get_self(), voter_name.value );

      auto del_itr = del_tbl.begin();
      while(del_itr != del_tbl.end()) {
         new_staked += del_itr->net_weight.amount + del_itr->cpu_weight.amount;
         del_itr++;
      }

      if( voter->staked != new_staked){
         // check if staked and new_staked are different and only
         _voters.modify( voter, same_payer, [&]( auto& av ) {
            av.staked = new_staked;
         });
      }

      update_votes(voter_name, voter->proxy, voter->producers, true);
   } // voteupdate


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

      // TELOS BEGIN
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
      // TELOS END
   }

   void system_contract::regproxy( const name& proxy, bool isproxy ) {
      // TELOS BEGIN
      // require_auth( proxy );
      check ( !isproxy, "proxy voting is disabled" );
      // TELOS END

      auto pitr = _voters.find( proxy.value );
      if ( pitr != _voters.end() ) {
         check( isproxy != pitr->is_proxy, "action has no effect" );
         check( !isproxy || !pitr->proxy, "account that uses a proxy is not allowed to become a proxy" );
         _voters.modify( pitr, same_payer, [&]( auto& p ) {
               p.is_proxy = isproxy;
            });
         // TELOS BEGIN
         // propagate_weight_change( *pitr );
         update_votes(pitr->owner, pitr->proxy, pitr->producers, true);
         // TELOS END
      } else {
         _voters.emplace( proxy, [&]( auto& p ) {
               p.owner  = proxy;
               p.is_proxy = isproxy;
            });
      }
   }

   void system_contract::propagate_weight_change( const voter_info& voter ) {
      check( !voter.proxy || !voter.is_proxy, "account registered as a proxy is not allowed to use a proxy" );
      // TELOS REPLACE BEGIN
      /*
      double new_weight = stake2vote( voter.staked );
      if ( voter.is_proxy ) {
         new_weight += voter.proxied_vote_weight;
      }

      /// don't propagate small changes (1 ~= epsilon)
      if ( fabs( new_weight - voter.last_vote_weight ) > 1 )  {
         if ( voter.proxy ) {
            auto& proxy = _voters.get( voter.proxy.value, "proxy not found" ); //data corruption
            _voters.modify( proxy, same_payer, [&]( auto& p ) {
                  p.proxied_vote_weight += new_weight - voter.last_vote_weight;
               }
            );
            propagate_weight_change( proxy );
         } else {
            auto delta = new_weight - voter.last_vote_weight;
            const auto ct = current_time_point();
            double delta_change_rate         = 0;
            double total_inactive_vpay_share = 0;
            for ( auto acnt : voter.producers ) {
               auto& prod = _producers.get( acnt.value, "producer not found" ); //data corruption
               const double init_total_votes = prod.total_votes;
               _producers.modify( prod, same_payer, [&]( auto& p ) {
                  p.total_votes += delta;
                  _gstate.total_producer_vote_weight += delta;
               });
               auto prod2 = _producers2.find( acnt.value );
               if ( prod2 != _producers2.end() ) {
                  const auto last_claim_plus_3days = prod.last_claim_time + microseconds(3 * useconds_per_day);
                  bool crossed_threshold       = (last_claim_plus_3days <= ct);
                  bool updated_after_threshold = (last_claim_plus_3days <= prod2->last_votepay_share_update);
                  // Note: updated_after_threshold implies cross_threshold

                  double new_votepay_share = update_producer_votepay_share( prod2,
                                                ct,
                                                updated_after_threshold ? 0.0 : init_total_votes,
                                                crossed_threshold && !updated_after_threshold // only reset votepay_share once after threshold
                                             );

                  if( !crossed_threshold ) {
                     delta_change_rate += delta;
                  } else if( !updated_after_threshold ) {
                     total_inactive_vpay_share += new_votepay_share;
                     delta_change_rate -= init_total_votes;
                  }
               }
            }

            update_total_votepay_share( ct, -total_inactive_vpay_share, delta_change_rate );
         }
      }
      _voters.modify( voter, same_payer, [&]( auto& v ) {
            v.last_vote_weight = new_weight;
         }
      );
      */
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
      // TELOS REPLACE END
   }

   // TELOS BEGIN
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
   // TELOS END

} /// namespace eosiosystem
