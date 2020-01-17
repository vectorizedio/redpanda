#pragma once

#include "model/fundamental.h"
#include "model/metadata.h"
#include "model/timeout_clock.h"
#include "reflection/adl.h"
#include "raft/types.h"
#include "rpc/models.h"

namespace raft {
class consensus;
}

namespace cluster {

using consensus_ptr = ss::lw_shared_ptr<raft::consensus>;
using broker_ptr    = ss::lw_shared_ptr<model::broker>;
struct log_record_key {
  enum class type : int8_t { partition_assignment, topic_configuration };

  type record_type;
};

/// Join request sent by node to join raft-0
struct join_request {
  explicit join_request(model::broker b) : node(std::move(b)) {}
  model::broker node;
};

struct join_reply {
  bool success;
};

/// Partition assignment describes an assignment of all replicas for single NTP.
/// The replicas are hold in vector of broker_shard.
struct partition_assignment {
  raft::group_id                   group;
  model::ntp                       ntp;
  std::vector<model::broker_shard> replicas;

  model::partition_metadata create_partition_metadata() const {
    auto p_md     = model::partition_metadata(ntp.tp.partition);
    p_md.replicas = replicas;
    ;
    return p_md;
  }
};

struct topic_configuration {
  topic_configuration(model::ns n, model::topic t, int32_t count, int16_t rf)
    : ns(std::move(n))
    , topic(std::move(t))
    , partition_count(count)
    , replication_factor(rf) {}
  model::ns    ns;
  model::topic topic;
  // using signed integer because Kafka protocol defines it as signed int
  int32_t partition_count;
  // using signed integer because Kafka protocol defines it as signed int
  int16_t replication_factor;
  // topic configuration entries
  model::compression                 compression = model::compression::none;
  model::topic_partition::compaction compaction =
    model::topic_partition::compaction::no;
  uint64_t                       retention_bytes = 0;
  model::timeout_clock::duration retention       = model::max_duration;
};

enum class topic_error_code : int16_t {
  no_error,
  unknown_error,
  time_out,
  invalid_partitions,
  invalid_replication_factor,
  invalid_config,
  not_leader_controller,
  topic_error_code_min = no_error,
  topic_error_code_max = not_leader_controller,
};

constexpr std::string_view topic_error_code_names[] = {
  [(int16_t)topic_error_code::no_error]           = "no_error",
  [(int16_t)topic_error_code::unknown_error]      = "unknown_error",
  [(int16_t)topic_error_code::time_out]           = "time_out",
  [(int16_t)topic_error_code::invalid_partitions] = "invalid_partitions",
  [(int16_t)topic_error_code::invalid_replication_factor] =
    "invalid_replication_factor",
  [(int16_t)topic_error_code::invalid_config]        = "invalid_config",
  [(int16_t)topic_error_code::not_leader_controller] = "not_leader_controller"};

std::ostream &operator<<(std::ostream &, topic_error_code);

struct topic_result {
  explicit topic_result(model::topic     t,
                        topic_error_code ec = topic_error_code::no_error)
    : topic(std::move(t)), ec(ec) {}
  model::topic     topic;
  topic_error_code ec;
};

/// Structure representing difference between two set of brokers.
/// It is used to represent changes that have to be applied to raft client cache
struct brokers_diff {
  std::vector<broker_ptr> updated;
  std::vector<broker_ptr> removed;
};

}  // namespace cluster

namespace reflection {

template<>
struct adl<cluster::topic_configuration> {
  void to(iobuf& out, cluster::topic_configuration&& t) {
    reflection::serialize(out,
                   ss::sstring(std::move(t.ns)),
                   ss::sstring(std::move(t.topic)),
                   t.partition_count,
                   t.replication_factor,
                   t.compression,
                   t.compaction,
                   t.retention_bytes,
                   t.retention);
  }

  cluster::topic_configuration from(iobuf_parser& in) {
    auto ns = model::ns(adl<ss::sstring>{}.from(in));
    auto topic = model::topic(adl<ss::sstring>{}.from(in));
    auto count = adl<int32_t>{}.from(in);
    auto rf = adl<int16_t>{}.from(in);
    auto tp_cfg =  cluster::topic_configuration(std::move(ns), std::move(topic), count, rf);

    tp_cfg.compression = adl<model::compression>{}.from(in);
    tp_cfg.compaction = adl<model::topic_partition::compaction>{}.from(in);
    tp_cfg.retention_bytes = adl<uint64_t>{}.from(in);
    tp_cfg.retention = adl<model::timeout_clock::duration>{}.from(in);
    return tp_cfg;
  }
};

template<>
struct adl<cluster::join_request> {
  void to(iobuf& out, cluster::join_request&& r) {
    adl<model::broker>().to(out, std::move(r.node));
  }

  cluster::join_request from(iobuf io) {
    return reflection::from_iobuf<cluster::join_request>(std::move(io));
  }

  cluster::join_request from(iobuf_parser& in) {
    return cluster::join_request(adl<model::broker>().from(in));
  }
};


}  // namespace reflection
