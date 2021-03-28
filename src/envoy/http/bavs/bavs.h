#pragma once
#include <iostream>
#include <stdio.h>
#include <map>

#include "envoy/http/filter.h"
#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"
#include "envoy/http/async_client.h"
#include "common/http/message_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"
#include "common/common/random_generator.h"

#include "src/envoy/http/bavs/bavs.pb.h"
#include "common/upstream/cluster_manager_impl.h"
#include "envoy/upstream/cluster_manager.h"


namespace Envoy {
namespace Http {

std::string create_json_string(const std::map<std::string, std::string>& json_elements);
std::string get_array_as_string(const Json::Object* json);
std::string get_object_as_string(const Json::Object* json);
std::string build_json_from_params(const Json::ObjectSharedPtr, const std::vector<bavs::BAVSParameter>);
std::string merge_jsons(const Json::ObjectSharedPtr original, const Json::ObjectSharedPtr updater);

class UpstreamConfig {
public:
    UpstreamConfig() {}
    UpstreamConfig(const bavs::Upstream& proto_config) :
        full_hostname_(proto_config.full_hostname()), port_(proto_config.port()),
        path_(proto_config.path()), method_(proto_config.method()), total_attempts_(proto_config.total_attempts()),
        task_id_(proto_config.task_id()) {}

    inline const std::string& full_hostname() const { return full_hostname_; }
    inline int port() const { return port_; }
    inline const std::string& path() const { return path_; }
    inline const std::string& method() const { return method_; }
    inline int totalAttempts() const { return total_attempts_; }
    inline const std::string& taskId() const { return task_id_; }

private:
    std::string full_hostname_;
    int port_;
    std::string path_;
    std::string method_;
    int total_attempts_;
    std::string task_id_;
};

using UpstreamConfigSharedPtr = std::shared_ptr<UpstreamConfig>;

class BavsFilterConfig {
public:
    virtual ~BavsFilterConfig() {}
    BavsFilterConfig() {}
    BavsFilterConfig(const bavs::BAVSFilter& proto_config);

    virtual const std::string& wfIdValue() { return wf_id_; }
    virtual const std::string& flowdCluster() { return flowd_cluster_; }
    virtual const std::string& flowdPath() { return flowd_path_; }
    virtual const std::string& taskId() { return task_id_; }
    virtual const std::string& trafficShadowCluster() { return traffic_shadow_cluster_; }
    virtual const std::string& trafficShadowPath() { return traffic_shadow_path_; }
    virtual const std::vector<std::string>& headersToForward() { return headers_to_forward_; }
    virtual const std::vector<const UpstreamConfigSharedPtr>& forwards() { return forwards_; }
    std::vector<bavs::BAVSParameter>& inputParams() { return input_params_; }
    std::vector<bavs::BAVSParameter>& outputParams() { return output_params_; }
    bool isClosureTransport() { return is_closure_transport_; }
    int upstreamPort() { return upstream_port_; }

private:
    std::vector<const UpstreamConfigSharedPtr> forwards_;
    std::string wf_id_;
    std::string flowd_cluster_;
    std::string flowd_path_;
    std::string task_id_;
    std::string traffic_shadow_cluster_;
    std::string traffic_shadow_path_;
    std::vector<std::string> headers_to_forward_;
    std::vector<bavs::BAVSParameter> input_params_;
    std::vector<bavs::BAVSParameter> output_params_;
    bool is_closure_transport_;
    int upstream_port_;
};

using BavsFilterConfigSharedPtr = std::shared_ptr<BavsFilterConfig>;

class BavsOutboundCallbacks : public Envoy::Upstream::AsyncStreamCallbacksAndHeaders {
public:
    ~BavsOutboundCallbacks() = default;
    BavsOutboundCallbacks() {};
    BavsOutboundCallbacks(std::string id,
                        std::unique_ptr<Http::RequestHeaderMapImpl> headers,
                        Upstream::ClusterManager& cm, int num_retries,
                        std::string fail_cluster, std::string cluster,
                        std::string fail_cluster_path, UpstreamConfigSharedPtr config)
        : id_(id), cluster_manager_(&cm), attempts_left_(num_retries),
          fail_cluster_(fail_cluster), cluster_(cluster), headers_(std::move(headers)), buffer_(new Buffer::OwnedImpl),
          headers_only_(false), fail_cluster_path_(fail_cluster_path), config_(config) {
        cluster_manager_->storeCallbacksAndHeaders(id, this);
    }

