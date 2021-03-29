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

#define REXFLOW_CONNECTION_ERROR_HDR "CONNECTION_ERROR"
#define REXFLOW_APPLICATION_ERROR_HDR "APPLICATION_ERROR"

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
    const UpstreamConfigSharedPtr errorGateway() { return error_gateway_; }

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
};

using BavsFilterConfigSharedPtr = std::shared_ptr<BavsFilterConfig>;

/**
 * Usage: To send an inbound Workflow request and do appropriate error handling + forward
 * response to proper next service in workflow, simply declare a BavsInboundRequest object
 * on the stack and call `.send()`. You should allow it to fall out of scope.
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
                       bool inbound_data_is_json) :
                       config_(config), cm_(cm), inbound_headers_(std::move(inbound_headers)),
                       original_inbound_data_(std::move(original_inbound_data)),
                       inbound_data_to_send_(std::move(inbound_data_to_send)),
                       retries_left_(retries_left), span_id_(span_id), instance_id_(instance_id),
                       saved_headers_(saved_headers),
                       inbound_data_is_json_(inbound_data_is_json) {
        Random::RandomGeneratorImpl rng;
        cm_callback_id_ = rng.uuid();
        cm_.storeRequestCallbacks(cm_callback_id_, this);
    }

    void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override {
        Buffer::OwnedImpl data_to_send;
        std::string content_type;

        if (config_->isClosureTransport()) {
            // If status is bad, we notify flowd or error gateway
            std::string status_str(response->headers.getStatusValue());
            int status = atoi(status_str.c_str());

            if (status < 200 || status >= 300) {
                if (retries_left_ > 0) {
                    BavsInboundRequest retry_request(config_, cm_ , inbound_headers_,
                                                    original_inbound_data_, inbound_data_to_send_,
                                                    retries_left_ - 1, span_id_, instance_id_,
                                                    saved_headers_, inbound_data_is_json_);
                    retry_request.send();
                    cm_.eraseRequestCallbacks(cm_callback_id_);
                    return;
                } else {
                    callErrorGatewayOrFlowd(*response);
                    cm_.eraseRequestCallbacks(cm_callback_id_);
                    return;
                }
            }
            // If we get here, then we know that the call to the inbound service (i.e. the
            // one that this Envoy Proxy is proxying) succeeded.
            // Since this is closure transport, we now need to merge the response with the
            // previous closure context.
            try {
                data_to_send.add(mergeResponseAndContext(*response));
                content_type = "application/json";
            } catch (const EnvoyException& exn) {
                std::cout << "Exception while merging context + response: "<< exn.what() << std::endl;
                callErrorGatewayOrFlowd(*response);
            }
        } else {
            data_to_send.add(response->data());
            content_type = response->headers.getContentTypeValue();
        }

        size_t content_length = data_to_send.length();
        for (UpstreamConfigSharedPtr& upstream : config_->forwards()) {
            Http::RequestHeaderMapImplPtr request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
                {
                    {Http::Headers::get().Method, upstream.method()},
                    {Http::Headers::get().Host, upstream.full_hostname() + ":" + std::to_string(upstream.port())},
                    {Http::Headers::get().Path, upstream.path()},
                    {Http::LowerCaseString("x-rexflow-wf-id"), config_->wfIdValue()},
                    {Http::LowerCaseString("x-rexflow-task-id"), upstream.taskId()},
                    {Http::LowerCaseString("x-flow-id"), instance_id_}
                }
            );
            for (const auto& saved_header : saved_headers_) {
                request_headers->setCopy(Http::LowerCaseString(saved_header.first), saved_header.second);
            }
            // Inject tracing context
            request_headers->setCopy(Http::LowerCaseString("x-b3-spanid"), spanid_);
            request_headers->setContentLength(content_length);
            request_headers->setContentType(content_type);
            BavsOutboundRequest outbound_request(/*todo*/);
            outbound_request.send();
        }
        cm_.eraseRequestCallbacks(cm_callback_id_);
    }

    void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason&&) override {
        if (retries_left_ > 0) {
            BavsInboundRequest retry_request(config_, cm_ , inbound_headers_,
                                             original_inbound_data_, inbound_data_to_send_,
                                             retries_left_ - 1, span_id_, instance_id_,
                                             saved_headers_, inbound_data_is_json_);
            retry_request.send();
        } else {
            notifyFlowdOfConnectionError();
        }
        cm_.eraseRequestCallbacks(cm_callback_id_);
    }

    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&, const Http::ResponseHeaderMap*) override {}

    void send() {
        // First, form the message
        RequestMessagePtr message();

        RequestHeaderMapImpl* temp = message->headers();
        inbound_headers_->iterate([temp] (const HeaderEntry& header) -> HeaderMap::Iterate {
            std::string hdr_key(header.key().getStringView());
            temp->setCopy(Http::LowerCaseString(hdr_key), header.value().getStringView());
        });

        message->body().add(inbound_data_to_send_);

        // Second, send the message
        Http::AsyncClient* client = NULL;
        try {
            client = &(cluster_manager_.httpAsyncClientForCluster(service_cluster_));
        } catch(const EnvoyException&) {
            // The cluster wasn't found, so we need to begin error processing.
            notifyFlowdOfConnectionError();
            return;
        }
        client->send(std::move(message), *this, Http::RequestOptions());
    }

