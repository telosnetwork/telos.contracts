#pragma once

#include <cmath>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>

namespace eosio {
   // TODO external table with delphioracle contract
   enum class median_types : uint8_t {
      day = 0,
      week = 1,
      month = 2,
      current_week = 4,

      none = 255,
   };

   eosio::name delphioracle{"delphioracle"_n};

   struct [[eosio::table, eosio::contract("delphioracle")]] confpayment
   {
      double multiplier;
      double power;
      bool is_median_cached = false;
   };
   using median_config_payments_singleton = eosio::singleton<"confpayment"_n, confpayment>;

   struct [[eosio::table, eosio::contract("eosio.system")]] cachepayment
   {
      double median;
   };
   using median_cache_payments_singleton = eosio::singleton<"cachepayment"_n, cachepayment>;

   struct [[eosio::table, eosio::contract("delphioracle")]] medians 
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

   bool is_active_medians() {
      median_config_payments_singleton config_payments(delphioracle, delphioracle.value);
      return config_payments.exists();
   }
   
   double median_price(const eosio::name& self)
   {
      medianstable medians_table(delphioracle, name("tlosusd").value);
      median_cache_payments_singleton cache_payments(delphioracle, delphioracle.value);
      median_config_payments_singleton config_payments(delphioracle, delphioracle.value);

      if (config_payments.exists() && config_payments.get().is_median_cached)
      {
         if (cache_payments.exists())
            return cache_payments.get().median;

         return 0.f;   
      }

      uint64_t value = 0;
      uint64_t request_count = 0;
  
      for (auto itr = medians_table.begin(); itr != medians_table.end(); ++itr)
      {
         if (itr->type == medians::get_type(median_types::week))
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
         median_cache_payments_singleton cache_payments(self, self.value);
         cachepayment temp;
         auto res = cache_payments.get_or_create( self, temp );
         res.median = median;
         cache_payments.set( res, self );
      }

      return median;
   }

   int64_t calculate_payment(const eosio::name& self)
   {
      median_config_payments_singleton config_table(delphioracle, delphioracle.value);
      const auto& current_config = config_table.get();
      
      auto m_price = median_price(self) / 10000;
      // print("m_price=", m_price, "; multiplier=", current_config.multiplier, "; power=", current_config.power);

      auto payment_per_block = current_config.multiplier * pow(m_price, current_config.power);
      auto payment_per_30min = payment_per_block * 31.5;
      auto payment_per_day = payment_per_30min * 48;

      // print("; per_block=", payment_per_block, "; per_30min=", payment_per_30min, "; per_day=", payment_per_day, "; ");

      return int64_t(payment_per_day * 10000);
   } 
}
