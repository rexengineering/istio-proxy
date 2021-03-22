#pragma once
#include <iostream>
#include <stdio.h>
#include <map>

#include "envoy/http/filter.h"
#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"
#include "envoy/http/async_client.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"
#include "common/common/random_generator.h"

#include "src/envoy/http/bavs_2.0/newbavs.pb.h"

namespace Envoy {
namespace Http {

class BavsCallbacks : public Http::AsyncClient::Callbacks, public Http::AsyncClient::StreamCallbacks {
public:
    ~BavsCallbacks() = default;
    BavsCallbacks() {}

  void onSuccess(const AsyncClient::Request&, Http::ResponseMessagePtr&& response) override {
    std::cout << "onSuccess: " << response->body().toString() << std::endl;
  }

  void onFailure(const AsyncClient::Request&, AsyncClient::FailureReason) override {
    std::cout << "failed" << std::endl;
  }
  void onData() override {}
  void onHeaders() override {}
  void 

  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}
};

class BavsFilter20 : public PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
private:
    Upstream::ClusterManager& cluster_manager_;
    bool is_workflow_;
    bool successful_response_;
    // std::vector<std::string> req_cb_keys_;
    std::string instance_id_;
    std::string wf_id_;
    std::unique_ptr<RequestMessageImpl> message_;
    std::map<std::string, std::string> saved_headers_;
    std::string service_cluster_;
    // RequestHeaderMapImpl* request_headers_;

public:
    BavsFilter20(Upstream::ClusterManager& cluster_manager)
    : cluster_manager_(cluster_manager), is_workflow_(false), successful_response_(true),
      service_cluster_("outbound|5000||start-underpants-63d700b2.default.svc.cluster.local") {};
      //inbound|5000||

    FilterDataStatus decodeData(Buffer::Instance&, bool);
    FilterDataStatus encodeData(Buffer::Instance&, bool);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool);
};

} // namespace Http
} // namespace Envoy
