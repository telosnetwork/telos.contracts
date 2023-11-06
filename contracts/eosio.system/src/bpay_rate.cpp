// TELOS BEGIN
#include <eosio.system/bpay_rate.hpp>

using eosio::asset;

namespace eosiosystem {
    double compute_bpay_rate(uint64_t tlos_price, asset total_telos_supply) {
        const double MULTIPLIER_CONSTANT = 8.34;
        const double POWER_OF_CONSTANT = -0.516;
        const double ACTIVE_BP_COUNT = 21;
        const double STANDBY_BP_COUNT = 21;

        // 100k divided by (30-minutes divided by the Number-of-minutes-in-a-year)
        // 100k / 0.00005707762557
        // This was pulled out of the bpay_rate formula
        const double _100K_DIVIDED_BY_TIME_PERIOD = 1752000000;

        const double raw_tlos_price = double(tlos_price) / 100;
        const double tlos_per_bp = MULTIPLIER_CONSTANT * pow((double) raw_tlos_price, POWER_OF_CONSTANT);
        const double tlos_in_30_mins = tlos_per_bp * (ACTIVE_BP_COUNT + 0.5 * STANDBY_BP_COUNT);

        double total_telos_supply_double = double(total_telos_supply.amount) / pow(10.0, total_telos_supply.symbol.precision());

        const double bp_rate = round(
            (_100K_DIVIDED_BY_TIME_PERIOD* tlos_in_30_mins) /
            total_telos_supply_double
        );

        return bp_rate;
    }
}
// TELOS END
