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

static const char* CONNECTION_ERROR("FAILED_CONNECTION");
static const char* CONTEXT_INPUT_ERROR("FAILED_CONTEXT_INPUT_PARSING");
static const char* CONTEXT_OUTPUT_ERROR("FAILED_CONTEXT_OUTPUT_PARSING");
static const char* TASK_ERROR("FAILED_TASK");

// For shadowing traffic.
// TODO: Make these configurable via bavs.proto
static const char* ORIGINAL_REQ_FAILED_HDR("x-rexflow-original-request-failed");
static const char* REQ_TYPE_HEADER("x-rexflow-request-type");
static const char* ORIGINAL_HOST_HDR("x-rexflow-original-host");
static const char* ORIGINAL_PATH_HDR("x-rexflow-original-path");
static const char* ORIGINAL_METHOD_HDR("x-rexflow-original-method");

static const char* REQ_TYPE_INBOUND("INBOUND");
static const char* REQ_TYPE_OUTBOUND("OUTBOUND");
static const char* REQ_TYPE_ERROR("ERROR");
static const char* REQ_TYPE_TASK_ERROR("TASK_ERROR");

// Fine to hard-code these here because they come from Envoy, not rexflow.
static const char* SPAN_ID_HEADER("x-b3-spanid");
static const char* TRACE_ID_HEADER("x-b3-traceid");
static const char* REQUEST_ID_HEADER("x-request-id");

namespace BavsUtil {
std::string jstringify(const std::string&);
std::string dumpHeaders(const Http::RequestOrResponseHeaderMap& hdrs);
std::string create_json_string(const std::map<std::string, std::string>& json_elements);
std::string get_array_as_string(const Json::Object* json);
std::string get_array_as_string(const std::vector<Json::ObjectSharedPtr>& arr);
std::string get_object_as_string(const Json::Object* json);
std::string build_json_from_params(const Json::ObjectSharedPtr, const std::vector<bavs::BAVSParameter>&);
std::string merge_jsons(const Json::ObjectSharedPtr original, const Json::ObjectSharedPtr updater);
std::string get_array_or_obj_str_from_json(const Json::ObjectSharedPtr json_obj,
        const std::string& key);

std::string createErrorMessage(std::string error_code, std::string error_msg,
                               const Buffer::OwnedImpl& input_data, const Http::RequestHeaderMap& input_headers,
                               Http::ResponseMessage& response);
std::string createErrorMessage(std::string error_code, std::string error_msg,
                               const Buffer::OwnedImpl& input_data, const Http::RequestHeaderMap& input_headers);
} // namespace BavsUtil


class UpstreamConfig {
public:
    UpstreamConfig() {}
    UpstreamConfig(const bavs::Upstream& proto_config) :
        full_hostname_(proto_config.full_hostname()),
        port_(proto_config.port()),
        path_(proto_config.path()),
        method_(proto_config.method()),
        total_attempts_(proto_config.total_attempts()),
        wf_tid_(proto_config.wf_tid())
    {
        cluster_ = "outbound|" + std::to_string(port_) + "||" + full_hostname_;
    }

    inline const std::string& fullHostName() const { return full_hostname_; }
    inline int port() const { return port_; }
    inline const std::string& path() const { return path_; }
    inline const std::string& method() const { return method_; }
    inline int totalAttempts() const { return total_attempts_; }
    inline const std::string& wfTID() const { return wf_tid_; }
    inline const std::string& cluster() const { return cluster_; }


private:
    std::string full_hostname_;
    int port_;
    std::string path_;
    std::string method_;
    int total_attempts_;
    std::string wf_tid_;
    std::string cluster_;
};

using UpstreamConfigSharedPtr = std::shared_ptr<UpstreamConfig>;

class BavsFilterConfig {
public:
    virtual ~BavsFilterConfig() {}
    BavsFilterConfig(const bavs::BAVSFilter& proto_config, Upstream::ClusterManager&);

