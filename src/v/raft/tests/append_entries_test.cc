#include "finjector/hbadger.h"
#include "model/fundamental.h"
#include "model/metadata.h"
#include "model/record.h"
#include "model/timestamp.h"
#include "raft/consensus_utils.h"
#include "raft/tests/raft_group_fixture.h"
#include "raft/types.h"
#include "storage/record_batch_builder.h"
#include "storage/tests/utils/disk_log_builder.h"

#include <system_error>

FIXTURE_TEST(test_entries_are_replicated_to_all_nodes, raft_test_fixture) {
    raft_group gr = raft_group(raft::group_id(0), 3);
    gr.enable_all();

    auto leader_id = wait_for_group_leader(gr);
    auto leader_raft = gr.get_member(leader_id).consensus;
    auto res = leader_raft
                 ->replicate(random_batches_entry(1), default_replicate_opts)
                 .get0();

    validate_logs_replication(gr);
};

FIXTURE_TEST(test_replicate_multiple_entries_single_node, raft_test_fixture) {
    raft_group gr = raft_group(raft::group_id(0), 1);
    gr.enable_all();
    auto leader_id = wait_for_group_leader(gr);
    auto leader_raft = gr.get_member(leader_id).consensus;
    for (int i = 0; i < 5; ++i) {
        if (leader_raft->is_leader()) {
            auto res = leader_raft
                         ->replicate(
                           random_batches_entry(5), default_replicate_opts)
                         .get0();
        }
    }

    validate_logs_replication(gr);

    wait_for(
      10s,
      [this, &gr] { return are_all_commit_indexes_the_same(gr); },
      "State is consistent after replication");
};

FIXTURE_TEST(test_replicate_multiple_entries, raft_test_fixture) {
    raft_group gr = raft_group(raft::group_id(0), 3);
    gr.enable_all();
    auto leader_id = wait_for_group_leader(gr);
    auto leader_raft = gr.get_member(leader_id).consensus;
    for (int i = 0; i < 5; ++i) {
        if (leader_raft->is_leader()) {
            auto res = leader_raft
                         ->replicate(
                           random_batches_entry(5), default_replicate_opts)
                         .get0();
        }
    }

    validate_logs_replication(gr);
    wait_for(
      10s,
      [this, &gr] { return are_all_commit_indexes_the_same(gr); },
      "State is consistent");
};

FIXTURE_TEST(test_single_node_recovery, raft_test_fixture) {
    raft_group gr = raft_group(raft::group_id(0), 3);
    gr.enable_all();
    auto leader_id = wait_for_group_leader(gr);
    model::node_id disabled_id;
    for (auto& [id, _] : gr.get_members()) {
        // disable one of the non leader nodes
        if (leader_id != id) {
            disabled_id = id;
            gr.disable_node(id);
            break;
        }
    }
    auto leader_raft = gr.get_member(leader_id).consensus;
    // append some entries
    for (int i = 0; i < 5; ++i) {
        if (leader_raft->is_leader()) {
            auto res = leader_raft
                         ->replicate(
                           random_batches_entry(5), default_replicate_opts)
                         .get0();
        }
    }
    validate_logs_replication(gr);

    gr.enable_node(disabled_id);

    validate_logs_replication(gr);

    wait_for(
      10s,
      [this, &gr] { return are_all_commit_indexes_the_same(gr); },
      "After recovery state is consistent");

    validate_logs_replication(gr);
};

FIXTURE_TEST(test_empty_node_recovery, raft_test_fixture) {
    raft_group gr = raft_group(raft::group_id(0), 3);
    gr.enable_all();
    auto leader_id = wait_for_group_leader(gr);
    auto leader_raft = gr.get_member(leader_id).consensus;
    // append some entries
    for (int i = 0; i < 5; ++i) {
        if (leader_raft->is_leader()) {
            auto res = leader_raft
                         ->replicate(
                           random_batches_entry(5), default_replicate_opts)
                         .get0();
        }
    }
    validate_logs_replication(gr);
    model::node_id disabled_id;
    for (auto& [id, m] : gr.get_members()) {
        // disable one of the non leader nodes
        if (leader_id != id) {
            disabled_id = id;
            // truncate the node log
            m.log
              ->truncate(storage::truncate_config(
                model::offset(0), ss::default_priority_class()))
              .get();
            gr.disable_node(id);
            break;
        }
    }

    gr.enable_node(disabled_id);

    validate_logs_replication(gr);

    wait_for(
      10s,
      [this, &gr] { return are_all_commit_indexes_the_same(gr); },
      "After recovery state is consistent");
};

