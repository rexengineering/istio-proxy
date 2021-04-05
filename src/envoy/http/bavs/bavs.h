#pragma once
#include <iostream>
#include <stdio.h>
#include <map>
#include <queue>

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

#define ERROR_TYPE_HEADER "x-rexflow-error-type"
#define CONNECTION_ERROR "FAILED_CONNECTION"
#define CONTEXT_INPUT_PARSING_ERROR "FAILED_CONTEXT_INPUT_PARSING"
#define CONTEXT_OUTPUT_PARSING_ERROR "FAILED_CONTEXT_OUTPUT_PARSING"
#define TASK_ERROR "FAILED_TASK"

std::string jstringify(const std::string&);
std::string dumpHeaders(Http::RequestOrResponseHeaderMap& hdrs);
std::string create_json_string(const std::map<std::string, std::string>& json_elements);
std::string get_array_as_string(const Json::Object* json);
std::string get_array_as_string(const std::vector<Json::ObjectSharedPtr>& arr);
std::string get_object_as_string(const Json::Object* json);
std::string build_json_from_params(const Json::ObjectSharedPtr, const std::vector<bavs::BAVSParameter>&);
std::string merge_jsons(const Json::ObjectSharedPtr original, const Json::ObjectSharedPtr updater);
std::string get_array_or_obj_str_from_json(const Json::ObjectSharedPtr json_obj,
        const std::string& key);

std::string createErrorMessage(std::string error_code, std::string error_msg,
                               Buffer::OwnedImpl& input_data, Http::RequestHeaderMap& input_headers,
                               Http::ResponseMessage& response);
std::string createErrorMessage(std::string error_code, std::string error_msg,
                               Buffer::OwnedImpl& input_data, Http::RequestHeaderMap& input_headers);

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
    const UpstreamConfigSharedPtr errorGateway() { return error_gateway_; }
    int inboundRetries() const { return inbound_retries_; }
    virtual const std::vector<const UpstreamConfigSharedPtr>& errorUpstreams() { return error_upstreams_; }

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
    UpstreamConfigSharedPtr error_gateway_;
    int inbound_retries_;
    std::vector<const UpstreamConfigSharedPtr> error_upstreams_;
};

using BavsFilterConfigSharedPtr = std::shared_ptr<BavsFilterConfig>;

/**
 * Usage: To send an inbound Workflow request and do appropriate error handling + forward
 * response to proper next service in workflow, simply allocate a new BavsInboundRequest object
 * and call `->send()`. You should allow it to fall out of scope.
 * The BavsInboundRequest ctor + callbacks deal with cleanup.
 */
class BavsInboundRequest : public Http::AsyncClient::Callbacks {
public:
    BavsInboundRequest(BavsFilterConfigSharedPtr config, Upstream::ClusterManager& cm,
                       std::unique_ptr<Http::RequestHeaderMapImpl> inbound_headers,
                       std::unique_ptr<Buffer::OwnedImpl> original_inbound_data,
                       std::unique_ptr<Buffer::OwnedImpl> inbound_data_to_send,
                       int retries_left, std::string span_id, std::string instance_id,
                       std::map<std::string, std::string> saved_headers,
                       bool inbound_data_is_json, std::string service_cluster);

    void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&, const Http::ResponseHeaderMap*) override {}
    void send();

private:
    Http::RequestHeaderMapPtr createOutboundHeaders(UpstreamConfigSharedPtr upstream_ptr);
    std::string mergeResponseAndContext(Http::ResponseMessagePtr& response);

    void raiseConnectionError();
    void raiseContextOutputParsingError(Http::ResponseMessage& msg, std::string error_msg);
    void raiseContextOutputParsingError(Http::ResponseMessage&); 
    void raiseTaskError(Http::ResponseMessage&);
    void doRetry();

    BavsFilterConfigSharedPtr config_;
    Upstream::ClusterManager& cm_;
    std::unique_ptr<Http::RequestHeaderMapImpl> inbound_headers_;
    std::unique_ptr<Buffer::OwnedImpl> original_inbound_data_;
    std::unique_ptr<Buffer::OwnedImpl> inbound_data_to_send_;
    int retries_left_;
    std::string span_id_;
    std::string instance_id_;
    std::map<std::string, std::string> saved_headers_;
    bool inbound_data_is_json_;
    std::string cm_callback_id_;
    std::string service_cluster_;
};

