// TELOS BEGIN
// tf 1mil
// econdevfunds 500k
// rex 1mil
// wps 1mil
// bpay 1760000
// total 5260000 * 12 momths / 365 days = 172931.5068 per day (63.12mil per year)
// rounding up to ensure contract always can have a surplus beyond the total max of all payouts
// const uint64_t max_drawdown_amount = 173000;

// 60sec * 60min * 24hrs
const uint64_t daily_interval = 86400;

// 60sec * 30min
const uint64_t rex_interval = 1800;

const uint64_t max_econdev_amount = 0;

// 700k * 12 months / 365 days = 23013.69863
const uint64_t max_tf_amount = 23014;

// 400k * 12 months / 365 days = 13150
const uint64_t max_coredev_amount = 13150;

// 1.7mil * 12 months / 365 days / 24hrs / 2 (every half hour) = 1164.3835
const uint64_t max_rex_amount = 1165;

// 500k/mo for ignite a month
const uint64_t max_ignitegrants_amount = 16438;

// 1.7mil for fuel month
const uint64_t max_tlosfuel_amount = 55890;


#ifndef TESTER
#define VNAME eosio::name
#else
#define VNAME eosio::chain::name
#endif


static constexpr VNAME CORE_SYM_ACCOUNT = "eosio.token"_n;
// static constexpr symbol CORE_SYM = symbol("TLOS", 4);
static constexpr VNAME SYSTEM_ACCOUNT = "eosio"_n;

static constexpr VNAME TF_ACCOUNT = "tf"_n;
static constexpr VNAME ECONDEV_ACCOUNT = "econdevfunds"_n;
static constexpr VNAME COREDEV_ACCOUNT = "treasury.tcd"_n;
static constexpr VNAME EVM_ACCOUNT = "eosio.evm"_n;
static constexpr VNAME REX_ACCOUNT = "eosio.rex"_n;
static constexpr VNAME IGNITE_ACCOUNT = "ignitegrants"_n;
static constexpr VNAME FUEL_ACCOUNT = "tlosfuelfund"_n;
// TELOS END