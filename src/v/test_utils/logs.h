#pragma once
#include "model/record_batch_reader.h"
#include "storage/log_manager.h"
namespace tests {

storage::log_manager make_log_mgr(sstring base_dir) {
    return storage::log_manager(
      {.base_dir = base_dir,
       .max_segment_size = 1 << 30,
       .should_sanitize = storage::log_config::sanitize_files::yes});
}

future<> persist_log_file(
  sstring base_dir,
  model::ntp file_ntp,
  std::vector<model::record_batch> batches) {
    return do_with(
      make_log_mgr(base_dir),
      [file_ntp = std::move(file_ntp),
       batches = std::move(batches)](storage::log_manager& mgr) mutable {
          return mgr.manage(file_ntp)
            .then([b = std::move(batches)](storage::log_ptr log) mutable {
                auto reader = model::make_memory_record_batch_reader(
                  std::move(b));
                return log->append(
                  std::move(reader),
                  storage::log_append_config{
                    storage::log_append_config::fsync::yes,
                    default_priority_class(),
                    model::no_timeout});
            })
            .finally([&mgr] { return mgr.stop(); })
            .discard_result();
      });
}

struct to_vector_consumer {
    future<stop_iteration> operator()(model::record_batch&& batch) {
        _batches.push_back(std::move(batch));
        return make_ready_future<stop_iteration>(stop_iteration::no);
    }

    std::vector<model::record_batch> end_of_stream() {
        return std::move(_batches);
    }

private:
    std::vector<model::record_batch> _batches;
};

future<std::vector<model::record_batch>>
read_log_file(sstring base_dir, model::ntp file_ntp) {
    return do_with(
      make_log_mgr(base_dir),
      [file_ntp = std::move(file_ntp)](storage::log_manager& mgr) mutable {
          return mgr.manage(file_ntp).then([](storage::log_ptr log) mutable {
              auto reader = log->make_reader(
                storage::log_reader_config{.start_offset = model::offset(0),
                                           .max_bytes = (1 << 30),
                                           .min_bytes = 0,
                                           .prio = default_priority_class()});
              return do_with(
                std::move(reader), [](model::record_batch_reader& reader) {
                    return reader.consume(
                      to_vector_consumer(), model::no_timeout);
                });
          });
      });
}
} // namespace tests