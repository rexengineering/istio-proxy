#include "bavs.h"

namespace Envoy {
namespace Http {

BavsInboundRequest::BavsInboundRequest(BavsFilterConfigSharedPtr config, Upstream::ClusterManager& cm,
                       std::unique_ptr<Http::RequestHeaderMapImpl> inbound_headers,
                       std::unique_ptr<Buffer::OwnedImpl> original_inbound_data,
                       std::unique_ptr<Buffer::OwnedImpl> inbound_data_to_send,
                       int retries_left, std::string span_id, std::string instance_id,
                       std::map<std::string, std::string> saved_headers,
                       bool inbound_data_is_json, std::string service_cluster) :
                       config_(config), cm_(cm), inbound_headers_(std::move(inbound_headers)),
                       original_inbound_data_(std::move(original_inbound_data)),
                       inbound_data_to_send_(std::move(inbound_data_to_send)),
                       retries_left_(retries_left), span_id_(span_id), instance_id_(instance_id),
                       saved_headers_(saved_headers), inbound_data_is_json_(inbound_data_is_json),
                       service_cluster_(service_cluster) {
    Random::RandomGeneratorImpl rng;
    cm_callback_id_ = rng.uuid();
    cm_.storeRequestCallbacks(cm_callback_id_, this);
}

void BavsInboundRequest::raiseTaskError(Http::ResponseMessage& msg) {
    std::string error_message = "Service task failed.";
    std::string error_data = createErrorMessage(TASK_ERROR, error_message,
                                                *original_inbound_data_,
                                                *inbound_headers_, msg);

    std::unique_ptr<Buffer::OwnedImpl> buf = std::make_unique<Buffer::OwnedImpl>();
    buf->add(error_data);
    BavsErrorRequest* error_req = new BavsErrorRequest(
                                cm_, config_->flowdCluster(), std::move(buf),
                                std::move(inbound_headers_), config_->flowdPath());
    error_req->send();

    cm_.eraseRequestCallbacks(cm_callback_id_);
}

void BavsInboundRequest::raiseContextOutputParsingError(Http::ResponseMessage& msg) {
    std::string error_message = "Failed parsing service response for output context params.";
    std::string error_data = createErrorMessage(CONTEXT_OUTPUT_PARSING_ERROR, error_message,
                                                *original_inbound_data_,
                                                *inbound_headers_, msg);

    std::unique_ptr<Buffer::OwnedImpl> buf = std::make_unique<Buffer::OwnedImpl>();
    buf->add(error_data);
    BavsErrorRequest* error_req = new BavsErrorRequest(
                                cm_, config_->flowdCluster(), std::move(buf),
                                std::move(inbound_headers_), config_->flowdPath());
    error_req->send();

    cm_.eraseRequestCallbacks(cm_callback_id_);
}

void BavsInboundRequest::raiseConnectionError() {
    std::string error_message = "Failed connecting to service task.";
    std::string error_data = createErrorMessage(CONNECTION_ERROR, error_message,
                                                *original_inbound_data_,
                                                *inbound_headers_);

    std::unique_ptr<Buffer::OwnedImpl> buf = std::make_unique<Buffer::OwnedImpl>();
    buf->add(error_data);
    BavsErrorRequest* error_req = new BavsErrorRequest(
                                cm_, config_->flowdCluster(), std::move(buf),
                                std::move(inbound_headers_), config_->flowdPath());
    error_req->send();

    cm_.eraseRequestCallbacks(cm_callback_id_);

}

void BavsInboundRequest::onSuccess(const Http::AsyncClient::Request&,
                                   Http::ResponseMessagePtr&& response) {
    Buffer::OwnedImpl data_to_send;
    std::string content_type;

    if (config_->isClosureTransport()) {
        // If status is bad, we notify flowd or error gateway
        std::string status_str(response->headers().getStatusValue());
        int status = atoi(status_str.c_str());

        if (status < 200 || status >= 300) {
            if (retries_left_ > 0) {
                BavsInboundRequest* retry_request = new BavsInboundRequest(
                                                config_, cm_ , std::move(inbound_headers_),
                                                std::move(original_inbound_data_), 
                                                std::move(inbound_data_to_send_),
                                                retries_left_ - 1, span_id_, instance_id_,
                                                saved_headers_, inbound_data_is_json_,
                                                service_cluster_);
                retry_request->send();
                cm_.eraseRequestCallbacks(cm_callback_id_);
                return;
            } else {
                raiseTaskError(*response);
                return;
            }
        }
        // If we get here, then we know that the call to the inbound service (i.e. the
        // one that this Envoy Proxy is proxying) succeeded.
        // Since this is closure transport, we now need to merge the response with the
        // previous closure context.
        try {
            data_to_send.add(mergeResponseAndContext(response));
            content_type = "application/json";
        } catch (const EnvoyException& exn) {
            raiseContextOutputParsingError(*response);
            return;
        }
    } else {
        data_to_send.add(response->body());
        content_type = response->headers().getContentTypeValue();
    }

    size_t content_length = data_to_send.length();
    for (const UpstreamConfigSharedPtr& upstream : config_->forwards()) {
        std::unique_ptr<Http::RequestHeaderMapImpl> request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
            {
                {Http::Headers::get().Method, upstream->method()},
                {Http::Headers::get().Host, upstream->full_hostname() + ":" + std::to_string(upstream->port())},
                {Http::Headers::get().Path, upstream->path()},
                {Http::LowerCaseString("x-rexflow-wf-id"), config_->wfIdValue()},
                {Http::LowerCaseString("x-rexflow-task-id"), upstream->taskId()},
                {Http::LowerCaseString("x-flow-id"), instance_id_}
            }
        );
        for (const auto& saved_header : saved_headers_) {
            request_headers->setCopy(Http::LowerCaseString(saved_header.first), saved_header.second);
        }
        // Inject tracing context
        request_headers->setCopy(Http::LowerCaseString("x-b3-spanid"), span_id_);
        request_headers->setContentLength(content_length);
        request_headers->setContentType(content_type);

        std::unique_ptr<Buffer::OwnedImpl> data_to_send_to_this_upstream = std::make_unique<Buffer::OwnedImpl>();
        data_to_send_to_this_upstream->add(data_to_send);

        std::string cluster_string = "outbound|" + std::to_string(upstream->port());
        cluster_string += "||" + upstream->full_hostname();
        BavsOutboundRequest* outbound_request = new BavsOutboundRequest(
                                             cm_, cluster_string, config_->flowdCluster(),
                                             upstream->totalAttempts() - 1, std::move(request_headers),
                                             std::move(data_to_send_to_this_upstream), upstream->taskId(),
                                             config_->flowdPath());
        outbound_request->send();
    }
    cm_.eraseRequestCallbacks(cm_callback_id_);
}

void BavsInboundRequest::onFailure(const Http::AsyncClient::Request&,
                                   Http::AsyncClient::FailureReason) {
    if (retries_left_ > 0) {
        BavsInboundRequest* retry_request = new BavsInboundRequest(
                                         config_, cm_ , std::move(inbound_headers_),
                                         std::move(original_inbound_data_),
                                         std::move(inbound_data_to_send_),
                                         retries_left_ - 1, span_id_, instance_id_,
                                         saved_headers_, inbound_data_is_json_,
                                         service_cluster_);
        retry_request->send();
    } else {
        raiseConnectionError();
    }
    cm_.eraseRequestCallbacks(cm_callback_id_);
}

void BavsInboundRequest::send() {
    // First, form the message
    std::unique_ptr<Http::RequestMessageImpl> message = std::make_unique<Http::RequestMessageImpl>();

    RequestHeaderMap* temp = &message->headers();
    inbound_headers_->iterate([temp] (const HeaderEntry& header) -> HeaderMap::Iterate {
        std::string hdr_key(header.key().getStringView());
        temp->setCopy(Http::LowerCaseString(hdr_key), header.value().getStringView());
        return HeaderMap::Iterate::Continue; 
    });

    message->body().add(*inbound_data_to_send_);

    // Second, send the message
    Http::AsyncClient* client = NULL;
    try {
        client = &(cm_.httpAsyncClientForCluster(service_cluster_));
    } catch(const EnvoyException&) {
        // The cluster wasn't found, so we need to begin error processing.
        raiseConnectionError();
        return;
    }
    client->send(std::move(message), *this, Http::AsyncClient::RequestOptions());
}

Http::RequestHeaderMapPtr BavsInboundRequest::createOutboundHeaders(
            UpstreamConfigSharedPtr upstream_ptr) {
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
    hdrs->setCopy(Http::LowerCaseString("x-b3-spanid"), span_id_);
    return std::move(hdrs);
}

std::string BavsInboundRequest::mergeResponseAndContext(Http::ResponseMessagePtr& response) {
    Json::ObjectSharedPtr response_json = Json::Factory::loadFromString(response->body().toString());
    std::string updatee_string;
    /**
     * TODO: In the future, expand this section to support non-json messages, for example,
     * passing a small image or a byte stream as a context variable.
     */
    if (!response_json->isObject()) {
        if (config_->outputParams().size() > 0) {
            // TODO: in this line, we would parse the non-json content-type stuff.
            throw EnvoyException(
                "Tried to get variables, but response data was not a JSON object."
            );
        } else if (!inbound_data_is_json_) {
            throw EnvoyException("Neither input nor output is json.");
        }
        return original_inbound_data_->toString();
    }

    std::string updater_string = build_json_from_params(response_json, config_->outputParams());
    Json::ObjectSharedPtr updater = Json::Factory::loadFromString(updater_string);

    if (inbound_data_is_json_) {
        updatee_string = original_inbound_data_->toString();
    } else {
        // We're in the first step of workflow and the input data was ignorable.
        updatee_string = "{}";
    }

    Json::ObjectSharedPtr updatee = Json::Factory::loadFromString(updatee_string);

    if (!updatee->isObject())
        throw EnvoyException("Received invalid input json from previous service.");

    // At this point, we know that we have two valid json objects: the updater (the response)
    // and the updatee (the closure context).
    return merge_jsons(updatee, updater);
}


} // namespace Http
} // namespace Envoy