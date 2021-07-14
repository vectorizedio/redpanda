/*
 * Copyright 2020 Vectorized, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */

#pragma once
#include "bytes/iobuf.h"
#include "model/fundamental.h"
#include "seastarx.h"
#include "storage/multi_snapshot.h"

#include <seastar/core/file.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/seastar.hh>
#include <seastar/util/log.hh>

#include <filesystem>

namespace storage {

class snapshot_manager {
public:
    static constexpr const char* default_snapshot_filename = "snapshot";

    snapshot_manager(
      std::filesystem::path dir,
      ss::sstring filename,
      ss::io_priority_class io_prio) noexcept
      : _filename(filename)
      , _snapshot(filename, std::move(dir), io_prio) {}

    ss::future<std::optional<snapshot_reader>> open_snapshot() {
        return _snapshot.open_snapshot(_filename);
    }

    ss::future<snapshot_writer> start_snapshot() {
        return _snapshot.start_snapshot(_filename);
    }
    ss::future<> finish_snapshot(snapshot_writer& writer) {
        return _snapshot.finish_snapshot(writer);
    }

    std::filesystem::path snapshot_path() const {
        return _snapshot.snapshot_path(_filename);
    }

    ss::future<> remove_partial_snapshots() {
        return _snapshot.remove_partial_snapshots();
    }

    ss::future<> remove_snapshot() {
        return _snapshot.remove_snapshot(_filename);
    }

private:
    ss::sstring _filename;
    multi_snapshot_manager _snapshot;
};

} // namespace storage
