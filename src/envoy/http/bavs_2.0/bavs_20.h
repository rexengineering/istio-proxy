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

class BavsCallbacks : public Envoy::Upstream::AsyncStreamCallbacksAndHeaders {
public:
    ~BavsCallbacks() = default;
    BavsCallbacks(std::string id, std::unique_ptr<Http::RequestHeaderMapImpl> headers, Upstream::ClusterManager& cm) 
        : id_(id), headers_(std::move(headers)), cluster_manager_(cm) {
        cluster_manager_.storeCallbacksAndHeaders(id, this);
    }

    void onHeaders(Http::ResponseHeaderMapPtr&&, bool) override {}
    void onData(Buffer::Instance& data, bool) override {
      std::cout << "onData: " << data.toString() << std::endl;
    }
    void onTrailers(Http::ResponseTrailerMapPtr&&) override {}
    void onReset() override {
      std::cout << "hi from onreset \n\n\n\n\n" << std::endl;
    }
    void onComplete() override {
        std::cout << "hi from onComplete() \n\n\n" << std::endl;
        // remove ourself from the clusterManager
        cluster_manager_.eraseCallbacksAndHeaders(id_);
    }
    Http::RequestHeaderMapImpl& requestHeaderMap() override {
        std::cout << "hi: " << headers_ <<  std::endl;
        return *(headers_.get());
    }

    void setStream(Http::AsyncClient::Stream* stream) override { request_stream_ = stream;}
    Http::AsyncClient::Stream* getStream() override { return request_stream_; }

private:
    std::string id_;
    std::unique_ptr<Http::RequestHeaderMapImpl> headers_;
    Upstream::ClusterManager& cluster_manager_;
    Http::AsyncClient::Stream* request_stream_;
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
    std::unique_ptr<RequestHeaderMapImpl> request_headers_;
    Buffer::OwnedImpl request_data_;
    BavsCallbacks* callbacks_;
    std::string callback_key_;

    void sendHeaders(bool end_stream);

public:
    BavsFilter20(Upstream::ClusterManager& cluster_manager)
    : cluster_manager_(cluster_manager), is_workflow_(false), successful_response_(true),
      service_cluster_("inbound|5000||"),
      request_headers_(Http::RequestHeaderMapImpl::create()) {};
      //inbound|5000||
      //"outbound|5000||fake-api-63d700b2.default.svc.cluster.local"

    FilterDataStatus decodeData(Buffer::Instance&, bool);
    FilterDataStatus encodeData(Buffer::Instance&, bool);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool);
};

} // namespace Http
} // namespace Envoy
