#include "rpc/connection_cache.h"

#include "rpc/backoff_policy.h"

#include <fmt/format.h>

#include <chrono>

namespace rpc {

/// \brief needs to be a future, because mutations may come from different
/// fibers and they need to be synchronized
ss::future<> connection_cache::emplace(
  model::node_id n,
  rpc::transport_configuration c,
  backoff_policy backoff_policy) {
    return with_semaphore(
      _sem,
      1,
      [this,
       n,
       c = std::move(c),
       backoff_policy = std::move(backoff_policy)]() mutable {
          if (_cache.find(n) != _cache.end()) {
              return;
          }
          _cache.emplace(
            n,
            ss::make_lw_shared<rpc::reconnect_transport>(
              std::move(c), std::move(backoff_policy)));
      });
}
ss::future<> connection_cache::remove(model::node_id n) {
    return ss::with_semaphore(
             _sem,
             1,
             [this, n]() -> transport_ptr {
                 auto it = _cache.find(n);
                 if (it == _cache.end()) {
                     return nullptr;
                 }
                 auto ptr = it->second;
                 _cache.erase(it);
                 return ptr;
             })
      .then([](transport_ptr ptr) {
          if (!ptr) {
              return ss::now();
          }
          return ptr->stop().finally([ptr] {});
      });
}

/// \brief closes all client connections
ss::future<> connection_cache::stop() {
    return parallel_for_each(_cache, [](auto& it) {
        auto& [_, cli] = it;
        return cli->stop();
    });
}

} // namespace rpc
