#include <string>
#include <cstring>
#include <iostream>
#include <stdio.h>
#include <cstdlib>

#include <curl/curl.h>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/common/base64.h"
#include "bavs.h"
#include "envoy/upstream/cluster_manager.h"
#include "common/upstream/cluster_manager_impl.h"

#include <typeinfo>
#include <future>
#include <random>
#include <thread>
#include <chrono>


namespace Envoy {
namespace Http {

BavsFilterConfig::BavsFilterConfig(const bavs::BAVSFilter& proto_config) {
    forwards_.reserve(proto_config.forwards_size());
    for (auto iter=proto_config.forwards().begin();
         iter != proto_config.forwards().end();
         iter++) {
        UpstreamConfigSharedPtr forwardee(
            std::make_shared<UpstreamConfig>(UpstreamConfig(*iter))
        );
        forwards_.push_back(forwardee);
    }
}

FilterHeadersStatus BavsFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
    const Http::HeaderEntry* entry = headers.get(Http::LowerCaseString("x-flow-id"));
    if ((entry != NULL) && (entry->value() != NULL)) {
        flow_id_ = std::string(entry->value().getStringView());
        is_workflow_ = true;
        std::cout << "workflow true" << std::endl;
    } else {
        std::cout << "workflow false" << std::endl;
    }
    return FilterHeadersStatus::Continue;
}

FilterDataStatus BavsFilter::decodeData(Buffer::Instance&, bool end_stream) {
    if (end_stream) {
        // TODO: if this is the end of the stream, return a 200 to the requestor.
    }
    return FilterDataStatus::Continue;
}

FilterHeadersStatus BavsFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
    // intercepts the Response headers.
    if (!is_workflow_) {
        std::cout << "not workflow - encodeHeaders exit" << std::endl;
        return FilterHeadersStatus::Continue;
    }

    // If bad response from upstream, don't send to next step in workflow.
    std::string status_str(headers.getStatusValue());
    int status = atoi(status_str.c_str());
    if (status < 200 || status >= 300) {
        std::cout << "got bad status: " << status << std::endl;
        successful_response_ = false;
        return FilterHeadersStatus::Continue;
    }

    if (req_cb_keys.empty()) {
        const Http::HeaderEntry* entry(headers.get(Http::Headers::get().ContentType));
        // FIXME:  The following assumes JSON content if it can't find a content
        // type in the initial batch of headers.  What if content type is sent in
        // a later invocation of encodeHeaders()?
        std::string content_type(
            ((entry != NULL) && (entry->value() != NULL)) ?
                entry->value().getStringView() :
                "application/json"
        );
        req_cb_keys.reserve(config_->forwards().size());
        Runtime::RandomGeneratorImpl rng;
        for (auto iter = config_->forwards().begin(); iter != config_->forwards().end(); iter++) {
            const UpstreamConfig& upstream(**iter);
            std::string req_cb_key = rng.uuid();

            // Create headers to send over to the next place.
            std::unique_ptr<RequestHeaderMapImpl> request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
                {
                    {Http::Headers::get().Method, upstream.method()},
                    {Http::Headers::get().Host, upstream.host() + ":" + std::to_string(upstream.port())},
                    {Http::Headers::get().Path, upstream.path()},
                    {Http::Headers::get().ContentType, content_type},
                    {Http::Headers::get().ContentLength, std::string(headers.getContentLengthValue())},
                    {Http::LowerCaseString("x-flow-id"), flow_id_}
                }
            );

            // Inject tracing context
            Envoy::Tracing::Span& active_span = encoder_callbacks_->activeSpan();
            active_span.injectContext(*(request_headers.get()));

            Upstream::CallbacksAndHeaders* callbacks = new Upstream::CallbacksAndHeaders(req_cb_key, std::move(request_headers), cluster_manager_);

            // Envoy speaks like "outbound|5000||secret-sauce.default.svc.cluster.local"
            std::string cluster_string = "outbound|" + std::to_string(upstream.port()) + "||" + upstream.host();
            cluster_string += ".default.svc.cluster.local";

            std::cout << "cluster_string: " << cluster_string << std::endl;

            Http::AsyncClient* client = nullptr;
            try {
                client = &(cluster_manager_.httpAsyncClientForCluster(cluster_string));
            } catch(const EnvoyException&) {
                for (const auto pair : cluster_manager_.clusters()) {
                    std::cout << pair.first << std::endl;
                }

                std::cout << "WHOOOOOAAAAAAAAA Houston we've got a problem, the cluster doesn't exist" << std::endl;
                continue;
            }
            if (!client) {
                std::cout << "WHOOOOOOAAAAAAA we couldn't get the client" << std::endl;
                continue;
            }
            callbacks->setRequestStream(client->start(
                *callbacks, AsyncClient::StreamOptions())
            );
            callbacks->requestStream()->sendHeaders(callbacks->requestHeaderMap(), end_stream);
            callbacks->setRequestKey(req_cb_key);
            req_cb_keys.push_back(req_cb_key);
        }
    }

    std::cout << "encodeHeaders exit" << std::endl;
    return FilterHeadersStatus::Continue;
}

FilterDataStatus BavsFilter::encodeData(Buffer::Instance& data, bool end_stream) {
    std::cout << "encodeData(): entered, end_stream=" << std::to_string(end_stream) << std::endl;

    // intercepts the response data
    if (!is_workflow_ || !successful_response_) {
        std::cout << "not workflow - encodeData exit" << std::endl;
        return FilterDataStatus::Continue;
    }

    for (auto iter=req_cb_keys.begin(); iter != req_cb_keys.end(); iter++) {
        Upstream::CallbacksAndHeaders* cb =
            static_cast<Upstream::CallbacksAndHeaders*>(
                cluster_manager_.getCallbacksAndHeaders(*iter));
        if (cb != NULL) {
            Http::AsyncClient::Stream* stream(cb->requestStream());
            if (stream != NULL) {
                Buffer::OwnedImpl cpy{data};
                stream->sendData(cpy, end_stream);
            } else {
                std::cout << "NULL HTTP stream pointer!" << std::endl;
            }
        } else {
            std::cout << "NULL callback pointer!" << std::endl;
        }
    }
    return FilterDataStatus::Continue;
}

} // namespace Http
} // namespace Envoy
