/* Copyright 2017 Istio Authors. All Rights Reserved.
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
#include "src/envoy/utils/grpc_transport.h"

using ::google::protobuf::util::Status;
using StatusCode = ::google::protobuf::util::error::Code;

namespace Envoy {
namespace Utils {
namespace {

// gRPC request timeout
const std::chrono::milliseconds kGrpcRequestTimeoutMs(5000);

// HTTP trace headers that should pass to gRPC metadata from origin request.
// x-request-id is added for easy debugging.
const Http::LowerCaseString kRequestId("x-request-id");
const Http::LowerCaseString kB3TraceId("x-b3-traceid");
const Http::LowerCaseString kB3SpanId("x-b3-spanid");
const Http::LowerCaseString kB3ParentSpanId("x-b3-parentspanid");
const Http::LowerCaseString kB3Sampled("x-b3-sampled");
const Http::LowerCaseString kB3Flags("x-b3-flags");
const Http::LowerCaseString kOtSpanContext("x-ot-span-context");

inline void CopyHeaderEntry(const Http::HeaderEntry* entry,
                            const Http::LowerCaseString& key,
                            Http::HeaderMap& headers) {
  if (entry) {
    std::string val(entry->value().c_str(), entry->value().size());
    headers.addReferenceKey(key, val);
  }
}

}  // namespace

template <class RequestType, class ResponseType>
GrpcTransport<RequestType, ResponseType>::GrpcTransport(
    Grpc::AsyncClientPtr async_client, const RequestType& request,
    const Http::HeaderMap* headers, ResponseType* response,
    istio::mixerclient::DoneFunc on_done)
    : async_client_(std::move(async_client)),
      headers_(headers),
      response_(response),
      on_done_(on_done),
      request_(async_client_->send(
          descriptor(), request, *this, Tracing::NullSpan::instance(),
          Optional<std::chrono::milliseconds>(kGrpcRequestTimeoutMs))) {
  ENVOY_LOG(debug, "Sending {} request: {}", descriptor().name(),
            request.DebugString());
}

template <class RequestType, class ResponseType>
void GrpcTransport<RequestType, ResponseType>::onCreateInitialMetadata(
    Http::HeaderMap& metadata) {
  if (!headers_) return;

  CopyHeaderEntry(headers_->RequestId(), kRequestId, metadata);
  CopyHeaderEntry(headers_->XB3TraceId(), kB3TraceId, metadata);
  CopyHeaderEntry(headers_->XB3SpanId(), kB3SpanId, metadata);
  CopyHeaderEntry(headers_->XB3ParentSpanId(), kB3ParentSpanId, metadata);
  CopyHeaderEntry(headers_->XB3Sampled(), kB3Sampled, metadata);
  CopyHeaderEntry(headers_->XB3Flags(), kB3Flags, metadata);

  // This one is NOT inline, need to do linar search.
  CopyHeaderEntry(headers_->get(kOtSpanContext), kOtSpanContext, metadata);
}

template <class RequestType, class ResponseType>
void GrpcTransport<RequestType, ResponseType>::onSuccess(
    std::unique_ptr<ResponseType>&& response, Tracing::Span&) {
  ENVOY_LOG(debug, "{} response: {}", descriptor().name(),
            response->DebugString());
  response->Swap(response_);
  on_done_(Status::OK);
  delete this;
}

template <class RequestType, class ResponseType>
void GrpcTransport<RequestType, ResponseType>::onFailure(
    Grpc::Status::GrpcStatus status, const std::string& message,
    Tracing::Span&) {
  ENVOY_LOG(debug, "{} failed with code: {}, {}", descriptor().name(), status,
            message);
  on_done_(Status(static_cast<StatusCode>(status), message));
  delete this;
}

template <class RequestType, class ResponseType>
void GrpcTransport<RequestType, ResponseType>::Cancel() {
  ENVOY_LOG(debug, "Cancel gRPC request {}", descriptor().name());
  delete this;
}

template <class RequestType, class ResponseType>
typename GrpcTransport<RequestType, ResponseType>::Func
GrpcTransport<RequestType, ResponseType>::GetFunc(
    Upstream::ClusterManager& cm, const std::string& cluster_name,
    const Http::HeaderMap* headers) {
  return [&cm, cluster_name, headers](const RequestType& request,
                                      ResponseType* response,
                                      istio::mixerclient::DoneFunc on_done)
             -> istio::mixerclient::CancelFunc {
               auto transport = new GrpcTransport<RequestType, ResponseType>(
                   Grpc::AsyncClientPtr(
                       new Grpc::AsyncClientImpl(cm, cluster_name.c_str())),
                   request, headers, response, on_done);
               return [transport]() { transport->Cancel(); };
             };
}

template <>
const google::protobuf::MethodDescriptor& CheckTransport::descriptor() {
  static const google::protobuf::MethodDescriptor* check_descriptor =
      istio::mixer::v1::Mixer::descriptor()->FindMethodByName("Check");
  ASSERT(check_descriptor);

  return *check_descriptor;
}

template <>
const google::protobuf::MethodDescriptor& ReportTransport::descriptor() {
  static const google::protobuf::MethodDescriptor* report_descriptor =
      istio::mixer::v1::Mixer::descriptor()->FindMethodByName("Report");
  ASSERT(report_descriptor);

  return *report_descriptor;
}

// explicitly instantiate CheckTransport and ReportTransport
template CheckTransport::Func CheckTransport::GetFunc(
    Upstream::ClusterManager& cm, const std::string& cluster_name,
    const Http::HeaderMap* headers);
template ReportTransport::Func ReportTransport::GetFunc(
    Upstream::ClusterManager& cm, const std::string& cluster_name,
    const Http::HeaderMap* headers);

}  // namespace Utils
}  // namespace Envoy
