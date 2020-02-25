/* Copyright 2019 Istio Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "extensions/stackdriver/edges/edge_reporter.h"

#include "extensions/stackdriver/common/constants.h"
#include "extensions/stackdriver/edges/edges.pb.h"

#ifndef NULL_PLUGIN
#include "api/wasm/cpp/proxy_wasm_intrinsics.h"
#else
#include "extensions/common/wasm/null/null_plugin.h"
#endif

namespace Extensions {
namespace Stackdriver {
namespace Edges {

using google::cloud::meshtelemetry::v1alpha1::ReportTrafficAssertionsRequest;
using google::cloud::meshtelemetry::v1alpha1::TrafficAssertion;
using google::cloud::meshtelemetry::v1alpha1::
    TrafficAssertion_Protocol_PROTOCOL_GRPC;
using google::cloud::meshtelemetry::v1alpha1::
    TrafficAssertion_Protocol_PROTOCOL_HTTP;
using google::cloud::meshtelemetry::v1alpha1::
    TrafficAssertion_Protocol_PROTOCOL_HTTPS;
using google::cloud::meshtelemetry::v1alpha1::
    TrafficAssertion_Protocol_PROTOCOL_TCP;
using google::cloud::meshtelemetry::v1alpha1::WorkloadInstance;

namespace {
void instanceFromMetadata(const ::wasm::common::NodeInfo& node_info,
                          WorkloadInstance* instance) {
  // TODO(douglas-reid): support more than just kubernetes instances
  if ((node_info.name().length() > 0) &&
      (node_info.namespace_().length() > 0)) {
    absl::StrAppend(instance->mutable_uid(), "kubernetes://", node_info.name(),
                    ".", node_info.namespace_());
  }
  // TODO(douglas-reid): support more than just GCP ?
  const auto& platform_metadata = node_info.platform_metadata();
  const auto location_iter = platform_metadata.find(Common::kGCPLocationKey);
  if (location_iter != platform_metadata.end()) {
    instance->set_location(location_iter->second);
  }
  const auto cluster_iter = platform_metadata.find(Common::kGCPClusterNameKey);
  if (cluster_iter != platform_metadata.end()) {
    instance->set_cluster_name(cluster_iter->second);
  }

  instance->set_owner_uid(node_info.owner());
  instance->set_workload_name(node_info.workload_name());
  instance->set_workload_namespace(node_info.namespace_());
};

}  // namespace

EdgeReporter::EdgeReporter(const ::wasm::common::NodeInfo& local_node_info,
                           std::unique_ptr<MeshEdgesServiceClient> edges_client,
                           int batch_size)
    : EdgeReporter(local_node_info, std::move(edges_client), batch_size, []() {
        return TimeUtil::NanosecondsToTimestamp(getCurrentTimeNanoseconds());
      }) {}

EdgeReporter::EdgeReporter(const ::wasm::common::NodeInfo& local_node_info,
                           std::unique_ptr<MeshEdgesServiceClient> edges_client,
                           int batch_size, TimestampFn now)
    : edges_client_(std::move(edges_client)),
      now_(now),
      max_assertions_per_request_(batch_size) {
  current_request_ = std::make_unique<ReportTrafficAssertionsRequest>();
  epoch_current_request_ = std::make_unique<ReportTrafficAssertionsRequest>();

  const auto iter =
      local_node_info.platform_metadata().find(Common::kGCPProjectKey);
  if (iter != local_node_info.platform_metadata().end()) {
    current_request_->set_parent("projects/" + iter->second);
    epoch_current_request_->set_parent("projects/" + iter->second);
  }

  std::string mesh_id = local_node_info.mesh_id();
  if (mesh_id.empty()) {
    mesh_id = "unknown";
  }
  current_request_->set_mesh_uid(mesh_id);
  epoch_current_request_->set_mesh_uid(mesh_id);

  instanceFromMetadata(local_node_info, &node_instance_);
};

EdgeReporter::~EdgeReporter() {}

// ONLY inbound
void EdgeReporter::addEdge(const ::Wasm::Common::RequestInfo& request_info,
                           const std::string& peer_metadata_id_key,
                           const ::wasm::common::NodeInfo& peer_node_info) {
  const auto& peer = known_peers_.emplace(peer_metadata_id_key);
  if (!peer.second) {
    // peer edge already exists
    return;
  }

  auto* traffic_assertions = current_request_->mutable_traffic_assertions();
  auto* edge = traffic_assertions->Add();

  edge->set_destination_service_name(request_info.destination_service_name);
  edge->set_destination_service_namespace(node_instance_.workload_namespace());
  instanceFromMetadata(peer_node_info, edge->mutable_source());
  edge->mutable_destination()->CopyFrom(node_instance_);

  auto protocol = request_info.request_protocol;
  if (protocol == "http" || protocol == "HTTP") {
    edge->set_protocol(TrafficAssertion_Protocol_PROTOCOL_HTTP);
  } else if (protocol == "https" || protocol == "HTTPS") {
    edge->set_protocol(TrafficAssertion_Protocol_PROTOCOL_HTTPS);
  } else if (protocol == "grpc" || protocol == "GRPC") {
    edge->set_protocol(TrafficAssertion_Protocol_PROTOCOL_GRPC);
  } else {
    edge->set_protocol(TrafficAssertion_Protocol_PROTOCOL_TCP);
  }

  auto* epoch_assertion =
      epoch_current_request_->mutable_traffic_assertions()->Add();
  epoch_assertion->MergeFrom(*edge);

  if (current_request_->traffic_assertions_size() >
      max_assertions_per_request_) {
    rotateCurrentRequest();
  }

  if (epoch_current_request_->traffic_assertions_size() >
      max_assertions_per_request_) {
    rotateEpochRequest();
  }

};  // namespace Edges

void EdgeReporter::reportEdges(bool full_epoch) {
  flush(full_epoch);
  auto timestamp = now_();
  if (full_epoch) {
    for (auto& req : epoch_queued_requests_) {
      // update all assertions
      auto assertion = req.get();
      *assertion->mutable_timestamp() = timestamp;
      edges_client_->reportTrafficAssertions(*assertion);
    }
    epoch_queued_requests_.clear();
    current_queued_requests_.clear();
  } else {
    for (auto& req : current_queued_requests_) {
      auto assertion = req.get();
      *assertion->mutable_timestamp() = timestamp;
      edges_client_->reportTrafficAssertions(*assertion);
    }
    current_queued_requests_.clear();
  }
};

void EdgeReporter::flush(bool flush_epoch) {
  rotateCurrentRequest();
  if (flush_epoch) {
    rotateEpochRequest();
    known_peers_.clear();
  }
}

void EdgeReporter::rotateCurrentRequest() {
  if (current_request_->traffic_assertions_size() == 0) {
    return;
  }
  std::unique_ptr<ReportTrafficAssertionsRequest> queued_request =
      std::make_unique<ReportTrafficAssertionsRequest>();
  queued_request->set_parent(current_request_->parent());
  queued_request->set_mesh_uid(current_request_->mesh_uid());
  current_request_.swap(queued_request);
  current_queued_requests_.emplace_back(std::move(queued_request));
}

void EdgeReporter::rotateEpochRequest() {
  if (epoch_current_request_->traffic_assertions_size() == 0) {
    return;
  }
  std::unique_ptr<ReportTrafficAssertionsRequest> queued_request =
      std::make_unique<ReportTrafficAssertionsRequest>();
  queued_request->set_parent(epoch_current_request_->parent());
  queued_request->set_mesh_uid(epoch_current_request_->mesh_uid());
  epoch_current_request_.swap(queued_request);
  epoch_queued_requests_.emplace_back(std::move(queued_request));
}

}  // namespace Edges
}  // namespace Stackdriver
}  // namespace Extensions