    void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool) override;
    void onComplete() override;
    void onData(Buffer::Instance&, bool) override {}

    void onTrailers(Http::ResponseTrailerMapPtr&&) override {}
    void onReset() override {}
    Http::RequestHeaderMapImpl& requestHeaderMap() override;
    void setStream(Http::AsyncClient::Stream* stream) override;
    Http::AsyncClient::Stream* getStream() override;
    void addData(Buffer::Instance& data);

private:

    void doRetry(bool end_stream);
    std::string id_;
    Upstream::ClusterManager* cluster_manager_;
    int attempts_left_;
    std::string fail_cluster_;
    std::string cluster_;
    std::unique_ptr<Http::RequestHeaderMapImpl> headers_;
    std::unique_ptr<Buffer::OwnedImpl> buffer_;
    bool headers_only_ = false;
    std::string fail_cluster_path_;
    bool unretriable_failure_ = false;
    bool retriable_failure_ = false;
    Http::AsyncClient::Stream* request_stream_;
    UpstreamConfigSharedPtr config_;
};

class BavsInboundCallbacks : public Envoy::Upstream::AsyncStreamCallbacksAndHeaders {
public:
    BavsInboundCallbacks(std::string id, std::unique_ptr<Http::RequestHeaderMapImpl> headers,
            Upstream::ClusterManager& cm, BavsFilterConfigSharedPtr config,
            std::map<std::string, std::string> saved_headers, std::string instance_id,
            std::string spanid, std::string context_input, bool data_is_json)
        : id_(id), headers_(std::move(headers)), cluster_manager_(cm), config_(config),
          saved_headers_(saved_headers), instance_id_(instance_id),
          spanid_(spanid), context_input_(context_input), inbound_data_is_json_(data_is_json),
          did_update_request_data_(false) {
        cluster_manager_.storeCallbacksAndHeaders(id, this);
    }

    void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override; 
    void sendAllHeaders(bool end_stream);
    void sendShadowHeaders(Http::RequestHeaderMapImpl& original_headers);
    void onData(Buffer::Instance& data, bool end_stream) override;

    void onTrailers(Http::ResponseTrailerMapPtr&&) override {}
    void onReset() override {}
    void onComplete() override {
        // remove ourself from the clusterManager
        cluster_manager_.eraseCallbacksAndHeaders(id_);
    }
    Http::RequestHeaderMapImpl& requestHeaderMap() override { return *(headers_.get()); }
    void setStream(Http::AsyncClient::Stream* stream) override { request_stream_ = stream;}
    Http::AsyncClient::Stream* getStream() override { return request_stream_; }

private:

    std::string id_;
    std::unique_ptr<Http::RequestHeaderMapImpl> headers_;
    Upstream::ClusterManager& cluster_manager_;
    BavsFilterConfigSharedPtr config_;
    Http::AsyncClient::Stream* request_stream_;
    std::map<std::string, std::string> saved_headers_;
    std::string instance_id_;
    std::string spanid_;
    std::vector<std::string> req_cb_keys;
    Buffer::OwnedImpl request_data_;
    std::string context_input_;
    bool inbound_data_is_json_;
    bool did_update_request_data_;

};

class BavsErrorCallbacks : public Envoy::Upstream::AsyncStreamCallbacksAndHeaders {

};

class BavsFilter : public PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
private:
    const BavsFilterConfigSharedPtr config_;
    Upstream::ClusterManager& cluster_manager_;
    bool is_workflow_;
    bool successful_response_;
    std::string instance_id_;
    std::unique_ptr<RequestMessageImpl> message_;
    std::map<std::string, std::string> saved_headers_;
    std::string service_cluster_;
    std::unique_ptr<RequestHeaderMapImpl> request_headers_;
    Buffer::OwnedImpl request_data_;
    BavsInboundCallbacks* callbacks_;
    std::string callback_key_;
    std::string spanid_;

    void sendHeaders(bool end_stream);

public:
    BavsFilter(BavsFilterConfigSharedPtr config, Upstream::ClusterManager& cluster_manager)
    : config_(config), cluster_manager_(cluster_manager), is_workflow_(false), successful_response_(true),
      request_headers_(Http::RequestHeaderMapImpl::create()) {
          service_cluster_ = "inbound|" + std::to_string(config_->upstreamPort()) + "||";
      };

    FilterDataStatus decodeData(Buffer::Instance&, bool);
    FilterDataStatus encodeData(Buffer::Instance&, bool);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool);
};

} // namespace Http
} // namespace Envoy
