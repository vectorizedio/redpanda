#include "redpanda/kafka/requests/metadata_request.h"

#include "model/metadata.h"
#include "redpanda/kafka/errors/errors.h"

#include <seastar/core/thread.hh>

#include <string_view>

namespace kafka::requests {

// Possible topic-level error codes:
//  unknown_topic_or_partition
//  topic_authorization_failed
//  invalid_replication_factor
// Possible partition-level error codes:
//  leader_not_available
//  listener_not_found
//  replica_not_available
seastar::future<response_ptr>
metadata_request::process(request_context& ctx, seastar::smp_service_group g) {
    if (
      ctx.header().version < min_supported
      || ctx.header().version > max_supported) {
        return seastar::make_exception_future<response_ptr>(
          std::runtime_error(fmt::format(
            "Unsupported version {} for metadata API", ctx.header().version)));
    }
    return seastar::async([&ctx] {
        auto topics = ctx.reader().read_array([](request_reader& r) {
            return model::topic_view(r.read_string_view());
        });
        if (topics.empty()) {
            topics = ctx.metadata_cache().all_topics().get0();
        }
        bool allow_auto_topic_creation = ctx.header().version >= api_version(4)
                                           ? ctx.reader().read_bool()
                                           : false;
        if (allow_auto_topic_creation) {
            kreq_log.warn("Automatically creating topics is not yet supported");
        }

        auto resp = std::make_unique<response>();
        // FIXME: Throttling #74
        if (ctx.header().version >= api_version(3)) {
            resp->writer().write(int32_t(0));
        }
        // FIXME: Get list of live brokers
        std::vector<model::broker> brokers = {
          model::broker(model::node_id(1), "localhost", 9092, std::nullopt)};
        resp->writer().write_array(
          brokers, [&ctx](const model::broker& b, response_writer& rw) {
              rw.write(b.id().value());
              rw.write(std::string_view(b.host()));
              rw.write(b.port());
              if (ctx.header().version > api_version(0)) {
                  rw.write(b.rack());
              }
          });

        // FIXME: Cluster id #95
        if (ctx.header().version >= api_version(2)) {
            resp->writer().write(std::optional<std::string_view>{});
        }

        // FIXME: Controller #96
        if (ctx.header().version >= api_version(1)) {
            resp->writer().write(int32_t(1));
        }

        resp->writer().write_array(
          topics, [&ctx](const model::topic_view& t, response_writer& rw) {
              // FIXME: Auto-create topics.
              auto topic_metadata
                = ctx.metadata_cache().get_topic_metadata(t).get0();
              if (!topic_metadata) {
                  rw.write(errors::error_code::unknown_topic_or_partition);
                  topic_metadata.emplace(model::topic_metadata{t});
              } else {
                  rw.write(errors::error_code::none);
              }
              rw.write(t.name());
              if (ctx.header().version >= api_version(1)) {
                  // Currently topics are never internal.
                  rw.write(false);
              }
              for (auto& pm : topic_metadata->partitions) {
                  rw.write(errors::error_code::none);
                  rw.write(pm.partition);
                  rw.write(int32_t(1)); // The leader.
                  // FIXME: Obtain partition replicas.
                  auto write_replicas = [&] {
                      rw.write_array(
                        std::vector<int32_t>{1},
                        [](int32_t replica, response_writer& rw) {
                            rw.write(replica);
                        });
                  };
                  write_replicas();
                  write_replicas();
                  if (ctx.header().version >= api_version(5)) {
                      // Offline replicas
                      rw.write_array(
                        std::vector<int32_t>{},
                        [](int32_t, response_writer&) {});
                  }
              }
          });

        return response_ptr(std::move(resp));
    });
}

} // namespace kafka::requests