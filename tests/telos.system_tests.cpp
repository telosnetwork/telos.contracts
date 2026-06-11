#include <boost/test/unit_test.hpp>
#include <cmath>
#include <set>

#include "eosio.system_tester.hpp"

#define MAX_PRODUCERS 35

using namespace eosio_system;

BOOST_AUTO_TEST_SUITE(telos_system_tests)

BOOST_FIXTURE_TEST_CASE(producer_onblock_check, eosio_system_tester) try {

   // min_activated_stake_part = 28'570'987'0000;
   const asset half_min_activated_stake = core_sym::from_string("14285493.5000");
   const asset quarter_min_activated_stake = core_sym::from_string("7142746.7500");
   const asset eight_min_activated_stake = core_sym::from_string("3571373.3750");

   const asset large_asset = core_sym::from_string("80.0000");
   create_account_with_resources( "producvotera"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( "producvoterb"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( "producvoterc"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );

   // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
   std::vector<account_name> producer_names;
   producer_names.reserve('z' - 'a' + 1);
   const std::string root("defproducer");
   for ( char c = 'a'; c <= 'z'; ++c ) {
      producer_names.emplace_back(root + std::string(1, c));
   }
   setup_producer_accounts(producer_names);

   for (auto a:producer_names)
      regproducer(a);

   BOOST_REQUIRE_EQUAL(0, get_producer_info( producer_names.front() )["total_votes"].as<double>());
   BOOST_REQUIRE_EQUAL(0, get_producer_info( producer_names.back() )["total_votes"].as<double>());

   transfer(config::system_account_name, "producvotera", half_min_activated_stake, config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvotera", quarter_min_activated_stake, quarter_min_activated_stake ));
   BOOST_REQUIRE_EQUAL(success(), vote( "producvotera"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+10)));

   BOOST_CHECK_EQUAL( wasm_assert_msg( "cannot undelegate bandwidth until the chain is activated (1,000,000 blocks produced)" ),
                      unstake( "producvotera", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );

   // give a chance for everyone to produce blocks
   {
      produce_blocks(21 * 12);

      bool all_21_produced = true;
      for (uint32_t i = 0; i < 21; ++i) {
         if (0 == get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            all_21_produced= false;
         }
      }
      bool rest_didnt_produce = true;
      for (uint32_t i = 21; i < producer_names.size(); ++i) {
         if (0 < get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE_EQUAL(false, all_21_produced);
      BOOST_REQUIRE_EQUAL(true, rest_didnt_produce);
   }

   {
      const char* claimrewards_activation_error_message = "cannot claim rewards until the chain is activated (1,000,000 blocks produced)";
      BOOST_CHECK_EQUAL(0, get_global_state()["total_unpaid_blocks"].as<uint32_t>());
      BOOST_REQUIRE_EQUAL(wasm_assert_msg( claimrewards_activation_error_message ),
                          push_action(producer_names.front(), "claimrewards"_n, mvo()("owner", producer_names.front())));
      BOOST_REQUIRE_EQUAL(0, get_balance(producer_names.front()).get_amount());
      BOOST_REQUIRE_EQUAL(wasm_assert_msg( claimrewards_activation_error_message ),
                          push_action(producer_names.back(), "claimrewards"_n, mvo()("owner", producer_names.back())));
      BOOST_REQUIRE_EQUAL(0, get_balance(producer_names.back()).get_amount());
   }


   // stake across 15% boundary
   transfer(config::system_account_name, "producvoterb", half_min_activated_stake, config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvoterb", quarter_min_activated_stake, quarter_min_activated_stake));
   transfer(config::system_account_name, "producvoterc", quarter_min_activated_stake, config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvoterc", eight_min_activated_stake, eight_min_activated_stake));

   // 21 => no rotation , 22 => yep, rotation
   BOOST_REQUIRE_EQUAL(success(), vote( "producvoterb"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+22)));
   BOOST_REQUIRE_EQUAL(success(), vote( "producvoterc"_n, vector<account_name>(producer_names.begin(), producer_names.end())));

   activate_network();

   // give a chance for everyone to produce blocks
   {
      produce_blocks(21 * 12);
      // for (uint32_t i = 0; i < producer_names.size(); ++i) {
      //    std::cout<<"["<<producer_names[i]<<"]: "<<get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()<<std::endl;
      // }

      bool all_21_produced = 0 < get_producer_info(producer_names[21])["unpaid_blocks"].as<uint32_t>();
      for (uint32_t i = 1; i < 21; ++i) {
         if (0 == get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            all_21_produced= false;
         }
      }
      bool rest_didnt_produce = 0 == get_producer_info(producer_names[0])["unpaid_blocks"].as<uint32_t>();
      for (uint32_t i = 22; i < producer_names.size(); ++i) {
         if (0 < get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE_EQUAL(true, all_21_produced);
      BOOST_REQUIRE_EQUAL(true, rest_didnt_produce);


      const char* claimrewards_payment_missing = "No payment exists for account";
      BOOST_REQUIRE_EQUAL(wasm_assert_msg( claimrewards_payment_missing ),
                          push_action(producer_names.front(), "claimrewards"_n, mvo()("owner", producer_names.front())));

      //give time for snapshot
      produce_block(fc::minutes(30));
      produce_blocks(1);

      BOOST_REQUIRE_EQUAL(success(),
                          push_action(producer_names[1], "claimrewards"_n, mvo()("owner", producer_names[1])));
      BOOST_REQUIRE(0 < get_balance(producer_names[1]).get_amount());
   }

   BOOST_CHECK_EQUAL( success(), unstake( "producvotera", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(rotation_clears_when_standby_moves_into_top_21, eosio_system_tester) try {
   const asset large_asset = core_sym::from_string("80.0000");
   create_account_with_resources( "rotvoter1111"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );

   std::vector<account_name> producer_names;
   producer_names.reserve(22);
   const std::string root("rprod");
   for(uint8_t i = 0; i < 22; i++) {
      producer_names.emplace_back(root + toBase31(i));
   }

   setup_producer_accounts(producer_names);
   for (const auto& p: producer_names) {
      BOOST_REQUIRE_EQUAL(success(), regproducer(p));
   }

   transfer(config::system_account_name, "rotvoter1111"_n, core_sym::from_string("400000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("rotvoter1111"_n, core_sym::from_string("150000000.0000"), core_sym::from_string("150000000.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote("rotvoter1111"_n, producer_names));

   activate_network();
   produce_blocks(250);

   auto rotation = get_rotation_state();
   const name bp_out = rotation["bp_currently_out"].as<name>();
   const name sbp_in = rotation["sbp_currently_in"].as<name>();
   BOOST_REQUIRE_NE(name(0), bp_out);
   BOOST_REQUIRE_NE(name(0), sbp_in);

   name removed_producer = name(0);
   for (const auto& p: producer_names) {
      if (p != bp_out && p != sbp_in) {
         removed_producer = p;
         break;
      }
   }
   BOOST_REQUIRE_NE(name(0), removed_producer);
   BOOST_REQUIRE_EQUAL(success(), push_action(removed_producer, "unregprod"_n, mvo()("producer", removed_producer)));

   produce_block(fc::minutes(2));
   produce_blocks(1);

   rotation = get_rotation_state();
   BOOST_REQUIRE_EQUAL(name(0), rotation["bp_currently_out"].as<name>());
   BOOST_REQUIRE_EQUAL(name(0), rotation["sbp_currently_in"].as<name>());

   const auto active_schedule = control->head_block_state_legacy()->active_schedule.producers;
   std::set<name> scheduled_producers;
   for (const auto& producer: active_schedule) {
      scheduled_producers.insert(producer.producer_name);
   }

   BOOST_REQUIRE_EQUAL(active_schedule.size(), scheduled_producers.size());
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(producer_pay, eosio_system_tester, * boost::unit_test::tolerance(1e-10)) try {
   const double usecs_per_year  = 52 * 7 * 24 * 3600 * 1000000ll;
   const double secs_per_year   = 52 * 7 * 24 * 3600;

   const asset large_asset = core_sym::from_string("80.0000");
   create_account_with_resources( "defproducera"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( "defproducerb"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( "defproducerc"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );

   create_account_with_resources( "producvotera"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( "producvoterb"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );

   BOOST_REQUIRE_EQUAL(success(), regproducer("defproducera"_n));
   auto prod = get_producer_info( "defproducera"_n );
   BOOST_REQUIRE_EQUAL("defproducera", prod["owner"].as_string());
   BOOST_REQUIRE_EQUAL(0, prod["total_votes"].as_double());

   transfer(config::system_account_name, "producvotera", core_sym::from_string("400000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvotera", core_sym::from_string("100000000.0000"), core_sym::from_string("100000000.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote( "producvotera"_n, { "defproducera"_n }));

   produce_blocks((1000 - get_global_state()["block_num"].as<uint32_t>()) + 1);
   transfer(name("eosio"), name("exrsrv.tf"), core_sym::from_string("400000000.0000"), config::system_account_name);
   {
      const uint32_t last_claim_time = get_global_state()["last_claimrewards"].as<uint32_t>();
      produce_blocks(3598);
      BOOST_REQUIRE_EQUAL(control->active_producers().version, 1);
      BOOST_REQUIRE_EQUAL(last_claim_time, get_global_state()["last_claimrewards"].as<uint32_t>());

      const fc::variant     initial_global_state      = get_global_state();
      const uint64_t initial_claim_time        = microseconds_since_epoch_of_iso_string( initial_global_state["last_pervote_bucket_fill"] );
      const int64_t  initial_pervote_bucket    = initial_global_state["pervote_bucket"].as<int64_t>();

      const int64_t  initial_perblock_bucket   = initial_global_state["perblock_bucket"].as<int64_t>();
      const int64_t  initial_savings           = get_balance("eosio.saving"_n).get_amount();
      const uint32_t initial_tot_unpaid_blocks = initial_global_state["total_unpaid_blocks"].as<uint32_t>();

      prod = get_producer_info("defproducera");
      const uint32_t unpaid_blocks = prod["unpaid_blocks"].as<uint32_t>();
      const bool is_active = prod["is_active"].as<bool>();

      BOOST_REQUIRE(is_active);
      BOOST_REQUIRE(1 < unpaid_blocks);

      BOOST_REQUIRE_EQUAL(initial_tot_unpaid_blocks, unpaid_blocks);

      const asset initial_supply  = get_token_supply();
      const asset initial_balance = get_balance("defproducera"_n);
      const asset initial_wps_balance = get_balance("works.decide"_n);
      const asset initial_bpay_balance = get_balance("eosio.bpay"_n);
      const asset initial_tedp_balance = get_balance("exrsrv.tf"_n);

      BOOST_REQUIRE_EQUAL(wasm_assert_msg("No payment exists for account"),
                          push_action("defproducera"_n, "claimrewards"_n, mvo()("owner", "defproducera")));

      produce_blocks();

      const fc::variant pay_rate_info = get_payrate_info();
      const uint64_t bpay_rate = pay_rate_info["bpay_rate"].as<uint64_t>();;
      const uint64_t worker_amount = pay_rate_info["worker_amount"].as<uint64_t>();

      const auto     global_state                  = get_global_state();
      const uint64_t claim_time                    = microseconds_since_epoch_of_iso_string( global_state["last_pervote_bucket_fill"] );
      const int64_t  pervote_bucket                = global_state["pervote_bucket"].as<int64_t>();
      const int64_t  perblock_bucket               = global_state["perblock_bucket"].as<int64_t>();
      const time_point last_pervote_bucket_fill    = global_state["last_pervote_bucket_fill"].as<time_point>();
      const int64_t  savings                       = get_balance("eosio.saving"_n).get_amount();
      const uint32_t tot_unpaid_blocks             = global_state["total_unpaid_blocks"].as<uint32_t>();

      auto usecs_between_fills = claim_time - initial_claim_time;
      int32_t secs_between_fills = usecs_between_fills / 1000000;

      // Replicate contract's dynamic price-based calculation
      // In test environment, TLOS price defaults to 1 (matches contract fallback when oracle data unavailable)
      uint64_t tlos_price = 1;  // Default price (matches contract's get_telos_average_price() fallback)
      auto to_workers = static_cast<int64_t>((12 * double(worker_amount) * double(usecs_between_fills)) / double(usecs_per_year));
      double bp_pay_per_month = std::min((double(189000) * std::pow(tlos_price/10000.0,-0.516)),double(315000)) * 10000;
      auto to_producers = static_cast<int64_t>((bp_pay_per_month * 12 * double(usecs_between_fills)) / double(usecs_per_year));

      prod = get_producer_info("defproducera");
      asset to_bpay = asset(to_producers, symbol{CORE_SYM});
      asset to_wps = asset(to_workers, symbol{CORE_SYM});
      asset new_tokens = asset(to_workers + to_producers, symbol{CORE_SYM});

      BOOST_REQUIRE_EQUAL(0, prod["unpaid_blocks"].as<uint32_t>());
      BOOST_REQUIRE_EQUAL(0, tot_unpaid_blocks);
      BOOST_REQUIRE_EQUAL(get_balance("eosio.bpay"_n), initial_bpay_balance + to_bpay);
      BOOST_REQUIRE_EQUAL(get_balance("works.decide"_n), initial_wps_balance + to_wps);
      const asset supply  = get_token_supply();
      const asset balance = get_balance("defproducera"_n);
      BOOST_REQUIRE_EQUAL(supply, initial_supply);
      BOOST_REQUIRE_EQUAL(get_balance("exrsrv.tf"_n), initial_tedp_balance - new_tokens);
      const asset payment = get_payment_info("defproducera"_n)["pay"].as<asset>();
      // Allow small tolerance (1 unit = 0.0001 TLOS) for floating point rounding differences
      int64_t diff = payment.get_amount() - to_bpay.get_amount();
      BOOST_REQUIRE_MESSAGE(diff >= -1 && diff <= 1, 
                            "Payment amount differs from expected by more than 1 unit. Payment: " + 
                            payment.to_string() + ", Expected: " + to_bpay.to_string());
      const asset initial_prod_balance = get_balance("defproducera"_n);
      push_action("defproducera"_n, "claimrewards"_n, mvo()("owner", "defproducera"));

      BOOST_REQUIRE_EQUAL(get_balance("defproducera"_n), initial_prod_balance + payment);
      BOOST_REQUIRE_EQUAL(claim_time, microseconds_since_epoch_of_iso_string( prod["last_claim_time"] ));

      BOOST_REQUIRE(get_payment_info("defproducera"_n).is_null());
   }
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(multi_producer_pay, eosio_system_tester, * boost::unit_test::tolerance(1e-10)) try {
   const double usecs_per_year  = 52 * 7 * 24 * 3600 * 1000000ll;
   const double secs_per_year   = 52 * 7 * 24 * 3600;
   const int producer_amount = MAX_PRODUCERS;

   std::vector<account_name> producer_names;
   {
      producer_names.reserve(producer_amount);
      const std::string root("tprod");
      for(uint8_t i = 0; i < producer_amount; i++) {
         name p = name(root + toBase31(i));
         producer_names.emplace_back(p);
      }

      setup_producer_accounts(producer_names);
      for ( const auto& p: producer_names ) {
         BOOST_REQUIRE_EQUAL( success(), regproducer(p) );
         BOOST_TEST_REQUIRE( 0 == get_producer_info(p)["total_votes"].as<double>() );
      }
      std::sort(producer_names.begin(), producer_names.end());
   }

   const asset large_asset = core_sym::from_string("80.0000");

   create_account_with_resources( "producvotera"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( "producvoterb"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );                                                                            

   auto prod = get_producer_info(producer_names[0]);
   BOOST_REQUIRE_EQUAL(producer_names[0].to_string(), prod["owner"].as_string());
   BOOST_REQUIRE_EQUAL(0, prod["total_votes"].as_double());
   // TODO: INCREASE VOTER As stake
   transfer(config::system_account_name, "producvotera", core_sym::from_string("400000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvotera", core_sym::from_string("150000000.0000"), core_sym::from_string("150000000.0000")));
   transfer(config::system_account_name, "producvoterb", core_sym::from_string("400000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvoterb", core_sym::from_string("100000000.0000"), core_sym::from_string("100000000.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote( "producvotera"_n, vector<name>( producer_names.begin(), producer_names.begin() + 21 )));
   BOOST_REQUIRE_EQUAL(success(), vote( "producvoterb"_n, vector<name>( producer_names.begin() + 21, producer_names.end() )));

   produce_blocks();

   for ( const auto& p: producer_names )
       BOOST_TEST_REQUIRE( get_producer_info(p)["total_votes"].as<double>() != 0 );

   produce_blocks((1000 - get_global_state()["block_num"].as<uint32_t>()) + 1);\
   {
      const uint32_t last_claim_time = get_global_state()["last_claimrewards"].as<uint32_t>();
      produce_blocks(3598);
      BOOST_REQUIRE_EQUAL(control->active_producers().version, 1);
      BOOST_REQUIRE_EQUAL(last_claim_time, get_global_state()["last_claimrewards"].as<uint32_t>());

      const fc::variant initial_global_state = get_global_state();
      const uint64_t initial_claim_time        = microseconds_since_epoch_of_iso_string( initial_global_state["last_pervote_bucket_fill"] );
      const int64_t  initial_pervote_bucket    = initial_global_state["pervote_bucket"].as<int64_t>();

      const int64_t  initial_perblock_bucket   = initial_global_state["perblock_bucket"].as<int64_t>();
      const int64_t  initial_savings           = get_balance("works.decide"_n).get_amount();
      const uint32_t initial_tot_unpaid_blocks = initial_global_state["total_unpaid_blocks"].as<uint32_t>();

      // TODO: check why this is off by three
      BOOST_REQUIRE_EQUAL(initial_global_state["total_unpaid_blocks"], 3595);

      const asset initial_supply  = get_token_supply();
      const asset initial_balance = get_balance(producer_names[0]);
      const asset initial_wps_balance = get_balance("works.decide"_n);
      const asset initial_bpay_balance = get_balance("eosio.bpay"_n);

      BOOST_REQUIRE_EQUAL(wasm_assert_msg("No payment exists for account"),
                          push_action(producer_names[0], "claimrewards"_n, mvo()("owner", producer_names[0])));

      auto comparator = [&](fc::variant a, fc::variant b) -> bool {
         return a["total_votes"].as<double>() > b["total_votes"].as<double>();
      };

      produce_blocks();

      const fc::variant pay_rate_info = get_payrate_info();
      const uint64_t bpay_rate = pay_rate_info["bpay_rate"].as<uint64_t>();;
      const uint64_t worker_amount = pay_rate_info["worker_amount"].as<uint64_t>();

      const auto     global_state                  = get_global_state();
      const uint64_t claim_time                    = microseconds_since_epoch_of_iso_string( global_state["last_pervote_bucket_fill"] );
      const int64_t  pervote_bucket                = global_state["pervote_bucket"].as<int64_t>();
      const int64_t  perblock_bucket               = global_state["perblock_bucket"].as<int64_t>();
      const time_point last_pervote_bucket_fill    = global_state["last_pervote_bucket_fill"].as<time_point>();
      const int64_t  savings                       = get_balance("works.decide"_n).get_amount();
      const uint32_t tot_unpaid_blocks             = global_state["total_unpaid_blocks"].as<uint32_t>();

      auto usecs_between_fills = claim_time - initial_claim_time;
      int32_t secs_between_fills = usecs_between_fills / 1000000;

      // Replicate contract's dynamic price-based calculation
      // In test environment, TLOS price defaults to 1 (matches contract fallback when oracle data unavailable)
      uint64_t tlos_price = 1;  // Default price (matches contract's get_telos_average_price() fallback)
      auto to_workers = static_cast<int64_t>((12 * double(worker_amount) * double(usecs_between_fills)) / double(usecs_per_year));
      double bp_pay_per_month = std::min((double(189000) * std::pow(tlos_price/10000.0,-0.516)),double(315000)) * 10000;
      auto to_producers = static_cast<int64_t>((bp_pay_per_month * 12 * double(usecs_between_fills)) / double(usecs_per_year));

      asset to_bpay = asset(to_producers, symbol{CORE_SYM});
      asset to_wps = asset(to_workers, symbol{CORE_SYM});
      asset new_tokens = asset(to_workers + to_producers, symbol{CORE_SYM});

      vector<fc::variant> producer_infos;
      auto i = 0;
      for(const name &p : producer_names)
         producer_infos.emplace_back(get_producer_info(p));

      std::sort(producer_infos.begin(), producer_infos.end(), comparator);
      BOOST_REQUIRE_EQUAL(get_balance("eosio.bpay"_n), initial_bpay_balance + to_bpay);

      for(const fc::variant &prod_info : producer_infos) {
         BOOST_REQUIRE(!prod_info.is_null());
      }

      // Count active producers (matching contract logic)
      uint32_t activecount = 0;
      for (const auto &prod : producer_infos) {
         if (prod["is_active"].as<bool>() && activecount < MAX_PRODUCERS) {
            activecount++;
         } else {
            break;
         }
      }

      // Calculate sum_of_multipliers using contract's formula
      double sum_of_multipliers = activecount <= 21 
         ? ((activecount / 2.0) * (2.0 * 1.2 - (activecount - 1) * 0.02) * 2.0) 
         : (42.0 + ((activecount - 21) / 2.0) * (2.0 * 1.2 - (activecount - 22) * 0.02));

      // Calculate shareValue using perblock_bucket (which equals to_producers after snapshot)
      auto shareValue = to_producers / sum_of_multipliers;

      // Collect all actual and expected payments
      std::vector<std::pair<name, asset>> actual_payments;
      std::vector<asset> expected_payments;

      int producer_count = 0;
      int32_t index = 0;
      for(const auto &prod : producer_infos) {
         if(producer_count < producer_amount && prod["is_active"].as<bool>()) {
            index++;
            BOOST_REQUIRE_EQUAL(0, prod["unpaid_blocks"].as<uint32_t>());
            const fc::variant payout_info = get_payment_info(prod["owner"].as<name>());
            BOOST_REQUIRE(!payout_info.is_null());
            const asset payment = payout_info["pay"].as<asset>();
            actual_payments.push_back({prod["owner"].as<name>(), payment});

            // Calculate expected payment using contract's tiered multiplier formula
            int64_t expected_pay = 0;
            if (index <= 21) {
               // Active BPs: shareValue * 2 * ((122-2*index)/100.0)
               expected_pay = static_cast<int64_t>(shareValue * 2.0 * ((122.0 - 2.0 * index) / 100.0));
            } else if (index >= 22 && index <= MAX_PRODUCERS) {
               // Standby BPs: shareValue * ((164-2*index)/100.0)
               expected_pay = static_cast<int64_t>(shareValue * ((164.0 - 2.0 * index) / 100.0));
            }
            expected_payments.push_back(asset(expected_pay, symbol{CORE_SYM}));
         } else {
            BOOST_REQUIRE_EQUAL(0, prod["unpaid_blocks"].as<uint32_t>());
            const asset balance = get_balance(prod["owner"].as<name>());
            BOOST_REQUIRE(get_payment_info(prod["owner"].as<name>()).is_null());
            BOOST_REQUIRE_EQUAL(wasm_assert_msg("No payment exists for account"),
                          push_action(prod["owner"].as<name>(), "claimrewards"_n, mvo()("owner", prod["owner"].as<name>())));
            BOOST_REQUIRE_EQUAL(get_balance(prod["owner"].as<name>()), balance);
         }
         producer_count++;
      }

      // Sort both actual and expected payments by amount (descending)
      std::sort(actual_payments.begin(), actual_payments.end(), 
                [](const std::pair<name, asset>& a, const std::pair<name, asset>& b) {
                   return a.second.get_amount() > b.second.get_amount();
                });
      std::sort(expected_payments.begin(), expected_payments.end(),
                [](const asset& a, const asset& b) {
                   return a.get_amount() > b.get_amount();
                });

      // Compare sorted payments
      BOOST_REQUIRE_EQUAL(actual_payments.size(), expected_payments.size());
      for(size_t i = 0; i < actual_payments.size(); ++i) {
         BOOST_REQUIRE_EQUAL(actual_payments[i].second, expected_payments[i]);
      }

      // Claim rewards for all producers
      for(const auto& payment_pair : actual_payments) {
         const asset balance = get_balance(payment_pair.first);
         push_action(payment_pair.first, "claimrewards"_n, mvo()("owner", payment_pair.first));
         BOOST_REQUIRE_EQUAL(get_balance(payment_pair.first), balance + payment_pair.second);
         auto prod_info = get_producer_info(payment_pair.first);
         BOOST_REQUIRE_EQUAL(claim_time, microseconds_since_epoch_of_iso_string( prod_info["last_claim_time"] ));
      }

      BOOST_REQUIRE_EQUAL(0, tot_unpaid_blocks);

      BOOST_REQUIRE_EQUAL(get_balance("works.decide"_n), initial_wps_balance + to_wps);
      const asset supply  = get_token_supply();
      BOOST_REQUIRE_EQUAL(supply, initial_supply + new_tokens);
   }
} FC_LOG_AND_RETHROW()

// Regression for the membership-based schedule metrics comparison: when two
// producers swap location values the proposed schedule ORDER changes (it is
// sorted by location) but membership does not. The schedule metrics vector
// must NOT be rebuilt in that case — a rebuild would wipe in-flight
// missed-block counters. The old metrics order is the fingerprint: if the
// vector were rebuilt it would be stored in the new (swapped) order.
BOOST_FIXTURE_TEST_CASE(schedule_metrics_survive_location_swap, eosio_system_tester) try {
   const asset large_asset = core_sym::from_string("80.0000");
   create_account_with_resources( "locvoter1111"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );

   std::vector<account_name> producer_names;
   std::map<name, uint16_t> locations;
   const std::string root("lprod");
   for(uint8_t i = 0; i < 8; i++) {
      name p = name(root + toBase31(i));
      producer_names.emplace_back(p);
      locations[p] = 10 * (i + 1);
   }
   setup_producer_accounts(producer_names);
   for (const auto& p: producer_names) {
      BOOST_REQUIRE_EQUAL(success(), push_action(p, "regproducer"_n, mvo()
                          ("producer", p)
                          ("producer_key", get_public_key(p, "active"))
                          ("url", "")
                          ("location", locations[p])));
   }

   transfer(config::system_account_name, "locvoter1111"_n, core_sym::from_string("400000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("locvoter1111"_n, core_sym::from_string("150000000.0000"), core_sym::from_string("150000000.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote("locvoter1111"_n, producer_names));

   activate_network();
   produce_blocks(300);

   auto metrics_before = get_gmetrics_state();
   BOOST_REQUIRE(!metrics_before.is_null());
   auto rows_before = metrics_before["producers_metric"].get_array();
   BOOST_REQUIRE_EQUAL(rows_before.size(), 8u);

   // swap the locations of the producers at metric positions 1 and 2
   const name p1 = rows_before[1]["bp_name"].as<name>();
   const name p2 = rows_before[2]["bp_name"].as<name>();
   BOOST_REQUIRE_EQUAL(success(), push_action(p1, "regproducer"_n, mvo()
                       ("producer", p1)("producer_key", get_public_key(p1, "active"))
                       ("url", "")("location", locations[p2])));
   BOOST_REQUIRE_EQUAL(success(), push_action(p2, "regproducer"_n, mvo()
                       ("producer", p2)("producer_key", get_public_key(p2, "active"))
                       ("url", "")("location", locations[p1])));

   // cross at least one update_elected_producers() proposal and let the
   // reordered schedule activate
   produce_block(fc::minutes(2));
   produce_blocks(300);

   // the reordered schedule must have actually activated (i.e. a real,
   // successful proposal happened)...
   const auto active_schedule = control->head_block_state_legacy()->active_schedule.producers;
   BOOST_REQUIRE_EQUAL(active_schedule.size(), 8u);
   int pos1 = -1, pos2 = -1;
   for (size_t i = 0; i < active_schedule.size(); ++i) {
      if (active_schedule[i].producer_name == p1) pos1 = int(i);
      if (active_schedule[i].producer_name == p2) pos2 = int(i);
   }
   BOOST_REQUIRE(pos1 >= 0 && pos2 >= 0);
   BOOST_REQUIRE_LT(pos2, pos1); // order flipped by the location swap

   // ...but the schedule metrics vector must NOT have been rebuilt: same
   // membership, and crucially still in the ORIGINAL order
   auto metrics_after = get_gmetrics_state();
   auto rows_after = metrics_after["producers_metric"].get_array();
   BOOST_REQUIRE_EQUAL(rows_after.size(), rows_before.size());
   for (size_t i = 0; i < rows_before.size(); ++i) {
      BOOST_REQUIRE_EQUAL(rows_before[i]["bp_name"].as<name>(), rows_after[i]["bp_name"].as<name>());
   }
} FC_LOG_AND_RETHROW()

// End-to-end missed-block autokick: a scheduled producer that stops producing
// must accumulate missed blocks across rotation cycles, get kicked once the
// threshold is crossed, and have lifetime_missed_blocks counted exactly once
// (regression for the double-count where update_missed_blocks_per_rotation
// added missed_blocks_per_rotation before kick() added it again).
BOOST_FIXTURE_TEST_CASE(missed_block_autokick_threshold_and_lifetime, eosio_system_tester) try {
   const asset large_asset = core_sym::from_string("80.0000");
   create_account_with_resources( "kickvoter111"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );

   std::vector<account_name> producer_names;
   const std::string root("kprod");
   for(uint8_t i = 0; i < 25; i++) {
      producer_names.emplace_back(root + toBase31(i));
   }
   setup_producer_accounts(producer_names);
   for (const auto& p: producer_names) {
      BOOST_REQUIRE_EQUAL(success(), regproducer(p));
   }

   transfer(config::system_account_name, "kickvoter111"_n, core_sym::from_string("400000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("kickvoter111"_n, core_sym::from_string("150000000.0000"), core_sym::from_string("150000000.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote("kickvoter111"_n, producer_names));

   activate_network();
   // fund the inflation offset account so hourly claimrewards snapshots in
   // onblock succeed over the long block range below
   transfer(name("eosio"), name("exrsrv.tf"), core_sym::from_string("400000000.0000"), config::system_account_name);
   produce_blocks(300);

   auto metrics = get_gmetrics_state();
   BOOST_REQUIRE(!metrics.is_null());
   auto rows = metrics["producers_metric"].get_array();
   BOOST_REQUIRE_EQUAL(rows.size(), 21u);

   // pick a victim that is in the schedule and not part of the rotation pair
   auto rotation = get_rotation_state();
   const name bp_out = rotation["bp_currently_out"].as<name>();
   const name sbp_in = rotation["sbp_currently_in"].as<name>();
   name victim = name(0);
   for (const auto& r: rows) {
      const name n = r["bp_name"].as<name>();
      if (n != bp_out && n != sbp_in) { victim = n; break; }
   }
   BOOST_REQUIRE_NE(victim, name(0));

   // threshold for a 21-producer schedule over the 12h rotation window:
   // 0.15 * 2*43200/(21-1) = 648 missed blocks
   const uint32_t threshold = 648;

   // produce blocks, skipping the victim's production windows entirely
   uint32_t last_missed = 0;
   bool kicked = false;
   uint32_t blocks_done = 0;
   const uint32_t max_blocks = 20000;
   while (blocks_done < max_blocks) {
      if (control->pending_block_producer() == victim) {
         produce_block(fc::milliseconds(500 * 12)); // skip the victim's 12-slot window
      } else {
         produce_block();
      }
      ++blocks_done;
      if (blocks_done % 250 == 0) {
         auto info = get_producer_info(victim);
         if (!info["is_active"].as<bool>()) { kicked = true; break; }
         last_missed = info["missed_blocks_per_rotation"].as<uint32_t>();
      }
   }
   BOOST_REQUIRE_MESSAGE(kicked, "victim was not kicked within " + std::to_string(max_blocks) +
                         " blocks; last missed_blocks_per_rotation=" + std::to_string(last_missed));

   // missed blocks accumulated across cycles (would stay near zero if schedule
   // metrics were being rebuilt every proposal)
   BOOST_REQUIRE_GT(last_missed, 100u);

   auto info = get_producer_info(victim);
   BOOST_REQUIRE_EQUAL(false, info["is_active"].as<bool>());
   BOOST_REQUIRE_EQUAL(1u, info["times_kicked"].as<uint32_t>());
   BOOST_REQUIRE_EQUAL(1u, info["kick_reason_id"].as<uint32_t>());
   BOOST_REQUIRE_EQUAL(0u, info["missed_blocks_per_rotation"].as<uint32_t>());

   // lifetime_missed_blocks must be counted exactly once: just over the
   // threshold, and no more than a few cycles beyond the last observation.
   // The old double-count bug would report roughly twice the threshold.
   const uint32_t lifetime = info["lifetime_missed_blocks"].as<uint32_t>();
   BOOST_REQUIRE_GT(lifetime, threshold);
   BOOST_REQUIRE_LE(lifetime, last_missed + 60);
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
