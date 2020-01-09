#pragma once

#include "hashing/crc32c.h"
#include "model/fundamental.h"
#include "model/record.h"
#include "random/generators.h"
#include "storage/crc_record.h"
#include "storage/log_manager.h"
#include "storage/tests/random_batch.h"
#include "test_utils/fixture.h"

#include <boost/range/irange.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <fmt/core.h>

constexpr size_t kb = 1024;
constexpr size_t mb = 1024 * kb;
constexpr size_t gb = 1024 * mb;

constexpr size_t operator""_kb(unsigned long long val) { return val * kb; }
constexpr size_t operator""_mb(unsigned long long val) { return val * mb; }
constexpr size_t operator""_gb(unsigned long long val) { return val * gb; }

static logger tlog{"test_log"};

inline void validate_batch_crc(model::record_batch& batch) {
    crc32 crc;
    storage::crc_batch_header(
      crc, batch.get_header_for_testing(), batch.size());
    if (batch.compressed()) {
        crc.extend(batch.get_compressed_records().records());
    } else {
        for (model::record& r : batch.get_uncompressed_records_for_testing()) {
            storage::crc_record_header_and_key(crc, r);
            crc.extend(r.packed_value_and_headers());
        }
    }

    BOOST_REQUIRE_EQUAL(batch.crc(), crc.value());
}

class storage_test_fixture {
public:
    sstring test_dir = "test_data_" + random_generators::gen_alphanum_string(5);

    storage_test_fixture() { configure_unit_test_logging(); }

    void configure_unit_test_logging() {
        seastar::global_logger_registry().set_all_loggers_level(
          seastar::log_level::trace);
        seastar::global_logger_registry().set_logger_level(
          "exception", seastar::log_level::debug);

        seastar::apply_logging_settings(seastar::logging_settings{
          .logger_levels = {{"exception", seastar::log_level::debug}},
          .default_level = seastar::log_level::trace,
          .stdout_timestamp_style = seastar::logger_timestamp_style::real});
    }

    /// Creates a log manager in test directory
    storage::log_manager make_log_manager(storage::log_config cfg) {
        return storage::log_manager(std::move(cfg));
    }

    /// Creates a log manager in test directory with default config
    storage::log_manager make_log_manager() {
        return storage::log_manager(default_log_config(test_dir));
    }

    storage::log_config default_log_config(sstring test_dir) {
        return storage::log_config{
          test_dir, 200_mb, storage::log_config::sanitize_files::yes};
    }

    model::ntp make_ntp(sstring ns, sstring topic, size_t partition_id) {
        return model::ntp{
          .ns = model::ns(std::move(ns)),
          .tp = {.topic = model::topic(std::move(topic)),
                 .partition = model::partition_id(partition_id)}};
    }

    void create_topic_dir(sstring ns, sstring topic, size_t partition_id) {
        auto ntp = make_ntp(ns, topic, partition_id);
        recursive_touch_directory(fmt::format("{}/{}", test_dir, ntp.path()))
          .wait();
    }

    struct batch_validating_consumer {
        future<stop_iteration> operator()(model::record_batch b) {
            tlog.debug(
              "Validating batch [{},{}] of size {} bytes and {} records, "
              "compressed {}, CRC: [{}] ",
              b.base_offset(),
              b.last_offset(),
              b.size_bytes(),
              b.size(),
              b.compressed(),
              b.crc());

            validate_batch_crc(b);
            batches.push_back(std::move(b));
            return make_ready_future<stop_iteration>(stop_iteration::no);
        }

        std::vector<model::record_batch> end_of_stream() {
            return std::move(batches);
        }

        std::vector<model::record_batch> batches;
    };

    std::vector<model::record_batch>
    read_and_validate_all_batches(storage::log_ptr log_ptr) {
        storage::log_reader_config cfg{
          .start_offset = model::offset(0),
          .max_bytes = std::numeric_limits<size_t>::max(),
          .min_bytes = 0,
          .prio = default_priority_class(),
          .type_filter = {}};

        auto reader = log_ptr->make_reader(std::move(cfg));
        return reader.consume(batch_validating_consumer{}, model::no_timeout)
          .get0();
    }

    std::vector<model::record_batch_header> append_random_batches(
      storage::log_ptr log_ptr,
      int appends,
      int max_batches_per_append,
      storage::log_append_config::fsync sync
      = storage::log_append_config::fsync::no) {
        storage::log_append_config append_cfg{
          sync, default_priority_class(), model::no_timeout};

        model::offset expected_offset = log_ptr->max_offset();
        std::vector<model::record_batch_header> headers;

        // do multiple append calls

        for (auto append : boost::irange(0, appends)) {
            auto batch_count = random_generators::get_int(
              max_batches_per_append);
            auto batches = storage::test::make_random_batches(
              model::offset(0), batch_count);
            // Collect batches offsets
            for (auto& b : batches) {
                headers.push_back(b.get_header_for_testing());
                expected_offset += model::offset(b.size());
            }
            auto reader = model::make_memory_record_batch_reader(
              std::move(batches));
            auto res = log_ptr->append(std::move(reader), append_cfg).get0();

            // Check if after append offset was updated correctly
            BOOST_REQUIRE_EQUAL(log_ptr->max_offset(), res.last_offset);
            BOOST_REQUIRE_EQUAL(log_ptr->max_offset(), expected_offset);
        }

        return headers;
    }
};