class BavsOutboundRequest : public Http::AsyncClient::Callbacks {
public:
    BavsOutboundRequest(Upstream::ClusterManager& cm, std::string target_cluster, std::string error_cluster,
                        int retries_left, std::unique_ptr<Http::RequestHeaderMapImpl> headers_to_send,
                        std::unique_ptr<Buffer::OwnedImpl> data_to_send, std::string task_id,
                        std::string error_path);
    ~BavsOutboundRequest() = default;
    void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&, const Http::ResponseHeaderMap*) override {}
    void send();

private:
    Upstream::ClusterManager& cm_;
    std::string target_cluster_;
    std::string error_cluster_;
    int retries_left_;
    std::unique_ptr<Http::RequestHeaderMapImpl> headers_to_send_;
    std::unique_ptr<Buffer::OwnedImpl> data_to_send_;
    std::string cm_callback_id_;
    std::string task_id_;
    std::string error_path_;

    void raiseConnectionError();
};

class BavsErrorRequest : public Http::AsyncClient::Callbacks {
/**
 * Defines a last-ditch effort to notify Flowd that something's gone horribly wrong.
 * If this request fails, then the WF Instance becomes orphaned.
 */
public:
    BavsErrorRequest(Upstream::ClusterManager& cm, std::string cluster,
                     std::unique_ptr<Buffer::OwnedImpl> data_to_send,
                     std::unique_ptr<Http::RequestHeaderMapImpl> headers_to_send,
                     std::string error_path);
    ~BavsErrorRequest() = default;
    void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&, const Http::ResponseHeaderMap*) override {}
    void send();

private:
    Upstream::ClusterManager& cm_;
    std::string cluster_;
    std::unique_ptr<Buffer::OwnedImpl> data_to_send_;
    std::unique_ptr<Http::RequestHeaderMapImpl> headers_to_send_;
    std::string cm_callback_id_;
    std::string error_path_;
};

class BavsTaskErrorRequest : public Http::AsyncClient::Callbacks {
public:
    BavsTaskErrorRequest(Upstream::ClusterManager& cm, std::string cluster,
                     std::unique_ptr<Buffer::OwnedImpl> data_to_send,
                     std::unique_ptr<Http::RequestHeaderMapImpl> headers_to_send,
                     BavsFilterConfigSharedPtr config,
                     UpstreamConfigSharedPtr upstream);
    ~BavsTaskErrorRequest() = default;
    void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&, const Http::ResponseHeaderMap*) override {}
    void send();

private:
    Upstream::ClusterManager& cm_;
    std::string cluster_;
    std::unique_ptr<Buffer::OwnedImpl> data_to_send_;
    std::unique_ptr<Http::RequestHeaderMapImpl> headers_to_send_;
    std::string cm_callback_id_;
    BavsFilterConfigSharedPtr config_;
    UpstreamConfigSharedPtr upstream_;
};


class BavsFilter : public PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
private:
    const BavsFilterConfigSharedPtr config_;
    Upstream::ClusterManager& cluster_manager_;
    bool is_workflow_;
    std::unique_ptr<RequestHeaderMapImpl> inbound_headers_;
    std::unique_ptr<Buffer::OwnedImpl> original_inbound_data_;
    std::unique_ptr<Buffer::OwnedImpl> inbound_data_to_send_;
    std::string service_cluster_;

    bool inbound_data_is_json_;

    std::string instance_id_;
    std::map<std::string, std::string> saved_headers_;
    std::string spanid_;

    void sendMessage();
    void raiseContextInputError(std::string msg);

public:
    BavsFilter(BavsFilterConfigSharedPtr config, Upstream::ClusterManager& cluster_manager)
    : config_(config), cluster_manager_(cluster_manager), is_workflow_(false),
      inbound_headers_(Http::RequestHeaderMapImpl::create()),
      original_inbound_data_(std::make_unique<Buffer::OwnedImpl>()),
      inbound_data_to_send_(std::make_unique<Buffer::OwnedImpl>()),
      service_cluster_("inbound|" + std::to_string(config->upstreamPort()) + "||"),
      inbound_data_is_json_(false) {}

    FilterDataStatus decodeData(Buffer::Instance&, bool);
    FilterDataStatus encodeData(Buffer::Instance&, bool);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool);
};

} // namespace Http
} // namespace Envoy
