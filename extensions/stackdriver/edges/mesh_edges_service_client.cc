
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

#include "extensions/stackdriver/edges/mesh_edges_service_client.h"

#include "extensions/stackdriver/common/constants.h"
#include "extensions/stackdriver/common/utils.h"
#include "google/protobuf/util/time_util.h"

#ifdef NULL_PLUGIN
namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Null {
namespace Plugin {

using envoy::config::core::v3::GrpcService;
using Envoy::Extensions::Common::Wasm::Null::Plugin::GrpcStatus;
using Envoy::Extensions::Common::Wasm::Null::Plugin::logDebug;
using Envoy::Extensions::Common::Wasm::Null::Plugin::logWarn;
using Envoy::Extensions::Common::Wasm::Null::Plugin::StringView;
#endif

// TODO(douglas-reid): confirm values here
constexpr char kMeshTelemetryService[] = "meshtelemetry.googleapis.com";
constexpr char kMeshEdgesService[] =
    "google.cloud.meshtelemetry.v1alpha1.MeshEdgesService";
constexpr char kReportTrafficAssertions[] = "ReportTrafficAssertions";
constexpr int kDefaultTimeoutMillisecond = 10000;  // 10 seconds

namespace Extensions {
namespace Stackdriver {
namespace Edges {

using google::cloud::meshtelemetry::v1alpha1::ReportTrafficAssertionsRequest;
using google::protobuf::util::TimeUtil;

MeshEdgesServiceClientImpl::MeshEdgesServiceClientImpl(
    RootContext* root_context, const std::string& edges_endpoint,
    const std::string& sts_port, const std::string& test_token_file,
    const std::string& test_root_pem_file)
    : context_(root_context) {
  success_callback_ = [](size_t) {
    // TODO(douglas-reid): improve logging message.
    logDebug(
        "successfully sent MeshEdgesService ReportTrafficAssertionsRequest");
  };

  failure_callback_ = [](GrpcStatus status) {
    // TODO(douglas-reid): add retry (and other) logic
    logWarn("MeshEdgesService ReportTrafficAssertionsRequest failure: " +
            std::to_string(static_cast<int>(status)) + " " +
            getStatus().second->toString());
  };

  GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("mesh_edges");

  // use application default creds and default target
  grpc_service.mutable_google_grpc()->set_target_uri(
      edges_endpoint.empty() ? kMeshTelemetryService : edges_endpoint);
  if (sts_port.empty()) {
    // Security token exchange is not enabled. Use default GCE credential.
    grpc_service.mutable_google_grpc()
        ->add_call_credentials()
        ->mutable_google_compute_engine();
  } else {
    ::Extensions::Stackdriver::Common::setSTSCallCredentialOptions(
        grpc_service.mutable_google_grpc()
            ->add_call_credentials()
            ->mutable_sts_service(),
        sts_port,
        test_token_file.empty()
            ? ::Extensions::Stackdriver::Common::kSTSSubjectTokenPath
            : test_token_file);
  }
  grpc_service.mutable_google_grpc()
      ->mutable_channel_credentials()
      ->mutable_ssl_credentials()
      ->mutable_root_certs()
      ->set_filename(
          test_root_pem_file.empty()
              ? ::Extensions::Stackdriver::Common::kDefaultRootCertFile
              : test_root_pem_file);

  grpc_service.SerializeToString(&grpc_service_);
};

void MeshEdgesServiceClientImpl::reportTrafficAssertions(
    const ReportTrafficAssertionsRequest& request) const {
  LOG_TRACE("mesh edge services client: sending request '" +
            request.DebugString() + "'");

  context_->grpcSimpleCall(
      grpc_service_, kMeshEdgesService, kReportTrafficAssertions, request,
      kDefaultTimeoutMillisecond, success_callback_, failure_callback_);
};

}  // namespace Edges
}  // namespace Stackdriver
}  // namespace Extensions

#ifdef NULL_PLUGIN
}  // namespace plugin
}  // namespace null
}  // namespace wasm
}  // namespace common
}  // namespace extensions
}  // namespace envoy
#endif
