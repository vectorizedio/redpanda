#pragma once
#include "redpanda/kafka/requests/topics/types.h"
#include "redpanda/kafka/requests/topics/validators.h"
#include "seastarx.h"

#include <boost/container/flat_map.hpp>

/// All of the Kafka Topic-related APIs have the same structure of
/// request/response messages. The request always contains list of
/// request specific properties tagged with topic name.
/// And additional fields depending on request type. The response contains
/// a list of errors for those topics for which requested operation failed.
/// The validation must be perform per 'topic' not the request as a whole.
/// This is a set of functions allowing to easily validate and generate errors
/// for topic request items.

namespace kafka::requests {
// clang-format off
CONCEPT(
template<typename T> 
concept TopicRequestItem = requires(T item) {
    { item.topic } -> model::topic_view;
};)
CONCEPT(
template<typename Iterator> 
concept TopicResultIterator = requires (Iterator it) {
    it = topic_op_result{};
} && std::is_same_v<typename Iterator::iterator_category, std::output_iterator_tag>;
)
// clang-format on

/// Generates failed topic_op_result for single topic request item
template<typename T>
CONCEPT(requires TopicRequestItem<T>)
topic_op_result
  generate_error(T item, errors::error_code code, const sstring& msg) {
    return topic_op_result{.topic = model::topic{sstring(item.topic())},
                        .error_code = code,
                        .err_msg = msg};
}

/// Generates successfull topic_op_result for single topic request item
template<typename T>
CONCEPT(requires TopicRequestItem<T>)
topic_op_result generate_successfull_result(T item) {
    return topic_op_result{.topic = model::topic{sstring(item.topic())},
                        .error_code = errors::error_code::none};
}

/// Validates topic requests items in range with predicate,
/// generate errors for not valid items and returns end of valid items range.
/// Generated errors are stored in other range beggining at out_it.
// clang-format off
template<typename Iter, typename ErrIter, typename Predicate>
CONCEPT(
    requires TopicRequestItem<typename Iter::value_type> && 
    TopicResultIterator<ErrIter> &&
    requires (Predicate p, Iter it) {
        {p(*it)}->bool
    }
)
// clang-format on
Iter validate_requests_range(
  Iter begin,
  Iter end,
  ErrIter out_it,
  errors::error_code code,
  const sstring& error_msg,
  Predicate&& p) {
    auto valid_range_end = std::partition(begin, end, p);
    std::transform(
      valid_range_end,
      end,
      out_it,
      [code, &error_msg](const typename Iter::value_type& item) {
          return generate_error(item, code, error_msg);
      });
    return valid_range_end;
}

/// Validates topic request items with validators from the ValidatorTypes
/// type list
template<typename Iter, typename ErrIter, typename... ValidatorTypes>
CONCEPT(requires TopicRequestItem<typename Iter::value_type>)
Iter validate_requests_range(
  Iter begin,
  Iter end,
  ErrIter err_it,
  validator_type_list<typename Iter::value_type, ValidatorTypes...> validators) {
    ((end = validate_requests_range(
        begin,
        end,
        err_it,
        ValidatorTypes::error_code,
        ValidatorTypes::error_message,
        ValidatorTypes::is_valid)),
     ...);
    return end;
}

// Maps errors generated by cluster::controller to objects reperesenting
// Kafka protocol error message
void append_cluster_results(
  const std::vector<cluster::topic_result>& cluster_results,
  std::vector<topic_op_result>& kafka_results) {
    std::transform(
      cluster_results.begin(),
      cluster_results.end(),
      std::back_inserter(kafka_results),
      [](const cluster::topic_result& res) {
          return topic_op_result::from_cluster_topic_result(res);
      });
}

// Converts objects representing KafkaAPI message to objects consumed
// by cluster::controller API
// clang-format off
template<typename KafkaApiTypeIter>
CONCEPT(
    requires TopicRequestItem<typename KafkaApiTypeIter::value_type> &&
    requires(KafkaApiTypeIter it) { 
        it->to_cluster_type(); 
    }
)
// clang-format on
auto to_cluster_type(KafkaApiTypeIter begin, KafkaApiTypeIter end)
  -> std::vector<decltype(begin->to_cluster_type())> {
    std::vector<decltype(begin->to_cluster_type())> cluster_types;
    cluster_types.reserve(std::distance(begin, end));
    std::transform(
      begin,
      end,
      std::back_inserter(cluster_types),
      [](const typename KafkaApiTypeIter::value_type& kafka_type) {
          return kafka_type.to_cluster_type();
      });
    return cluster_types;
}

/// Generate errors for all the request items that topic names
/// are duplicated within given range,
/// the errors are insterted in the range begginning at out_it
// clang-format off
template<typename Iter, typename ErrIter>
CONCEPT(requires TopicRequestItem<typename Iter::value_type> && 
                 TopicResultIterator<ErrIter>)
// clang-format on
Iter validate_range_duplicates(Iter begin, Iter end, ErrIter out_it) {
    using type = typename Iter::value_type;
    boost::container::flat_map<model::topic_view, uint32_t> freq;
    freq.reserve(std::distance(begin, end));
    for (auto const& r : boost::make_iterator_range(begin, end)) {
        freq[r.topic]++;
    }
    auto valid_range_end = std::partition(
      begin, end, [&freq](const type& item) { return freq[item.topic] == 1; });
    std::transform(valid_range_end, end, out_it, [](const type& item) {
        return generate_error(
          item, errors::error_code::invalid_request, "Duplicated topic");
    });
    return valid_range_end;
}

/// Generate NOT_CONTROLLER error for all the request items within given range
/// the errors are insterted in the range begginning at out_it
/// This pattern is used in every Admin request of Kafka protocol.
template<typename Iter, typename ErrIter>
CONCEPT(requires TopicRequestItem<typename Iter::value_type>&&
          TopicResultIterator<ErrIter>)
void generate_not_controller_errors(Iter begin, Iter end, ErrIter out_it) {
    std::transform(
      begin, end, out_it, [](const typename Iter::value_type& item) {
          return generate_error(
            item,
            errors::error_code::not_controller,
            "Current node is not a cluster controller");
      });
}
} // namespace kafka::requests
