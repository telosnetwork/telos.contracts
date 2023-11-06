// TELOS BEGIN
#include <eosio/eosio.hpp>
#include <math.h>

using eosio::time_point;
using eosio::const_mem_fun;
using eosio::indexed_by;

namespace delphioracle {

  const eosio::time_point NULL_TIME_POINT = eosio::time_point(eosio::microseconds(0));


  enum class average_types: uint8_t {
      last_7_days = 0,
      last_14_days = 1,
      last_30_days = 2,
      last_45_days = 3,
      none = 255,
  };

  TABLE daily_datapoints {
    uint64_t id;
    uint64_t value;
    time_point timestamp;

    uint64_t primary_key() const {
      return id;
    }
    uint64_t by_timestamp() const {
      return timestamp.elapsed.to_seconds();
    }
    uint64_t by_value() const {
      return value;
    }
  };

  TABLE averages {
    uint64_t id;
    uint8_t type = get_type(average_types::none);
    uint64_t value = 0;
    time_point timestamp = NULL_TIME_POINT;
    uint64_t primary_key() const {
      return id;
    }
    uint64_t by_timestamp() const {
      return timestamp.elapsed.to_seconds();
    }

    static uint8_t get_type(average_types type) {
      return static_cast < uint8_t > (type);
    }
  };

  typedef eosio::multi_index<
    "dailydatapnt"_n,
    daily_datapoints,
    indexed_by<"value"_n, const_mem_fun<daily_datapoints, uint64_t, &daily_datapoints::by_value>>,
    indexed_by<"timestamp"_n, const_mem_fun<daily_datapoints,uint64_t, &daily_datapoints::by_timestamp>>
  > dailydatapointstable;

  typedef eosio::multi_index<
    "averages"_n,
    averages,indexed_by <"timestamp"_n, const_mem_fun<averages, uint64_t, &averages::by_timestamp>>
  > averagestable;

}
// TELOS END
