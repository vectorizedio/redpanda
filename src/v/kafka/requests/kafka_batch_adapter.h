#pragma once

#include "bytes/iobuf_parser.h"
#include "model/record.h"
#include "model/record_batch_reader.h"
#include "utils/vint.h"

namespace kafka {

namespace internal {

constexpr size_t kafka_header_size = sizeof(int64_t) + // base offset
                                     sizeof(int32_t) + // batch length
                                     sizeof(int32_t) + // partition leader epoch
                                     sizeof(int8_t) +  // magic
                                     sizeof(int32_t) + // crc
                                     sizeof(int16_t) + // attributes
                                     sizeof(int32_t) + // last offset delta
                                     sizeof(int64_t) + // first timestamp
                                     sizeof(int64_t) + // max timestamp
                                     sizeof(int64_t) + // producer id
                                     sizeof(int16_t) + // producer epoch
                                     sizeof(int32_t) + // base sequence
                                     sizeof(int32_t);  // num records

} // namespace internal

class kafka_batch_adapter {
public:
    // TODO: change adapt to return a value of whether or not parsing
    // was successful. at the moment we use exceptions to abort parsing
    void adapt(iobuf&&);

    bool has_transactional = false;
    bool has_idempotent = false;
    bool has_non_v2_magic = false;
    std::vector<model::record_batch> batches;

private:
    model::record_batch_header read_header(iobuf_parser&);
};

} // namespace kafka
