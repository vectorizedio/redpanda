#pragma once

#include "cluster/types.h"
#include "model/metadata.h"
#include "random/fast_prng.h"
#include "utils/intrusive_list_helpers.h"

#include <fmt/ostream.h>

#include <vector>

namespace cluster {
class partition_allocator;

class allocation_node {
public:
    static constexpr const uint32_t core0_extra_weight = 2;
    // TODO make configurable
    static constexpr const uint32_t max_allocations_per_core = 7000;

    allocation_node(
      model::broker b,
      uint32_t cpus,
      std::unordered_map<sstring, sstring> labels)
      : _node(std::move(b))
      , _weights(cpus)
      , _machine_labels(std::move(labels)) {
        // add extra weights to core 0
        _weights[0] = core0_extra_weight;
        _partition_capacity = (cpus * max_allocations_per_core)
                              - core0_extra_weight;
    }
    allocation_node(allocation_node&& o) noexcept
      : _node(std::move(o._node))
      , _weights(std::move(o._weights))
      , _partition_capacity(o._partition_capacity)
      , _machine_labels(std::move(o._machine_labels))
      , _hook(std::move(o._hook)) {
    }
    uint32_t cpus() const {
        return _weights.size();
    }
    model::node_id id() const {
        return _node.id();
    }
    uint32_t partition_capacity() const {
        return _partition_capacity;
    }

private:
    friend partition_allocator;

    bool is_full() const {
        for (uint32_t w : _weights) {
            if (w != max_allocations_per_core) {
                return false;
            }
        }
        return true;
    }
    uint32_t allocate() {
        auto it = std::min_element(_weights.begin(), _weights.end());
        (*it)++; // increment the weights
        _partition_capacity--;
        return std::distance(_weights.begin(), it);
    }
    void deallocate(uint32_t core) {
        _partition_capacity++;
        _weights[core]--;
    }
    const std::unordered_map<sstring, sstring>& machine_labels() const {
        return _machine_labels;
    }
    const model::broker& node() const {
        return _node;
    }

    model::broker _node;
    /// each index is a CPU. A weight is roughly the number of assigments
    std::vector<uint32_t> _weights;
    uint32_t _partition_capacity{0};
    /// generated by `rpk` usually in /etc/redpanda/machine_labels.json
    std::unordered_map<sstring, sstring> _machine_labels;

    // for partition_allocator
    safe_intrusive_list_hook _hook;
};

struct partition_allocator_tester;
class partition_allocator {
public:
    using value_type = allocation_node;
    using ptr = std::unique_ptr<value_type>;
    using underlying_t = std::vector<ptr>;
    using iterator = underlying_t::iterator;
    using cil_t = counted_intrusive_list<value_type, &allocation_node::_hook>;

    /// should only be initialized _after_ we become the leader so we know we
    /// are up to date, and have the highest known group_id ever assigned
    /// reset to nullptr when no longer leader
    partition_allocator(raft::group_id highest_known_group)
      : _highest_group(highest_known_group) {
        _rr = _available_machines.end();
    }
    void register_node(ptr n) {
        _machines.push_back(std::move(n));
        _available_machines.push_back(*_machines.back());
    }

    /// best effort placement.
    /// kafka/common/protocol/Errors.java does not have a way to
    /// represent failed allocation yet. Up to caller to interpret
    /// how to use a nullopt value
    std::optional<std::vector<partition_assignment>>
    allocate(const topic_configuration&);

    /// best effort. Does not throw if we cannot find the old partition
    void deallocate(const model::broker_shard&);
    ~partition_allocator() {
        _available_machines.clear();
        _rr = _available_machines.end();
    }

private:
    friend partition_allocator_tester;
    /// rolls back partition assignment, only decrementing
    /// raft-group by distinct raft-group counts
    /// assumes sorted in raft-group order
    void rollback(const std::vector<partition_assignment>& pa);
    void rollback(const std::vector<model::broker_shard>& v);

    std::optional<std::vector<model::broker_shard>>
    allocate_replicas(model::ntp, int16_t replication_factor);

    [[gnu::always_inline]] inline cil_t::iterator& round_robin_ptr() {
        if (_rr == _available_machines.end()) {
            _rr = _available_machines.begin();
        }
        return _rr;
    }
    raft::group_id _highest_group;

    cil_t::iterator _rr; // round robin
    cil_t _available_machines;
    underlying_t _machines;

    // for testing
    void test_only_saturate_all_machines();
    uint32_t test_only_max_cluster_allocation_partition_capacity() const;
};

} // namespace cluster
