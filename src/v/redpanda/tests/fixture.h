#pragma once
#include "cluster/namespace.h"
#include "cluster/types.h"
#include "kafka/client.h"
#include "redpanda/application.h"
#include "storage/directories.h"
#include "storage/tests/utils/disk_log_builder.h"
#include "storage/tests/utils/random_batch.h"
#include "test_utils/fixture.h"
#include "test_utils/logs.h"

#include <seastar/util/log.hh>

#include <fmt/format.h>

#include <filesystem>

class redpanda_thread_fixture {
public:
    static constexpr const char* rack_name = "i-am-rack";

    redpanda_thread_fixture() {
        app.initialize();
        configure();
        app.check_environment();
        app.configure_admin_server();
        app.wire_up_services();
        app.start();
    }

    ~redpanda_thread_fixture() {
        app.shutdown();
        std::filesystem::remove_all(data_dir);
    }

    config::configuration& lconf() { return config::shard_local_cfg(); }

    void configure() {
        data_dir = fmt::format("test.dir_{}", time(0));
        ss::smp::invoke_on_all([this] {
            auto& config = config::shard_local_cfg();
            config.get("developer_mode").set_value(true);
            config.get("enable_admin_api").set_value(false);
            config.get("rack").set_value(std::optional<ss::sstring>(rack_name));

            config.get("data_directory")
              .set_value(config::data_directory_path{.path = data_dir});

            config.get("node_id").set_value(model::node_id(1));
        }).get0();
    }

    ss::future<> wait_for_controller_leadership() {
        return app.cntrl_dispatcher.local().dispatch_to_controller(
          [](cluster::controller& c) { return c.wait_for_leadership(); });
    }

    ss::future<kafka::client> make_kafka_client() {
        return config::shard_local_cfg().kafka_api().resolve().then(
          [](ss::socket_address addr) {
              return kafka::client(rpc::base_transport::configuration{
                .server_addr = addr,
              });
          });
    }

    model::ntp
    make_default_ntp(model::topic topic, model::partition_id partition) {
        auto ntp = model::ntp{
          .ns = cluster::kafka_namespace,
          .tp = model::topic_partition{
            .topic = topic,
            .partition = partition,
          },
        };
        return ntp;
    }

    storage::log_config make_default_config() {
        return storage::log_config(
          storage::log_config::storage_type::disk,
          lconf().data_directory().as_sstring(),
          1_GiB,
          storage::log_config::debug_sanitize_files::yes);
    }

    ss::future<> recover_ntp(const model::ntp& ntp) {
        cluster::partition_assignment as{
          .group = raft::group_id(1),
          .ntp = ntp,
          .replicas = {{model::node_id(lconf().node_id()), 0}},
        };
        return ss::do_with(
          std::move(as), [this](cluster::partition_assignment& as) {
              return app.metadata_cache
                .invoke_on_all([&as](cluster::metadata_cache& mdc) {
                    mdc.add_topic(cluster::topic_configuration(
                      as.ntp.ns, as.ntp.tp.topic, 1, 1));
                })
                .then([this, &as] {
                    return app.controller.local().recover_assignment(as);
                });
          });
    }

    model::ntp make_data() {
        auto topic_name = fmt::format("my_topic_{}", 0);

        auto batches = storage::test::make_random_batches(
          model::offset(0), 20, false);

        auto ntp = model::ntp{
          .ns = cluster::kafka_namespace,
          .tp = model::topic_partition{.topic = model::topic(topic_name),
                                       .partition = model::partition_id(0)}};
        tests::persist_log_file(
          lconf().data_directory().as_sstring(), ntp, std::move(batches))
          .get();

        recover_ntp(ntp).get();

        return ntp;
    }

    application app;
    std::filesystem::path data_dir;
};