FIXTURE_TEST(test_single_node_recovery_multi_terms, raft_test_fixture) {
    raft_group gr = raft_group(raft::group_id(0), 3);
    gr.enable_all();
    auto leader_id = wait_for_group_leader(gr);
    model::node_id disabled_id;
    for (auto& [id, _] : gr.get_members()) {
        // disable one of the non leader nodes
        if (leader_id = gr.get_leader_id().value(); leader_id != id) {
            disabled_id = id;
            gr.disable_node(id);
            break;
        }
    }
    auto leader_raft = gr.get_member(leader_id).consensus;
    // append some entries in current term
    for (int i = 0; i < 5; ++i) {
        if (leader_raft->is_leader()) {
            auto res = leader_raft
                         ->replicate(
                           random_batches_entry(5), default_replicate_opts)
                         .get0();
        }
    }

    // roll the term
    leader_raft->step_down(leader_raft->term() + model::term_id(1)).get0();
    leader_id = wait_for_group_leader(gr);
    leader_raft = gr.get_member(leader_id).consensus;
    // append some entries in next term
    for (int i = 0; i < 5; ++i) {
        if (leader_raft->is_leader()) {
            auto res = leader_raft
                         ->replicate(
                           random_batches_entry(5), default_replicate_opts)
                         .get0();
        }
    }

    validate_logs_replication(gr);

    gr.enable_node(disabled_id);

    validate_logs_replication(gr);

    wait_for(
      10s,
      [this, &gr] { return are_all_commit_indexes_the_same(gr); },
      "State is conistent after recovery");
};

FIXTURE_TEST(test_recovery_of_crashed_leader_truncation, raft_test_fixture) {
    raft_group gr = raft_group(raft::group_id(0), 3);
    gr.enable_all();
    auto first_leader_id = wait_for_group_leader(gr);
    model::node_id disabled_id;
    std::vector<model::node_id> disabled_nodes{};
    for (auto& [id, _] : gr.get_members()) {
        // disable all nodes except the leader
        if (id != first_leader_id) {
            disabled_nodes.push_back(id);
        }
    }
    for (auto& id : disabled_nodes) {
        gr.disable_node(id);
    }
    // append some entries to leader log
    auto leader_raft = gr.get_member(first_leader_id).consensus;
    auto f = leader_raft->replicate(
      random_batches_entry(2), default_replicate_opts);
    // since replicate doesn't accept timeout client have to deal with it.
    auto v = ss::with_timeout(model::timeout_clock::now() + 1s, std::move(f))
               .handle_exception_type([](const ss::timed_out_error&) {
                   return result<raft::replicate_result>(
                     rpc::errc::client_request_timeout);
               })
               .get0();

    // shut down the leader
    gr.disable_node(first_leader_id);

    // enable nodes that were disabled before we appended on leader
    for (auto id : disabled_nodes) {
        gr.enable_node(model::node_id(id));
    }
    // wait for leader to be elected from enabled nodes
    auto leader_id = wait_for_group_leader(gr);
    leader_raft = gr.get_member(leader_id).consensus;

    // append some entries via new leader so old one has some data to
    // truncate
    auto res = leader_raft
                 ->replicate(random_batches_entry(2), default_replicate_opts)
                 .get0();

    validate_logs_replication(gr);

    gr.enable_node(first_leader_id);

    // wait for data to be replicated to old leader node (have to truncate)
    validate_logs_replication(gr);

    wait_for(
      10s,
      [this, &gr] { return are_all_commit_indexes_the_same(gr); },
      "After recovery state should be consistent");
};

