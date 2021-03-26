#include "bavs.h"

namespace Envoy {
namespace Http {

/**
 * The role of this function is to intercept the response headers from the call to the
 * inbound upstream service (i.e. the service that this Envoy is proxying). We then
 * decide where to send the traffic to and initialize some BavsOutboundCallbacks 
 */
void BavsInboundCallbacks::onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {

    // If bad response from upstream, don't send to next step in workflow.
    std::string status_str(headers->getStatusValue());
    int status = atoi(status_str.c_str());
    if (status < 200 || status >= 300) {
        std::cout << "got bad status: " << status << std::endl;
        // FIXME: Do something useful here, perhaps let Flowd know?
        return;
    }

    const Http::HeaderEntry* content_type_entry(headers->get(Http::Headers::get().ContentType));
    // Safe since onHeaders() can only be called once per http request
    std::string content_type(((content_type_entry != NULL) && (content_type_entry->value() != NULL)) ?
        content_type_entry->value().getStringView() : "application/json"
    );
    Random::RandomGeneratorImpl rng;
    for (UpstreamConfigSharedPtr upstream_ptr : config_->forwards()) {
        const UpstreamConfig& upstream(*upstream_ptr);
        std::string req_cb_key = rng.uuid();

        // Create headers to send over to the next place.
        std::unique_ptr<RequestHeaderMapImpl> request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
            {
                {Http::Headers::get().Method, upstream.method()},
                {Http::Headers::get().Host, upstream.full_hostname() + ":" + std::to_string(upstream.port())},
                {Http::Headers::get().Path, upstream.path()},
                {Http::Headers::get().ContentType, content_type},
                {Http::Headers::get().ContentLength, std::string(headers->getContentLengthValue())},
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

        // Envoy speaks like "outbound|5000||secret-sauce.default.svc.cluster.local"
        std::string cluster_string = "outbound|" + std::to_string(upstream.port()) + "||" + upstream.full_hostname();
        Http::AsyncClient* client = nullptr;
        try {
            client = &(cluster_manager_.httpAsyncClientForCluster(cluster_string));
        } catch(const EnvoyException&) {
            std::cout << "Could not find the cluster " << cluster_string << " on WF Instance " << instance_id_;
            std::cout << "...sending traffic to Flowd instead." << std::endl;;

            // Try to send traffic to Flowd so at least we save the state of the WF Instance.
            try {
                client = &(cluster_manager_.httpAsyncClientForCluster(config_->flowdCluster()));
                request_headers->setCopy(Http::LowerCaseString("x-rexflow-original-path"), request_headers->getPathValue());
                request_headers->setCopy(Http::LowerCaseString("x-rexflow-original-host"), request_headers->getHostValue());
                request_headers->setPath(config_->flowdPath());
                cluster_string = config_->flowdCluster();
            } catch(const EnvoyException&) {
                std::cout << "Could not connect to Flowd on WF Instance " << instance_id_ << std::endl;
                continue;
            }
        }
        if (!client) {
            // Completely out of luck.
            std::cout << "Could not connect to Flowd on WF Instance " << instance_id_ << std::endl;
            continue;
        }

        BavsOutboundCallbacks* callbacks = new BavsOutboundCallbacks(
            req_cb_key, std::move(request_headers), cluster_manager_,
            upstream.totalAttempts(), config_->flowdCluster(), cluster_string, config_->flowdPath(),
            upstream_ptr);
        callbacks->setStream(client->start(
            *callbacks, AsyncClient::StreamOptions())
        );

        if (config_->trafficShadowCluster() != "") {
            sendShadowHeaders(callbacks->requestHeaderMap());
        }
        if (end_stream) {
            callbacks->getStream()->sendHeaders(callbacks->requestHeaderMap(), end_stream);
        }
        req_cb_keys.push_back(req_cb_key);
    }
}

void BavsInboundCallbacks::sendAllHeaders(bool end_stream) {
    for (auto& key : req_cb_keys) {
        Envoy::Upstream::AsyncStreamCallbacksAndHeaders* cb = 
            cluster_manager_.getCallbacksAndHeaders(key);
        if (cb != NULL) {
            Http::AsyncClient::Stream* stream(cb->getStream());
            if (stream != NULL) {
                auto& hdr_map = cb->requestHeaderMap();
                if (did_update_request_data_) {
                    std::cout << "updating headers " << std::endl;
                    hdr_map.setContentType("application/json");
                    hdr_map.setContentLength(request_data_.length());
                } else std::cout << "didn't update headers " << std::endl;
                stream->sendHeaders(hdr_map, end_stream);
            } else {
                std::cout << "NULL HTTP stream pointer!" << std::endl;
                // FIXME: Do something useful here since the request failed. Maybe notify flowd?
            }
        } else {
            std::cout << "NULL callback pointer!" << std::endl;
            // FIXME: Do something useful here since the request failed. Maybe notify flowd?
        }
    }
}

void BavsInboundCallbacks::sendShadowHeaders(Http::RequestHeaderMapImpl& original_headers) {
    // copy the headers
    std::unique_ptr<RequestHeaderMapImpl> headers = Http::RequestHeaderMapImpl::create();
    original_headers.iterate(
        [&headers](const HeaderEntry& header) -> HeaderMap::Iterate {
            RequestHeaderMapImpl* hdrs = headers.get();
            std::string key(header.key().getStringView());
            hdrs->addCopy(Http::LowerCaseString(key), header.value().getStringView());
            return HeaderMap::Iterate::Continue;
        }
    );
    headers->setCopy(Http::LowerCaseString("x-rexflow-original-host"), headers->getHostValue()); // host gets overwritten
    headers->setCopy(Http::LowerCaseString("x-rexflow-original-path"), headers->getPathValue()); // host gets overwritten
    headers->setPath(config_->trafficShadowPath());

    Random::RandomGeneratorImpl rng;
    std::string guid = rng.uuid();
    Envoy::Upstream::CallbacksAndHeaders* callbacks = new Upstream::CallbacksAndHeaders(guid, std::move(headers), cluster_manager_);
    Http::AsyncClient* client = nullptr;
    try {
        client = &(cluster_manager_.httpAsyncClientForCluster(config_->trafficShadowCluster()));
    } catch(const EnvoyException&) {
        std::cout << "Couldn't find Kafka cluster: " << config_->trafficShadowCluster() << std::endl;
        return;
    }
    if (!client) return;
    callbacks->setStream(client->start(*callbacks, AsyncClient::StreamOptions()));
    if (callbacks->getStream()) {
        callbacks->getStream()->sendHeaders(callbacks->requestHeaderMap(), false);
    }
    req_cb_keys.push_back(guid);
}

void BavsInboundCallbacks::onData(Buffer::Instance& data, bool end_stream) {
    std::cout << "InboundCallbacks onData: " << data.toString() << std::endl;
    request_data_.add(data);
    if (!end_stream) return;

    if (!config_->outputParams().empty()) {
        std::string raw_input(request_data_.toString());
        std::string new_input = "{}";
        Json::ObjectSharedPtr json_obj;
        try {
            json_obj = Json::Factory::loadFromString(raw_input);
            if (!json_obj->isObject()) {
                throw new EnvoyException("Not a real json object");
            }
            new_input = build_json_from_params(json_obj, config_->outputParams());
        } catch(const EnvoyException& exn) {
            // TODO: handle errors
            std::cout << exn.what() << "Unable to process input:\n" << raw_input << std::endl;
        }
        request_data_.drain(request_data_.length());
        std::cout << "New input:: " << new_input << std::endl;

        request_data_.add(new_input);
        did_update_request_data_ = true;
    }

    if (config_->isClosureTransport() && inbound_data_is_json_) {  // TODO: Make this part of the config_
        Json::ObjectSharedPtr updater = Json::Factory::loadFromString(request_data_.toString());
        Json::ObjectSharedPtr updatee = Json::Factory::loadFromString(context_input_);
        if (updater->isObject() && updatee->isObject()) {
            std::string new_data = merge_jsons(updatee, updater);
            request_data_.drain(request_data_.length());
            request_data_.add(new_data);
            did_update_request_data_ = true;
        } else {
            std::cout << "Unable to perform merge." << std::endl;
            // TODO: Error handling.
        }
    }

    sendAllHeaders(false);
    std::cout << "Going to send " << request_data_.toString() << std::endl;

    for (std::string& key : req_cb_keys) {
        Envoy::Upstream::AsyncStreamCallbacksAndHeaders* cb =  cluster_manager_.getCallbacksAndHeaders(key);
        if (cb != NULL) {
            BavsOutboundCallbacks* bavs_cb = dynamic_cast<BavsOutboundCallbacks*>(cb);
            if (bavs_cb) {
                // special method for the BavsOutboundCallbacks to enable retries, etc.
                bavs_cb->addData(request_data_);
            }
            Http::AsyncClient::Stream* stream(cb->getStream());
            if (stream != NULL) {
                Buffer::OwnedImpl cpy;
                cpy.add(request_data_);
                stream->sendData(cpy, true);
            } else {
                std::cout << "NULL HTTP stream pointer!" << std::endl;
                // FIXME: Do something useful here since the request failed. Maybe notify flowd?
            }
        } else {
            std::cout << "NULL callback pointer!" << std::endl;
            // FIXME: Do something useful here since the request failed. Maybe notify flowd?
        }
        std::cout << "completed " << key << std::endl;
    }
}

} // namespace Http
} // namespace Envoy