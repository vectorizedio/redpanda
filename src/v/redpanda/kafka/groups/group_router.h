#pragma once
#include "redpanda/kafka/groups/types.h"
#include "redpanda/kafka/requests/heartbeat_request.h"
#include "redpanda/kafka/requests/join_group_request.h"
#include "redpanda/kafka/requests/leave_group_request.h"
#include "redpanda/kafka/requests/sync_group_request.h"
#include "seastarx.h"
#include "utils/concepts-enabled.h"

#include <seastar/core/reactor.hh>
#include <seastar/core/scheduling.hh>
#include <seastar/core/sharded.hh>

namespace kafka::groups {

// clang-format off
CONCEPT(
template <typename T>
concept GroupManager =
requires(
  T m,
  requests::join_group_request&& join_request,
  requests::sync_group_api&& sync_request,
  requests::heartbeat_api&& heartbeat_request,
  requests::leave_group_request&& leave_request) {

    { m.join_group(std::move(join_request)) } ->
        future<requests::join_group_response>;

    { m.sync_group(std::move(sync_request)) } ->
        future<requests::sync_group_response>;

    { m.heartbeat(std::move(heartbeat_request)) } ->
        future<requests::heartbeat_response>;

    { m.leave_group(std::move(leave_request)) } ->
        future<requests::leave_group_response>;
};

template<typename T>
concept GroupShardMapper = requires(T m, const kafka::group_id& group_id) {
    { m.shard_for(group_id) } -> seastar::shard_id;
};
)
// clang-format on

/// \brief Forward group operations to the owning core.
template<typename GroupMgr, typename Shards>
CONCEPT(requires GroupManager<GroupMgr>&& GroupShardMapper<Shards>)
class group_router final {
public:
    /// \brief Create an instance of the group router.
    ///
    /// The constructor takes a reference to sharded<Shards> but stores a
    /// reference to the local core's Shards instance. When instantiating
    /// the group router via sharded<group_router>::start, the constructor will
    /// run on each core so sharded<>::local() is valid.
    group_router(
      scheduling_group sched_group,
      smp_service_group smp_group,
      sharded<GroupMgr>& group_manager,
      sharded<Shards>& shards)
      : _sg(sched_group)
      , _ssg(smp_group)
      , _group_manager(group_manager)
      , _shards(shards.local()) {
    }

    future<requests::join_group_response>
    join_group(requests::join_group_request&& request) {
        auto shard = _shards.shard_for(request.group_id);
        return with_scheduling_group(
          _sg, [this, shard, request = std::move(request)]() mutable {
              return _group_manager.invoke_on(
                shard,
                _ssg,
                [request = std::move(request)](GroupMgr& m) mutable {
                    return m.join_group(std::move(request));
                });
          });
    }

    future<requests::sync_group_response>
    sync_group(requests::sync_group_api&& request) {
        auto shard = _shards.shard_for(request.group_id);
        return with_scheduling_group(
          _sg, [this, shard, request = std::move(request)]() mutable {
              return _group_manager.invoke_on(
                shard,
                _ssg,
                [request = std::move(request)](GroupMgr& m) mutable {
                    return m.sync_group(std::move(request));
                });
          });
    }

    future<requests::heartbeat_response>
    heartbeat(requests::heartbeat_api&& request) {
        auto shard = _shards.shard_for(request.group_id);
        return with_scheduling_group(
          _sg, [this, shard, request = std::move(request)]() mutable {
              return _group_manager.invoke_on(
                shard,
                _ssg,
                [request = std::move(request)](GroupMgr& m) mutable {
                    return m.heartbeat(std::move(request));
                });
          });
    }

    future<requests::leave_group_response>
    leave_group(requests::leave_group_request&& request) {
        auto shard = _shards.shard_for(request.group_id);
        return with_scheduling_group(
          _sg, [this, shard, request = std::move(request)]() mutable {
              return _group_manager.invoke_on(
                shard,
                _ssg,
                [request = std::move(request)](GroupMgr& m) mutable {
                    return m.leave_group(std::move(request));
                });
          });
    }

private:
    scheduling_group _sg;
    smp_service_group _ssg;
    sharded<GroupMgr>& _group_manager;
    Shards& _shards;
};

} // namespace kafka::groups
