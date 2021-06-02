#include "bavs.h"
#include <algorithm>

namespace Envoy {
namespace Http {

std::unique_ptr<Http::RequestMessage> BavsInboundRequest::getMessage() {
    // Step 1: Get the original data sent to Envoy from the outside world.
    std::unique_ptr<RequestHeaderMap> original_headers = copyHeaders();
    const Buffer::OwnedImpl* original_data = getData();

    // Step 2: Create a message to send
    RequestMessagePtr message = std::make_unique<RequestMessageImpl>(std::move(original_headers));

    // Step 3: If context parsing is enabled, do it.
    if (config_->isClosureTransport()) {
        if (message->headers().getContentTypeValue() != "application/json") {
            std::string error_msg = "Expected to get JSON input because was expecting a context.";
            createAndSendError(CONTEXT_INPUT_ERROR, error_msg);
            return nullptr;
        }

        std::string raw_input(original_data->toString());

        std::string new_input = "{}";
        try {
            Json::ObjectSharedPtr json = Json::Factory::loadFromString(raw_input);
            if (!json->isObject()) {
                // This is possible if the data is a list (eg. '[]'). Shouldn't happen except
                // if we do a `flowctl run` with no data.
                if (!config_->inputParams().empty()) {
                    throw EnvoyException("Incoming data not an object, yet we needed to unmarshal.");
                }
                message->body().add(*original_data);
            } else {
                new_input = BavsUtil::build_json_from_params(json, config_->inputParams());
                message->body().add(new_input);
                message->headers().setContentLength(new_input.size());
                message->headers().setContentType("application/json");
                // perform token substitution from the input json
                std::string path = json_substitution(json, config_->inboundUpstream()->path());
                message->headers().setPath(path);
            }
        } catch(const EnvoyException& exn) {
            std::string error_msg = "Failed to build inbound request from context parameters: ";
            error_msg += exn.what();
            createAndSendError(CONTEXT_INPUT_ERROR, error_msg);
            return nullptr;
        }
    } /* if (config_->isClosureTransport()) */ else { 
        message->body().add(*original_data);
    }
    message->headers().setMethod(target_->method());
    return message;
}


void BavsInboundRequest::processSuccess(const AsyncClient::Request&, ResponseMessage* response) {
    // We do processing of the context output in the `InboundRequest` class
    // so that we can easily raise an error showing both input and output.
    // If we were to do this processing in `OutboundRequest`, then we would
    // not be able to send the original input along as journaling to flowd.

    Buffer::OwnedImpl data_to_send;
    std::string content_type;

    std::string status_str(response->headers().getStatusValue());
    int status = atoi(status_str.c_str());

    // Note that Envoy will return a 502 or 503 and call it a "successful" request
    // when it fails to connect to the upstream service. So we must catch it here.
    if (status == 502 || status == 503) {
        handleConnectionError();
        return;
    }

    if (config_->isClosureTransport()) {
        // If status is bad, we notify flowd or error gateway
        if (status < 200 || status >= 300) {
            if (retries_left_ > 0) {
                doRetry();
            } else {
                raiseTaskError(*response);
            }
            return;
        }
        // If we get here, then we know that the call to the inbound service (i.e. the
        // one that this Envoy Proxy is proxying) succeeded.
        // Since this is closure transport, we now need to merge the response with the
        // previous closure context.
        try {
            data_to_send.add(mergeResponseAndContext(response));
            content_type = "application/json";
        } catch (const EnvoyException& exn) {
            std::string error_msg = "Failed parsing service output: ";
            error_msg += std::string(exn.what());
            // NOTE: The error_msg can sometimes contain the '\n' character.
            // This throws off the JSON correctness. For now, we just eliminate the '\n'
            // char. TODO: in the future, write an actual json writer which can properly
            // stringify the strings.
            // Another reason for this TODO: There could be other invalid things that we
            // thave to deal with.
            error_msg.erase(std::remove(error_msg.begin(), error_msg.end(), '\n'), error_msg.end());

            createAndSendError(CONTEXT_OUTPUT_ERROR, error_msg, *response);
            return;
        }
    } else {
        data_to_send.add(response->body());
        content_type = response->headers().getContentTypeValue();
    }

    for (const UpstreamConfigSharedPtr& upstream : config_->forwardUpstreams()) {
        std::unique_ptr<Buffer::OwnedImpl> request_data = std::make_unique<Buffer::OwnedImpl>();
        request_data->add(data_to_send);
        RequestHeaderMapPtr request_headers = copyHeaders();
        request_headers->setCopy(LowerCaseString(config_->wfTIDHeader()), upstream->wfTID());

        BavsOutboundRequest* outbound_request = new BavsOutboundRequest(
            config_, std::move(request_data), std::move(request_headers),
            saved_headers_, upstream->totalAttempts() - 1,
            upstream, REQ_TYPE_OUTBOUND
        );
        outbound_request->send();
    }
}

void BavsInboundRequest::processFailure(const Http::AsyncClient::Request&,
                                        Http::AsyncClient::FailureReason) {
    handleConnectionError();
}


void BavsInboundRequest::raiseTaskError(Http::ResponseMessage& msg) {
    std::string error_message = "Service task failed.";
    std::string error_data = BavsUtil::createErrorMessage(TASK_ERROR, error_message,
                                                *getData(),
                                                *getHeaders(), msg);

    // If there's 1 or more error gateways, go through those. Else, notify flowd.
    if (config_->errorGatewayUpstreams().size() > 0) {
        for (const auto& upstream: config_->errorGatewayUpstreams()) {
            std::unique_ptr<Buffer::OwnedImpl> buf = std::make_unique<Buffer::OwnedImpl>();
            buf->add(error_data);

            std::unique_ptr<RequestHeaderMap> headers = copyHeaders();

            BavsOutboundRequest* error_req = new BavsOutboundRequest(
                config_, std::move(buf), std::move(headers),
                saved_headers_, upstream->totalAttempts() - 1, upstream,
                REQ_TYPE_ERROR
            );
            error_req->send();
        }
    } else {
        std::unique_ptr<Buffer::OwnedImpl> buf = std::make_unique<Buffer::OwnedImpl>();
        buf->add(error_data);
        std::unique_ptr<RequestHeaderMap> temp_headers = copyHeaders();
        BavsErrorRequest* error_req = new BavsErrorRequest(
                                config_, std::move(buf), std::move(temp_headers),
                                saved_headers_,
                                config_->flowdUpstream()->totalAttempts() - 1,
                                config_->flowdUpstream(), REQ_TYPE_ERROR);
        error_req->send();
    }
}

void BavsInboundRequest::createAndSendError(
    std::string error_code, std::string error_msg, ResponseMessage& response)
{
    std::string error_data = BavsUtil::createErrorMessage(
        error_code, error_msg, *getData(), *getHeaders(), response
    );
    std::unique_ptr<Buffer::OwnedImpl> buf = std::make_unique<Buffer::OwnedImpl>();
    buf->add(error_data);

    std::unique_ptr<RequestHeaderMap> temp_headers = copyHeaders();
    BavsErrorRequest* error_req = new BavsErrorRequest(
                                config_, std::move(buf), std::move(temp_headers),
                                saved_headers_,
                                config_->flowdUpstream()->totalAttempts() - 1,
                                config_->flowdUpstream(), REQ_TYPE_ERROR);
    error_req->send();
}


void BavsInboundRequest::createAndSendError(std::string error_code, std::string error_msg) {
    std::string error_data = BavsUtil::createErrorMessage(
        error_code, error_msg, *getData(), *getHeaders()
    );
    std::unique_ptr<Buffer::OwnedImpl> buf = std::make_unique<Buffer::OwnedImpl>();
    buf->add(error_data);

    std::unique_ptr<RequestHeaderMap> temp_headers = copyHeaders();
    BavsErrorRequest* error_req = new BavsErrorRequest(
                                config_, std::move(buf), std::move(temp_headers),
                                saved_headers_,
                                config_->flowdUpstream()->totalAttempts() - 1,
                                config_->flowdUpstream(), REQ_TYPE_ERROR);
    error_req->send();
}

void BavsInboundRequest::handleConnectionError() {
    if (retries_left_ > 0) {
        doRetry();
    } else {
        createAndSendError(CONNECTION_ERROR, "Failed connecting to Service task");
    }
}

void BavsInboundRequest::doRetry() {
    std::unique_ptr<RequestHeaderMap> headers = copyHeaders();
    std::unique_ptr<Buffer::OwnedImpl> data = std::make_unique<Buffer::OwnedImpl>();
    data->add(*getData());

    BavsInboundRequest* retry_request = new BavsInboundRequest(
        config_, std::move(data), std::move(headers), saved_headers_,
        retries_left_ - 1, target_, request_type_
    );
    retry_request->send();
}

std::string BavsInboundRequest::mergeResponseAndContext(Http::ResponseMessage* response) {
    Json::ObjectSharedPtr response_json = Json::Factory::loadFromString(response->body().toString());
    std::string updatee_string;
    /**
     * TODO: In the future, expand this section to support non-json messages, for example,
     * passing a small image or a byte stream as a context variable.
     */
    if (!response_json->isObject() && !response_json->isArray()) {
        if (config_->outputParams().size() > 0) {
            // TODO: in this line, we would parse the non-json content-type stuff.
            throw EnvoyException(
                "Tried to get variables, but response data was not a JSON object."
            );
        } else if (!inbound_data_is_json_) {
            throw EnvoyException("Neither input nor output is json.");
        }
        return getData()->toString();
    }

    if (!response_json->isObject()) {
        if (config_->outputParams().size() > 1) {
            throw EnvoyException("got a non-object output and more than one param.");
        }
        if (config_->outputParams().size() == 1) {
            if (config_->outputParams()[0].value() != ".") {
                throw EnvoyException("Tried to index into a non-object. Yikes!");
            }
        }
    }

    std::string updater_string = BavsUtil::build_json_from_params(
        response_json, config_->outputParams()
    );
    Json::ObjectSharedPtr updater = Json::Factory::loadFromString(updater_string);

    if (inbound_data_is_json_) {
        updatee_string = getData()->toString();
    } else {
        // We're in the first step of workflow and the input data was ignorable.
        updatee_string = "{}";
    }

    Json::ObjectSharedPtr updatee = Json::Factory::loadFromString(updatee_string);

    if (!updatee->isObject())
        throw EnvoyException("Received invalid input json from previous service.");

    // At this point, we know that we have two valid json objects: the updater (the response)
    // and the updatee (the closure context).
    return BavsUtil::merge_jsons(updatee, updater);
}

/**
 * Take an input string with (supposed) embedded token references bracketed by {}
 * e.g. "hello {world}" In this case, the token "world" is abstracted, and a node
 * with that name is searched in the json.
 */
std::string BavsInboundRequest::json_substitution(Json::ObjectSharedPtr json, const std::string& src) {
    std::string::const_iterator itr = src.begin();
    std::string trg = sub_json_token(json, itr, src.cend());
    return trg;
}

std::string BavsInboundRequest::sub_json_token(Json::ObjectSharedPtr json, std::string::const_iterator& itr, std::string::const_iterator end, int level) {
    const char BEGIN_TOK = '{';
    const char END_TOK   = '}';

    std::string trg;
    for ( ; itr != end; ++itr )
    {
        char c(*itr);
        switch(c)
        {
            case BEGIN_TOK:
                // found a nested reference - so resolve it
                trg.append(sub_json_token(json, ++itr, end, level + 1));
                break;
            case END_TOK:
                if (level) {
                    // trg contains a JSON path to be resolved, and its
                    // value used in place of the identified path
                    return json->getString(trg);
                }
                // otherwise error?
                break;
            default:
                trg.append(1,c);
                break;
        }
    }
    // if (level) error?
    return trg;
}


} // namespace Http
} // namespace Envoy


