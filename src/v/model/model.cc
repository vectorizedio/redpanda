#include "bytes/iobuf.h"
#include "model/compression.h"
#include "model/fundamental.h"
#include "model/metadata.h"
#include "model/record.h"
#include "model/timestamp.h"
#include "utils/string_switch.h"
#include "utils/to_string.h"

#include <seastar/core/print.hh>
#include <seastar/net/inet_address.hh>
#include <seastar/net/ip.hh>

#include <fmt/ostream.h>

#include <iostream>

namespace model {

std::ostream& operator<<(std::ostream& os, const compression& c) {
    os << "{compression: ";
    switch (c) {
    case compression::none:
        os << "none";
        break;
    case compression::gzip:
        os << "gzip";
        break;
    case compression::snappy:
        os << "snappy";
        break;
    case compression::lz4:
        os << "lz4";
        break;
    case compression::zstd:
        os << "zstd";
        break;
    default:
        os << "ERROR";
        break;
    }
    return os << "}";
}

std::ostream& operator<<(std::ostream& os, timestamp ts) {
    if (ts != timestamp::missing()) {
        return ss::fmt_print(os, "{{timestamp: {}}}", ts.value());
    }
    return os << "{timestamp: missing}";
}

std::ostream& operator<<(std::ostream& os, const topic_partition& tp) {
    return ss::fmt_print(
      os, "{{topic_partition: {}:{}}}", tp.topic, tp.partition);
}

std::ostream& operator<<(std::ostream& os, const ntp& n) {
    fmt::print(os, "{{ntp: {}:{}:{}}}", n.ns, n.tp.topic, n.tp.partition);
    return os;
}

std::ostream& operator<<(std::ostream& o, const model::topic_namespace& tp_ns) {
    return ss::fmt_print(o, "{{ns: {}, topic: {}}}", tp_ns.ns, tp_ns.tp);
}

std::ostream&
operator<<(std::ostream& o, const model::topic_namespace_view& tp_ns) {
    return ss::fmt_print(o, "{{ns: {}, topic: {}}}", tp_ns.ns, tp_ns.tp);
}

std::ostream& operator<<(std::ostream& os, timestamp_type ts) {
    switch (ts) {
    case timestamp_type::append_time:
        return os << "{append_time}";
    case timestamp_type::create_time:
        return os << "{create_time}";
    }
    return os << "{unknown timestamp:" << static_cast<int>(ts) << "}";
}

std::ostream& operator<<(std::ostream& o, const record_header& h) {
    return o << "{key_size=" << h.key_size() << ", key=" << h.key()
             << ", value_size=" << h.value_size() << ", value=" << h.value()
             << "}";
}

std::ostream& operator<<(std::ostream& o, const record_attributes& a) {
    return o << "{" << a._attributes << "}";
}
std::ostream& operator<<(std::ostream& o, const record& r) {
    o << "{record: size_bytes=" << r.size_bytes()
      << ", attributes=" << r.attributes()
      << ", timestamp_delta=" << r._timestamp_delta
      << ", offset_delta=" << r._offset_delta << ", key_size= " << r._key_size
      << ", key=" << r.key() << ", value_size=" << r.value()
      << ", header_size:" << r.headers().size() << ", headers=[";

    for (auto& h : r.headers()) {
        o << h;
    }
    return o << "]}";
}

std::ostream&
operator<<(std::ostream& o, const record_batch_attributes& attrs) {
    o << "{compression:";
    if (attrs.is_valid_compression()) {
        // this method... sadly, just throws
        o << attrs.compression();
    } else {
        o << "invalid compression";
    }
    return o << ", type:" << attrs.timestamp_type() << "}";
}

std::ostream& operator<<(std::ostream& o, const record_batch_header& h) {
    o << "{header_crc:" << h.header_crc << ", size_bytes:" << h.size_bytes
      << ", base_offset:" << h.base_offset << ", type:" << h.type
      << ", crc:" << h.crc << ", attrs:" << h.attrs
      << ", last_offset_delta:" << h.last_offset_delta
      << ", first_timestamp:" << h.first_timestamp
      << ", max_timestamp:" << h.max_timestamp
      << ", producer_id:" << h.producer_id
      << ", producer_epoch:" << h.producer_epoch
      << ", base_sequence:" << h.base_sequence
      << ", record_count:" << h.record_count;
    o << ", ctx:{term:" << h.ctx.term << ", owner_shard:";
    if (h.ctx.owner_shard) {
        o << h.ctx.owner_shard << "}";
    } else {
        o << "nullopt}";
    }
    o << "}";
    return o;
}

std::ostream&
operator<<(std::ostream& os, const record_batch::compressed_records& records) {
    return ss::fmt_print(
      os, "{{compressed_records: size_bytes={}}}", records.size_bytes());
}

std::ostream& operator<<(std::ostream& os, const record_batch& batch) {
    os << "{record_batch=" << batch.header() << ", records=";
    if (batch.compressed()) {
        os << "{compressed=" << batch.get_compressed_records().size_bytes()
           << "bytes}";
    } else {
        os << "{";
        for (auto& r : batch) {
            os << r;
        }
        os << "}";
    }
    os << "}";
    return os;
}

ss::sstring ntp::path() const {
    return fmt::format("{}/{}/{}", ns(), tp.topic(), tp.partition());
}

std::istream& operator>>(std::istream& i, compression& c) {
    ss::sstring s;
    i >> s;
    c = string_switch<compression>(s)
          .match_all("none", "uncompressed", compression::none)
          .match("gzip", compression::gzip)
          .match("snappy", compression::snappy)
          .match("lz4", compression::lz4)
          .match("zstd", compression::zstd);
    return i;
}

std::ostream& operator<<(std::ostream& o, const model::broker_properties& b) {
    return ss::fmt_print(
      o,
      "{cores {}, mem_available {}, disk_available {}}",
      b.cores,
      b.available_memory,
      b.available_disk,
      b.mount_paths,
      b.etc_props);
}

std::ostream& operator<<(std::ostream& o, const model::broker& b) {
    return ss::fmt_print(
      o,
      "id: {} kafka_api_address: {} rpc_address: {} rack: {} "
      "properties: {}",
      b.id(),
      b.kafka_api_address(),
      b.rpc_address(),
      b.rack(),
      b.properties());
}
} // namespace model