FIXTURE_TEST(test_append_entries_with_relaxed_consistency, raft_test_fixture) {
    raft_group gr = raft_group(raft::group_id(0), 3);
    gr.enable_all();
    auto leader_id = wait_for_group_leader(gr);
    auto leader_raft = gr.get_member(leader_id).consensus;
    // append some entries
    auto opts = default_replicate_opts;
    opts.consistency = raft::consistency_level::leader_ack;
    for (int i = 0; i < 30; ++i) {
        if (leader_raft->is_leader()) {
            auto res
              = leader_raft->replicate(random_batches_entry(5), opts).get0();
        }
    }
    validate_logs_replication(gr);

    wait_for(
      10s,
      [this, &gr] { return are_all_commit_indexes_the_same(gr); },
      "After recovery state is consistent");
};

FIXTURE_TEST(
  test_append_entries_with_relaxed_consistency_single_node, raft_test_fixture) {
    raft_group gr = raft_group(raft::group_id(0), 1);
    gr.enable_all();
    auto leader_id = wait_for_group_leader(gr);
    auto leader_raft = gr.get_member(leader_id).consensus;
    // append some entries
    auto opts = default_replicate_opts;
    opts.consistency = raft::consistency_level::leader_ack;
    for (int i = 0; i < 30; ++i) {
        if (leader_raft->is_leader()) {
            auto res
              = leader_raft->replicate(random_batches_entry(5), opts).get0();
        }
    }
    validate_logs_replication(gr);

    wait_for(
      1s,
      [this, &gr] { return are_all_commit_indexes_the_same(gr); },
      "After recovery state is consistent");

    wait_for(
      1s,
      [this, &gr] {
          auto& node = gr.get_members().begin()->second;
          auto lstats = node.log->offsets();
          return lstats.committed_offset == lstats.dirty_offset
                 && node.consensus->committed_offset()
                      == lstats.committed_offset;
      },
      "Commit index is advanced ");
};

/**
 *
 * This test tests recovery of log with gaps
 *
 * Example situation:
 *
 * Leader log: [0,10]|--gap--|[21,40]|--gap--|[45,59][60,73]
 *
 *
 * Expected outcome:
 *
 * Follower log has exactly the same set of batches as leader
 *
 */
FIXTURE_TEST(test_compacted_log_recovery, raft_test_fixture) {
    raft_group gr = raft_group(
      raft::group_id(0),
      3,
      storage::log_config::storage_type::disk,
      model::cleanup_policy_bitflags::compaction,
      10_MiB);

    auto cfg = storage::log_builder_config();
    cfg.base_dir = fmt::format("{}/{}", gr.get_data_dir(), 0);

    // for now, as compaction isn't yet ready we simulate it with log builder
    auto ntp = node_ntp(raft::group_id(0), model::node_id(0));
    storage::ntp_config::default_overrides overrides;
    overrides.cleanup_policy_bitflags
      = model::cleanup_policy_bitflags::compaction;
    storage::ntp_config ntp_config(
      ntp,
      cfg.base_dir,
      std::make_unique<storage::ntp_config::default_overrides>(
        std::move(overrides)));
    storage::disk_log_builder builder(std::move(cfg));

    builder | storage::start(std::move(ntp_config)) | storage::add_segment(0)
      | storage::add_random_batch(0, 1, storage::maybe_compress_batches::no)
      | storage::add_random_batch(1, 5, storage::maybe_compress_batches::no)
      // gap from 6 to 19
      | storage::add_random_batch(20, 30, storage::maybe_compress_batches::no)
      // gap from 50 to 67
      | storage::add_random_batch(68, 11, storage::maybe_compress_batches::no)
      | storage::stop();

    gr.enable_all();
    auto leader_id = wait_for_group_leader(gr);
    model::node_id disabled_id;
    auto leader_raft = gr.get_member(leader_id).consensus;
    ss::abort_source as;

    // disable one of the non leader nodes
    for (auto& [id, _] : gr.get_members()) {
        if (leader_id != id) {
            disabled_id = id;
            gr.disable_node(id);
            break;
        }
    }
    validate_logs_replication(gr);

    gr.enable_node(disabled_id);

    validate_logs_replication(gr);

    wait_for(
      3s,
      [this, &gr] { return are_all_commit_indexes_the_same(gr); },
      "After recovery state is consistent");

    validate_logs_replication(gr);
};