    const std::vector<const UpstreamConfigSharedPtr>& forwardUpstreams() { return forward_upstreams_; }
    const UpstreamConfigSharedPtr flowdUpstream() { return flowd_upstream_; }
    const UpstreamConfigSharedPtr shadowUpstream() { return shadow_upstream_; }
    const std::vector<const UpstreamConfigSharedPtr>& errorGatewayUpstreams() {
        return error_gateway_upstreams_;
    }
    bool isClosureTransport() { return is_closure_transport_; }
    std::vector<bavs::BAVSParameter>& inputParams() { return input_params_; }
    std::vector<bavs::BAVSParameter>& outputParams() { return output_params_; }
    const std::string& wfDID() { return wf_did_; }
    const std::vector<std::string>& headersToForward() { return headers_to_forward_; }
    Upstream::ClusterManager& clusterManager() { return cluster_manager_; }
    UpstreamConfigSharedPtr inboundUpstream() { return inbound_upstream_; }
    const std::string wfDIDHeader() { return wf_did_header_; }
    const std::string wfIIDHeader() { return wf_iid_header_; }
    const std::string wfTIDHeader() { return wf_tid_header_; }
    const std::string requestFailedHeader() { return ORIGINAL_REQ_FAILED_HDR; }
    const std::string requestTypeHeader() { return REQ_TYPE_HEADER; }
    const std::string originalHostHeader() { return ORIGINAL_HOST_HDR; }
    const std::string originalPathHeader() { return ORIGINAL_PATH_HDR; }
    const std::string originalMethodHeader() { return ORIGINAL_METHOD_HDR; }

private:
    std::vector<const UpstreamConfigSharedPtr> forward_upstreams_;
    std::vector<const UpstreamConfigSharedPtr> error_gateway_upstreams_;
    UpstreamConfigSharedPtr inbound_upstream_;
    UpstreamConfigSharedPtr flowd_upstream_;
    UpstreamConfigSharedPtr shadow_upstream_;
    std::string wf_did_;
    std::vector<std::string> headers_to_forward_;
    std::vector<bavs::BAVSParameter> input_params_;
    std::vector<bavs::BAVSParameter> output_params_;
    bool is_closure_transport_;
    Upstream::ClusterManager& cluster_manager_;
    std::string wf_did_header_;
    std::string wf_tid_header_;
    std::string wf_iid_header_;
};

using BavsFilterConfigSharedPtr = std::shared_ptr<BavsFilterConfig>;

class BavsRequestBase : public Http::AsyncClient::Callbacks {
public:
    BavsRequestBase(
        BavsFilterConfigSharedPtr config,
        std::unique_ptr<Buffer::OwnedImpl> data,
        std::unique_ptr<Http::RequestHeaderMap> headers,
        std::map<std::string, std::string> headers_to_forward,
        int retries_left,
        UpstreamConfigSharedPtr target,
        std::string request_type);

    void send();
    void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&, const Http::ResponseHeaderMap*) override {}

    // Utilities
    Http::RequestHeaderMapPtr copyHeaders(Http::RequestOrResponseHeaderMap& headers);

protected:
    BavsFilterConfigSharedPtr config_;
    const std::unique_ptr<Buffer::OwnedImpl> data_;
    const std::unique_ptr<Http::RequestHeaderMap> headers_;
    const int retries_left_;
    const std::string request_type_;
    std::map<std::string, std::string> saved_headers_;
    UpstreamConfigSharedPtr target_;

    const Http::RequestHeaderMap* getHeaders() const { return headers_.get(); }
    const Buffer::OwnedImpl* getData() const { return data_.get(); }
    Http::RequestHeaderMapPtr copyHeaders();

    /**
     * Returns the actual message that should be sent. Implemented by child classes.
     * Should NOT make any requests. The BavsRequestBase handles sending it, traffic shadowing,
     * and any error handling.
     * 
     * If the child class is unable to form a message to send (eg. if BAVS 2.0 context parameter
     * matching fails), then the function should return nullptr. The getMessage() function is
     * responsible for making any outbound ErrorRequests in such a case.
     */
    virtual std::unique_ptr<Http::RequestMessage> getMessage() PURE;

    /**
     * Called when request fails (normally due to connection issues).
     */
    virtual void processFailure(const AsyncClient::Request&, AsyncClient::FailureReason) PURE;

    /**
     * Called when request successfully comes back (note that it could be called with a non-200 response).
     */
    virtual void processSuccess(const AsyncClient::Request&, ResponseMessage*) PURE;

    /**
     * Called when the BavsRequestBase tries to send the request to the initial target
     * cluster, but fails to connect to it. May be a no-op but should only be a no-op if the
     * implementor is willing to fully give up and orphan the workflow instance (i.e. after
     * we've already tried to hit the `http://flowd.rexflow:9001/instancefail` endpoint).
     */
    virtual void handleConnectionError() PURE;

    /**
     * Depending on the BavsFilterConfig contents, optionally shadows this request
     * to a predetermined endpoint.
     */
    void sendShadowRequest(bool);

private:
    void preprocessHeaders(RequestHeaderMap&) const;

    std::string cm_callback_id_;
};


/**
 * Usage: To send an inbound Workflow request and do appropriate error handling + forward
 * response to proper next service in workflow, simply allocate a new BavsInboundRequest object
 * and call `->send()`. You should allow it to fall out of scope.
 * The BavsInboundRequest ctor + callbacks deal with cleanup.
 */