private:
    Http::RequestHeaderMapPtr createOutboundHeaders(UpstreamConfigSharedPtr upstream_ptr) {
        const UpstreamConfig& upstream(*upstream_ptr);

        // Create headers to send over to the next place.
        std::unique_ptr<RequestHeaderMapImpl> hdrs = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
            {
                {Http::Headers::get().Method, upstream.method()},
                {Http::Headers::get().Host, upstream.full_hostname() + ":" + std::to_string(upstream.port())},
                {Http::Headers::get().Path, upstream.path()},
                {Http::LowerCaseString("x-rexflow-wf-id"), config_->wfIdValue()},
                {Http::LowerCaseString("x-rexflow-task-id"), upstream.taskId()},
                {Http::LowerCaseString("x-flow-id"), instance_id_}
            }
        );

        for (const auto& saved_header : saved_headers_) {
            hdrs->setCopy(Http::LowerCaseString(saved_header.first), saved_header.second);
        }
        // Inject tracing context
        hdrs->setCopy(Http::LowerCaseString("x-b3-spanid"), spanid_);
        return std::move(hdrs);
    }

    std::string mergeResponseAndContext(Http::ResponseMessagePtr& response) {
        Json::ObjectSharedPtr updater = Json::Factory::loadFromString(response->data.toString());

        /**
         * TODO: In the future, expand this section to support non-json messages, for example,
         * passing a small image or a byte stream as a context variable.
         */
        if (!updater->isObject()) {
            if (config_->outputParams().size() > 0) {
                // TODO: in this line, we would parse the non-json content-type stuff.
                throw new EnvoyException(
                    "Tried to get variables, but response data was not a JSON object."
                );
            } else if (!inbound_data_is_json_) {
                throw new EnvoyException("Neither input nor output is json.");
            }
            return original_inbound_data_->toString();
        }
        if (!inbound_data_is_json_)
            return response->data().toString();

        Json::ObjectSharedPtr updatee = Json::Factory::loadFromString(
            original_inbound_data_.toString());

        if (!updatee->isObject())
            throw new EnvoyException("Received invalid input json from previous service.");

        // At this point, we know that we have two valid json objects: the updater (the response)
        // and the updatee (the closure context).
        return merge_jsons(updatee, updater);
    }

    void notifyFlowdOfConnectionError() {
        std::cout << "\n\n\n\nnotifyFlowdOfConnectionError()\n\n\n\n" << std::endl;
    }

    void callErrorGatewayOrFlowd() {
        std::cout << "\n\n\n\ncallErrorGatewayOrFlowd()\n\n\n\n" << std::endl;
    }

    BavsFilterConfigSharedPtr config_;
    Upstream::ClusterManager& cm_;
    std::unique_ptr<Http::RequestHeaderMapImpl> inbound_headers;
    std::unique_ptr<Buffer::OwnedImpl> original_inbound_data_;
    std::unique_ptr<Buffer::OwnedImpl> inbound_data_to_send_;
    int retries_left_;
    std::string span_id_;
    std::string instance_id_;
    std::map<std::string, std::string> saved_headers_;
    bool inbound_data_is_json_;
    std::string cm_callback_id_;
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
    void createAndSendErrorMessage(std::string msg);

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
