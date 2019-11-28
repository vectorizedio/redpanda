#include "model/record.h"
#include "raft/configuration_bootstrap_state.h"
#include "raft/consensus_utils.h"
#include "random/generators.h"
#include "resource_mgmt/io_priority.h"
#include "rpc/models.h"
#include "storage/log.h"
#include "storage/log_manager.h"
#include "storage/record_batch_builder.h"
// testing
#include "test_utils/fixture.h"

#include <seastar/core/future-util.hh>

struct foreign_entry_fixture {
    static constexpr int active_nodes = 3;
    foreign_entry_fixture()
      : _mngr(storage::log_config{
        .base_dir = ".",
        .max_segment_size = 1 << 30,
        .should_sanitize = storage::log_config::sanitize_files::yes}) {
        _log = _mngr.manage(_ntp).get0();
    }
    std::vector<storage::log::append_result> write_n(const std::size_t n) {
        auto cfg = storage::log_append_config{
          storage::log_append_config::fsync::no,
          default_priority_class(),
          model::no_timeout};
        std::vector<storage::log::append_result> res;
        res.push_back(
          _log->append(gen_data_record_batch_reader(n), cfg).get0());
        res.push_back(
          _log->append(gen_config_record_batch_reader(n), cfg).get0());
        _log->flush().get();
        return res;
    }
    template<typename Func>
    model::record_batch_reader reader_gen(std::size_t n, Func&& f) {
        std::vector<model::record_batch> batches;
        batches.reserve(n);
        while (n-- > 0) {
            batches.push_back(f());
        }
        return model::make_memory_record_batch_reader(std::move(batches));
    }
    model::record_batch_reader gen_config_record_batch_reader(std::size_t n) {
        return reader_gen(n, [this] { return config_batch(); });
    }
    model::record_batch_reader gen_data_record_batch_reader(std::size_t n) {
        return reader_gen(n, [this] { return data_batch(); });
    }
    model::record_batch data_batch() {
        storage::record_batch_builder bldr(raft::data_batch_type, _base_offset);
        bldr.add_raw_kv(rand_iobuf(), rand_iobuf());
        ++_base_offset;
        return std::move(bldr).build();
    }
    model::record_batch config_batch() {
        storage::record_batch_builder bldr(
          raft::configuration_batch_type, _base_offset);
        bldr.add_raw_kv(rand_iobuf(), rpc::serialize(rand_config()));
        ++_base_offset;
        return std::move(bldr).build();
    }
    iobuf rand_iobuf() const {
        iobuf b;
        auto data = random_generators::gen_alphanum_string(100);
        b.append(data.data(), data.size());
        return b;
    }
    raft::group_configuration rand_config() const {
        std::vector<model::broker> nodes;
        std::vector<model::broker> learners;
        auto bgen = [](int l, int h) {
            return model::broker(
              model::node_id(random_generators::get_int(l, h)), // id
              random_generators::gen_alphanum_string(10),       // host
              random_generators::get_int(1025, 65535),          // port
              std::nullopt);
        };
        for (auto i = 0; i < active_nodes; ++i) {
            nodes.push_back(bgen(i, i));
            learners.push_back(
              bgen(active_nodes + 1, active_nodes * active_nodes));
        }
        return raft::group_configuration{
          .leader_id = model::node_id(
            random_generators::get_int(0, active_nodes)),
          .nodes = std::move(nodes),
          .learners = std::move(learners)};
    }
    ~foreign_entry_fixture() { _mngr.stop().get(); }
    model::offset _base_offset{0};
    storage::log_ptr _log;
    storage::log_manager _mngr;
    model::ntp _ntp{
      model::ns("bootstrap_test_" + random_generators::gen_alphanum_string(8)),
      model::topic_partition{
        model::topic(random_generators::gen_alphanum_string(6)),
        model::partition_id(random_generators::get_int(0, 24))}};
};

FIXTURE_TEST(sharing_one_entry, foreign_entry_fixture) {
    std::vector<raft::entry> copies =
      // clang-format off
      raft::details::share_one_entry(
        raft::entry(raft::configuration_batch_type,
                    gen_config_record_batch_reader(3)),
        smp::count, true).get0();
    // clang-format on

    BOOST_REQUIRE_EQUAL(copies.size(), smp::count);
    for (shard_id shard = 0; shard < smp::count; ++shard) {
        info("Submitting shared raft::entry to shard:{}", shard);
        auto cfg =
          // MUST return the config; otherwise thread exception
          smp::submit_to(shard, [e = std::move(copies[shard])]() mutable {
              info("extracting configuration");
              return raft::details::extract_configuration(std::move(e));
          }).get0();

        for (auto& n : cfg.nodes) {
            BOOST_REQUIRE(
              n.id() >= 0 && n.id() <= foreign_entry_fixture::active_nodes);
        }
        for (auto& n : cfg.learners) {
            BOOST_REQUIRE(n.id() > foreign_entry_fixture::active_nodes);
        }
    }
}

FIXTURE_TEST(copy_lots_of_entries, foreign_entry_fixture) {
    std::vector<std::vector<raft::entry>> share_copies;
    {
        std::vector<raft::entry> entries;
        entries.reserve(smp::count);
        for (size_t i = 0; i < smp::count; ++i) {
            entries.emplace_back(
              raft::configuration_batch_type,
              gen_config_record_batch_reader(1));
        }
        share_copies = raft::details::foreign_share_n(
                         std::move(entries), smp::count)
                         .get0();
    }
    BOOST_REQUIRE_EQUAL(share_copies.size(), smp::count);
    BOOST_REQUIRE_EQUAL(
      std::accumulate(
        share_copies.begin(),
        share_copies.end(),
        size_t(0),
        [](size_t acc, std::vector<raft::entry>& ex) {
            return acc + ex.size();
        }),
      smp::count * smp::count);

    for (shard_id shard = 0; shard < smp::count; ++shard) {
        info("Submitting shared raft::entry to shard:{}", shard);
        auto cfgs
          = smp::submit_to(
              shard,
              [es = std::move(share_copies[shard])]() mutable {
                  return do_with(
                    std::move(es), [](std::vector<raft::entry>& es) {
                        return copy_range<
                          std::vector<raft::group_configuration>>(
                          es, [](raft::entry& e) {
                              info("(x) extracting configuration");
                              return raft::details::extract_configuration(
                                std::move(e));
                          });
                    });
              })
              .get0();
        for (auto& cfg : cfgs) {
            for (auto& n : cfg.nodes) {
                BOOST_REQUIRE(
                  n.id() >= 0 && n.id() <= foreign_entry_fixture::active_nodes);
            }
            for (auto& n : cfg.learners) {
                BOOST_REQUIRE(n.id() > foreign_entry_fixture::active_nodes);
            }
        }
    }
}
