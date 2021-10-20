#pragma once

namespace eosio {

   struct [[eosio::table, eosio::contract("eosio.system")]] config
   {
      double multiplier;
      double power;
      uint64_t interval;
      bool is_median_cached = false;
   };
   using singleton_config = eosio::singleton<"config"_n, config>;

   struct [[eosio::table, eosio::contract("eosio.system")]] mediancache
   {
      double median;
   };
   using median_cache_singleton = eosio::singleton<"mediancache"_n, mediancache>;

// TODO external table with delphioracle contract
   enum class median_types : uint8_t 
   {
      day = 0,
      week = 1,
      month = 2,
      none = 3,
   };

   struct [[eosio::table, eosio::contract("eosio.system")]] medians 
   {
      uint64_t   id;
      uint8_t    type;
      uint64_t   value;
      uint64_t   request_count;
      time_point timestamp;

      uint64_t primary_key() const { return id; }
      uint64_t by_timestamp() const { return timestamp.elapsed.to_seconds(); }

      static uint8_t get_type(median_types type) {
         return static_cast<uint8_t>(type);
      }
   };
    typedef eosio::multi_index<"medians"_n, medians,
      indexed_by<"timestamp"_n, const_mem_fun<medians, uint64_t, &medians::by_timestamp>>> medianstable;

    double median_price(const name& owner)
    {
       median_cache_singleton cache(owner, owner.value);
       singleton_config config(owner, owner.value);

       if (config.exists() && config.get().is_median_cached)
       {
          return cache.get().median;
       }

       uint64_t value = 0;
       uint64_t request_count = 0;

       medianstable medians_table("delphioracle"_n, name("tlosusd").value);
       for (auto itr = medians_table.begin(); itr != medians_table.end(); ++itr)
       {
          if (itr->type == medians::get_type(median_types::day))
          {
             value += itr->value;
             request_count += itr->request_count;
          }
       }

       double median = 0.f;
       if (request_count > 0)
       {
          median = value / request_count;
       }

       {
          mediancache median_cache;
          auto data = cache.get_or_create(owner, median_cache);
          data.median = median;
          cache.set(data, owner);
       }

       return median;
    }
}
