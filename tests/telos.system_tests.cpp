#include <boost/test/unit_test.hpp>

#include "eosio.system_tester.hpp"

#define MAX_PRODUCERS 42

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

      const double bpay_rate_percent = bpay_rate / double(100000);
      auto to_workers = static_cast<int64_t>((12 * double(worker_amount) * double(usecs_between_fills)) / double(usecs_per_year));
      auto to_producers = static_cast<int64_t>((bpay_rate_percent * double(initial_supply.get_amount()) * double(usecs_between_fills)) / double(usecs_per_year));

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
      BOOST_REQUIRE_EQUAL(payment, to_bpay);
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

      const double bpay_rate_percent = bpay_rate / double(100000);
      auto to_workers = static_cast<int64_t>((12 * double(worker_amount) * double(usecs_between_fills)) / double(usecs_per_year));
      auto to_producers = static_cast<int64_t>((bpay_rate_percent * double(initial_supply.get_amount()) * double(usecs_between_fills)) / double(usecs_per_year));

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

      uint32_t sharecount = 0;
      i = 0;
      for (const auto &prod : producer_infos) {
         if (prod["is_active"].as<bool>()) {
               if (i < 21) {
                  sharecount += 2;
               } else if (i >= 21 && i <= MAX_PRODUCERS) {
                  sharecount++;
               } else
                  break;
         }
         i++;
      }

      auto shareValue = to_producers / sharecount;

      int producer_count = 0;
      for(const auto &prod : producer_infos) {
         //std::cout << producer_count << std::endl;
         //std::cout << "producer_info: " << prod << std::endl;
         if(producer_count < producer_amount) {
            BOOST_REQUIRE_EQUAL(0, prod["unpaid_blocks"].as<uint32_t>());
            const asset balance = get_balance(prod["owner"].as<name>());
            const fc::variant payout_info = get_payment_info(prod["owner"].as<name>());
            BOOST_REQUIRE(!payout_info.is_null());
            const asset payment = payout_info["pay"].as<asset>();

            BOOST_REQUIRE_EQUAL(payment, asset(shareValue * ((producer_count < 21) ? 2 : 1), symbol{CORE_SYM}));
            push_action(prod["owner"].as<name>(), "claimrewards"_n, mvo()("owner", prod["owner"].as<name>()));
            BOOST_REQUIRE_EQUAL(get_balance(prod["owner"].as<name>()), balance + payment);
            BOOST_REQUIRE_EQUAL(claim_time, microseconds_since_epoch_of_iso_string( prod["last_claim_time"] ));
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

      BOOST_REQUIRE_EQUAL(0, tot_unpaid_blocks);

      BOOST_REQUIRE_EQUAL(get_balance("works.decide"_n), initial_wps_balance + to_wps);
      const asset supply  = get_token_supply();
      BOOST_REQUIRE_EQUAL(supply, initial_supply + new_tokens);
   }
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( ibc_new_account, eosio_system_tester ) try {

    // fail if alice tries
    BOOST_REQUIRE_EXCEPTION(
        create_account_with_resources("ibc.testing"_n, "alice1111111"_n),
        eosio_assert_message_exception,
        eosio_assert_message_is("only eosio can create names that start with 'ibc.'")
    );

    // success if sudo account tries
    BOOST_REQUIRE_EQUAL(
        false,
        create_account_with_resources("ibc.testing"_n, "eosio"_n)->except.has_value());

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