class BavsInboundRequest : public BavsRequestBase {
public:
    BavsInboundRequest(BavsFilterConfigSharedPtr config,
                    std::unique_ptr<Buffer::OwnedImpl> data,
                    std::unique_ptr<Http::RequestHeaderMap> headers,
                    std::map<std::string, std::string> headers_to_forward,
                    int retries_left,
                    UpstreamConfigSharedPtr target,
                    std::string request_type)
                    :   BavsRequestBase(
                            config,
                            std::move(data),
                            std::move(headers),
                            headers_to_forward,
                            retries_left,
                            target,
                            request_type
                        ) {

        auto value = getHeaders()->getContentTypeValue();
        if (value == "application/json") {
            try {
                Json::ObjectSharedPtr temp = Json::Factory::loadFromString(getData()->toString());
                inbound_data_is_json_ = true;
            } catch (const EnvoyException&) {
                inbound_data_is_json_ = false;
            }
        } else {
            inbound_data_is_json_ = false;
        }
    }

protected:
    std::unique_ptr<Http::RequestMessage> getMessage() override;
    void processSuccess(const AsyncClient::Request&, ResponseMessage*) override;
    void processFailure(const AsyncClient::Request&, AsyncClient::FailureReason) override;
    void handleConnectionError() override;
    std::string json_substitution(Json::ObjectSharedPtr json, const std::string& src);

private:
    std::string mergeResponseAndContext(Http::ResponseMessage* response);
    void doRetry();
    void createAndSendError(std::string error_code, std::string error_msg);
    void createAndSendError(std::string error_code, std::string error_msg, ResponseMessage& response);
    void raiseTaskError(Http::ResponseMessage&);
    std::string sub_json_token(Json::ObjectSharedPtr json, std::string::const_iterator& itr, std::string::const_iterator end, int level = 0);

    bool inbound_data_is_json_;
};

class BavsOutboundRequest : public BavsRequestBase {
public:
    BavsOutboundRequest(
        BavsFilterConfigSharedPtr config,
        std::unique_ptr<Buffer::OwnedImpl> data,
        std::unique_ptr<Http::RequestHeaderMap> headers,
        std::map<std::string, std::string> headers_to_forward,
        int retries_left,
        UpstreamConfigSharedPtr target,
        std::string request_type)
        : BavsRequestBase(
            config,
            std::move(data),
            std::move(headers),
            headers_to_forward,
            retries_left,
            target, request_type) {}

protected:
    std::unique_ptr<Http::RequestMessage> getMessage() override;
    void processSuccess(const AsyncClient::Request&, ResponseMessage*) override;
    void processFailure(const AsyncClient::Request&, AsyncClient::FailureReason) override;
    void handleConnectionError() override;
};

class BavsErrorRequest : public BavsRequestBase {
public:
    BavsErrorRequest(BavsFilterConfigSharedPtr config, std::unique_ptr<Buffer::OwnedImpl> data,
                    std::unique_ptr<Http::RequestHeaderMap> headers,
                    std::map<std::string, std::string> headers_to_forward, int retries_left,
                    UpstreamConfigSharedPtr target, std::string request_type):
                    BavsRequestBase(config, std::move(data),
                    std::move(headers), headers_to_forward, retries_left,
                    target, request_type) {}

protected:
    std::unique_ptr<Http::RequestMessage> getMessage() override;
    void processSuccess(const AsyncClient::Request&, ResponseMessage*) override;
    void processFailure(const AsyncClient::Request&, AsyncClient::FailureReason) override;
    void handleConnectionError() override;
};

class BavsFilter : public PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
private:
    const BavsFilterConfigSharedPtr config_;
    bool is_workflow_;
    std::unique_ptr<RequestHeaderMapImpl> inbound_headers_;
    std::unique_ptr<Buffer::OwnedImpl> original_inbound_data_;

    std::map<std::string, std::string> saved_headers_;

    void sendMessage();
    void raiseContextInputError(std::string msg);

public:
    BavsFilter(BavsFilterConfigSharedPtr config)
    : config_(config), is_workflow_(false),
      inbound_headers_(Http::RequestHeaderMapImpl::create()),
      original_inbound_data_(std::make_unique<Buffer::OwnedImpl>()) {}

    FilterDataStatus decodeData(Buffer::Instance&, bool);
    FilterDataStatus encodeData(Buffer::Instance&, bool);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool);
};


/**
 * Null callbacks do nothing. Used for fire-and-forget requests, such as
 * traffic shadowing.
 */
class NullCallbacks : public AsyncClient::Callbacks {
public:
    void onSuccess(const AsyncClient::Request&, ResponseMessagePtr&&) override {}
    void onFailure(const AsyncClient::Request&, AsyncClient::FailureReason) override {}
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&, const ResponseHeaderMap*) override {}
};

} // namespace Http
} // namespace Envoy
