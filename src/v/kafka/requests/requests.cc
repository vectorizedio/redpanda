#include "kafka/requests/requests.h"

#include "kafka/requests/api_versions_request.h"
#include "kafka/requests/create_topics_request.h"
#include "kafka/requests/fetch_request.h"
#include "kafka/requests/find_coordinator_request.h"
#include "kafka/requests/heartbeat_request.h"
#include "kafka/requests/join_group_request.h"
#include "kafka/requests/leave_group_request.h"
#include "kafka/requests/list_groups_request.h"
#include "kafka/requests/list_offsets_request.h"
#include "kafka/requests/metadata_request.h"
#include "kafka/requests/offset_commit_request.h"
#include "kafka/requests/offset_fetch_request.h"
#include "kafka/requests/produce_request.h"
#include "kafka/requests/request_context.h"
#include "kafka/requests/sync_group_request.h"
#include "utils/to_string.h"
#include "vlog.h"

#include <seastar/core/print.hh>
#include <seastar/util/log.hh>

namespace kafka {

/**
 * Dispatch request with version bounds checking.
 */
template<typename Request>
CONCEPT(requires(KafkaRequest<Request>))
struct process_dispatch {
    static ss::future<response_ptr>
    process(request_context&& ctx, ss::smp_service_group g) {
        if (
          ctx.header().version < Request::min_supported
          || ctx.header().version > Request::max_supported) {
            return ss::make_exception_future<response_ptr>(
              std::runtime_error(fmt::format(
                "Unsupported version {} for {} API",
                ctx.header().version,
                Request::name)));
        }
        return Request::process(std::move(ctx), std::move(g));
    }
};

/**
 * Dispatch API versions request without version checks.
 *
 * The version bounds checks are not applied to this request because the client
 * does not yet know what versions this server supports. The api versions
 * request is used by a client to query this information.
 */
template<>
struct process_dispatch<api_versions_api> {
    static ss::future<response_ptr>
    process(request_context&& ctx, ss::smp_service_group g) {
        return api_versions_api::process(std::move(ctx), std::move(g));
    }
};

template<typename Request>
CONCEPT(requires(KafkaRequest<Request>))
ss::future<response_ptr> do_process(
  request_context&& ctx, ss::smp_service_group g) {
    vlog(
      klog.trace,
      "Processing request {}({} v{}) for {}",
      Request::name,
      ctx.header().key,
      ctx.header().version,
      ctx.header().client_id.value_or(std::string_view("unset-client-id")));

    return process_dispatch<Request>::process(std::move(ctx), std::move(g));
}

ss::future<response_ptr>
process_request(request_context&& ctx, ss::smp_service_group g) {
    switch (ctx.header().key) {
    case api_versions_api::key:
        return do_process<api_versions_api>(std::move(ctx), std::move(g));
    case metadata_api::key:
        return do_process<metadata_api>(std::move(ctx), std::move(g));
    case list_groups_api::key:
        return do_process<list_groups_api>(std::move(ctx), std::move(g));
    case find_coordinator_api::key:
        return do_process<find_coordinator_api>(std::move(ctx), std::move(g));
    case offset_fetch_api::key:
        return do_process<offset_fetch_api>(std::move(ctx), std::move(g));
    case produce_api::key:
        return do_process<produce_api>(std::move(ctx), std::move(g));
    case list_offsets_api::key:
        return do_process<list_offsets_api>(std::move(ctx), std::move(g));
    case offset_commit_api::key:
        return do_process<offset_commit_api>(std::move(ctx), std::move(g));
    case fetch_api::key:
        return do_process<fetch_api>(std::move(ctx), std::move(g));
    case join_group_api::key:
        return do_process<join_group_api>(std::move(ctx), std::move(g));
    case heartbeat_api::key:
        return do_process<heartbeat_api>(std::move(ctx), std::move(g));
    case leave_group_api::key:
        return do_process<leave_group_api>(std::move(ctx), std::move(g));
    case sync_group_api::key:
        return do_process<sync_group_api>(std::move(ctx), std::move(g));
    case create_topics_api::key:
        return do_process<create_topics_api>(std::move(ctx), std::move(g));
    };
    return ss::make_exception_future<response_ptr>(
      std::runtime_error(fmt::format("Unsupported API {}", ctx.header().key)));
}

std::ostream& operator<<(std::ostream& os, const request_header& header) {
    fmt::print(
      os,
      "{{request_header: {}, {}, {{correlation_id: {}}}, ",
      header.key,
      header.version,
      header.correlation);
    if (header.client_id) {
        fmt::print(os, "{{client_id: {}}}}}", header.client_id);
    } else {
        return os << "{no client_id}}";
    }
    return os;
}

} // namespace kafka
