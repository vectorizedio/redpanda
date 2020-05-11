#include "cluster/types.h"

#include "model/metadata.h"
#include "tristate.h"
#include "utils/to_string.h"

#include <fmt/ostream.h>

#include <chrono>

namespace cluster {

topic_configuration::topic_configuration(
  model::ns n, model::topic t, int32_t count, int16_t rf)
  : tp_ns(std::move(n), std::move(t))
  , partition_count(count)
  , replication_factor(rf) {}

storage::ntp_config topic_configuration::make_ntp_config(
  const ss::sstring& work_dir, model::partition_id p_id) const {
    auto ret = storage::ntp_config(
      model::ntp(tp_ns.ns, tp_ns.tp, p_id), work_dir);
    auto has_overrides = cleanup_policy_bitflags || compaction_strategy
                         || segment_size || retention_bytes.has_value()
                         || retention_bytes.is_disabled()
                         || retention_duration.has_value()
                         || retention_duration.is_disabled();

    if (has_overrides) {
        ret.overrides
          = std::make_unique<storage::ntp_config::default_overrides>(
            storage::ntp_config::default_overrides{
              .cleanup_policy_bitflags = cleanup_policy_bitflags,
              .compaction_strategy = compaction_strategy,
              .segment_size = segment_size,
              .retention_bytes = retention_bytes,
              .retention_time = retention_duration});
    }

    return ret;
}

std::ostream& operator<<(std::ostream& o, const topic_configuration& cfg) {
    fmt::print(
      o,
      "{{ topic: {}, partition_count: {}, replication_factor: {}, compression: "
      "{}, cleanup_policy_bitflags: {}, compaction_strategy: {}, "
      "retention_bytes: {}, "
      "retention_duration_hours: {}, segment_size: {}, timestamp_type: {} }}",
      cfg.tp_ns,
      cfg.partition_count,
      cfg.replication_factor,
      cfg.compression,
      cfg.cleanup_policy_bitflags,
      cfg.compaction_strategy,
      cfg.retention_bytes,
      cfg.retention_duration,
      cfg.segment_size,
      cfg.timestamp_type);

    return o;
}

std::ostream& operator<<(std::ostream& o, const topic_result& r) {
    fmt::print(o, "topic: {}, result: {}", r.tp_ns, r.ec);
    return o;
}
} // namespace cluster

namespace reflection {
void adl<cluster::topic_configuration>::to(
  iobuf& out, cluster::topic_configuration&& t) {
    reflection::serialize(
      out,
      t.tp_ns,
      t.partition_count,
      t.replication_factor,
      t.compression,
      t.cleanup_policy_bitflags,
      t.compaction_strategy,
      t.timestamp_type,
      t.segment_size,
      t.retention_bytes,
      t.retention_duration);
}

cluster::topic_configuration
adl<cluster::topic_configuration>::from(iobuf_parser& in) {
    auto ns = model::ns(adl<ss::sstring>{}.from(in));
    auto topic = model::topic(adl<ss::sstring>{}.from(in));
    auto partition_count = adl<int32_t>{}.from(in);
    auto rf = adl<int16_t>{}.from(in);

    auto cfg = cluster::topic_configuration(
      std::move(ns), std::move(topic), partition_count, rf);

    cfg.compression = adl<std::optional<model::compression>>{}.from(in);
    cfg.cleanup_policy_bitflags
      = adl<std::optional<model::cleanup_policy_bitflags>>{}.from(in);
    cfg.compaction_strategy
      = adl<std::optional<model::compaction_strategy>>{}.from(in);
    cfg.timestamp_type = adl<std::optional<model::timestamp_type>>{}.from(in);
    cfg.segment_size = adl<std::optional<size_t>>{}.from(in);
    cfg.retention_bytes = adl<tristate<size_t>>{}.from(in);
    cfg.retention_duration = adl<tristate<std::chrono::milliseconds>>{}.from(
      in);

    return cfg;
}

void adl<cluster::join_request>::to(iobuf& out, cluster::join_request&& r) {
    adl<model::broker>().to(out, std::move(r.node));
}

cluster::join_request adl<cluster::join_request>::from(iobuf io) {
    return reflection::from_iobuf<cluster::join_request>(std::move(io));
}

cluster::join_request adl<cluster::join_request>::from(iobuf_parser& in) {
    return cluster::join_request(adl<model::broker>().from(in));
}

void adl<cluster::topic_result>::to(iobuf& out, cluster::topic_result&& t) {
    reflection::serialize(out, std::move(t.tp_ns), t.ec);
}

cluster::topic_result adl<cluster::topic_result>::from(iobuf_parser& in) {
    auto tp_ns = adl<model::topic_namespace>{}.from(in);
    auto ec = adl<cluster::errc>{}.from(in);
    return cluster::topic_result(std::move(tp_ns), ec);
}

void adl<cluster::create_topics_request>::to(
  iobuf& out, cluster::create_topics_request&& r) {
    reflection::serialize(out, std::move(r.topics), r.timeout);
}

cluster::create_topics_request
adl<cluster::create_topics_request>::from(iobuf io) {
    return reflection::from_iobuf<cluster::create_topics_request>(
      std::move(io));
}

cluster::create_topics_request
adl<cluster::create_topics_request>::from(iobuf_parser& in) {
    using underlying_t = std::vector<cluster::topic_configuration>;
    auto configs = adl<underlying_t>().from(in);
    auto timeout = adl<model::timeout_clock::duration>().from(in);
    return cluster::create_topics_request{std::move(configs), timeout};
}

void adl<cluster::create_topics_reply>::to(
  iobuf& out, cluster::create_topics_reply&& r) {
    reflection::serialize(
      out, std::move(r.results), std::move(r.metadata), std::move(r.configs));
}

cluster::create_topics_reply adl<cluster::create_topics_reply>::from(iobuf io) {
    return reflection::from_iobuf<cluster::create_topics_reply>(std::move(io));
}

cluster::create_topics_reply
adl<cluster::create_topics_reply>::from(iobuf_parser& in) {
    auto results = adl<std::vector<cluster::topic_result>>().from(in);
    auto md = adl<std::vector<model::topic_metadata>>().from(in);
    auto cfg = adl<std::vector<cluster::topic_configuration>>().from(in);
    return cluster::create_topics_reply{
      std::move(results), std::move(md), std::move(cfg)};
}

void adl<model::timeout_clock::duration>::to(iobuf& out, duration dur) {
    // This is a clang bug that cause ss::cpu_to_le to become ambiguous
    // because rep has type of long long
    // adl<rep>{}.to(out, dur.count());
    adl<uint64_t>{}.to(out, dur.count());
}

model::timeout_clock::duration
adl<model::timeout_clock::duration>::from(iobuf_parser& in) {
    // This is a clang bug that cause ss::cpu_to_le to become ambiguous
    // because rep has type of long long
    // auto rp = adl<rep>{}.from(in);
    auto rp = adl<uint64_t>{}.from(in);
    return duration(rp);
}
} // namespace reflection
