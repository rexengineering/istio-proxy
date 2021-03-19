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
#include "common/common/random_generator.h"

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
    wf_id_ = proto_config.wf_id();
    flowd_cluster_ = proto_config.flowd_envoy_cluster();
    flowd_path_ = proto_config.flowd_path();
    task_id_ = proto_config.task_id();
    traffic_shadow_cluster_ = proto_config.traffic_shadow_cluster();
    traffic_shadow_path_ = proto_config.traffic_shadow_path();
    for (auto iter=proto_config.headers_to_forward().begin();
              iter != proto_config.headers_to_forward().end();
              iter++) {
        headers_to_forward_.push_back(*iter);
        std::cout << "config " << *iter << std::endl;
    }
}

FilterHeadersStatus BavsFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
    const Http::HeaderEntry* entry = headers.get(Http::LowerCaseString("x-rexflow-task-id"));
    if (entry == NULL || (entry->value() != NULL && entry->value().getStringView() != config_->taskId())) {
        return FilterHeadersStatus::Continue;
    }

    entry = headers.get(Http::LowerCaseString("x-flow-id"));
    if ((entry != NULL) && (entry->value() != NULL)) {
        const Http::HeaderEntry* wf_template_entry = headers.get(Http::LowerCaseString("x-rexflow-wf-id"));
        if (wf_template_entry != NULL && wf_template_entry->value() != NULL &&
                (wf_template_entry->value().getStringView() == config_->wfIdValue())) {
            // FIXME: flow_id is a WF Instance id, and wf_id is a Workflow Template ID. Confusing.
            flow_id_ = std::string(entry->value().getStringView());
            wf_template_id_ = std::string(wf_template_entry->value().getStringView());
            is_workflow_ = true;

            for (auto iter = config_->headersToForward().begin(); 
                      iter != config_->headersToForward().end(); 
                      iter++ ) {
                // check if *iter is in headers. if so, add to request_headers
                const Http::HeaderEntry* entry = headers.get(Http::LowerCaseString(*iter));
                std::cout << "forward " << *iter << std::endl;
                if (entry != NULL && entry->value() != NULL) {
                    saved_headers_[*iter] = std::string(entry->value().getStringView());
                    std::cout << "Hello!" << std::endl;
                }
            }
        }
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
        return FilterHeadersStatus::Continue;
    }

    std::cout << "\n\n\n\nHeaders passed in:" << std::endl; // KILLME:
    headers.iterate(
        [](const HeaderEntry& header) -> HeaderMap::Iterate {
            std::cout << header.key().getStringView() << ':' << header.value().getStringView() << std::endl;
            return HeaderMap::Iterate::Continue;
        }
    );
    std::cout << "\n\n\n" << std::endl;

    // If bad response from upstream, don't send to next step in workflow.
    std::string status_str(headers.getStatusValue());
    int status = atoi(status_str.c_str());
    if (status < 200 || status >= 300) {
        std::cout << "got bad status: " << status << std::endl;
        // FIXME: Do something useful here, perhaps let Flowd know?
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
        Random::RandomGeneratorImpl rng;
        for (auto iter = config_->forwards().begin(); iter != config_->forwards().end(); iter++) {
            const UpstreamConfig& upstream(**iter);
            std::string req_cb_key = rng.uuid();

            // Create headers to send over to the next place.
            std::unique_ptr<RequestHeaderMapImpl> request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
                {
                    {Http::Headers::get().Method, upstream.method()},
                    {Http::Headers::get().Host, upstream.full_hostname() + ":" + std::to_string(upstream.port())},
                    {Http::Headers::get().Path, upstream.path()},
                    {Http::Headers::get().ContentType, content_type},
                    {Http::Headers::get().ContentLength, std::string(headers.getContentLengthValue())},
                    {Http::LowerCaseString("x-rexflow-wf-id"), wf_template_id_},
                    {Http::LowerCaseString("x-rexflow-error-after"), std::to_string(upstream.totalAttempts())},
                    {Http::LowerCaseString("x-rexflow-error-path"), config_->flowdPath()},
                    {Http::LowerCaseString("x-rexflow-task-id"), upstream.taskId()},
                    {Http::LowerCaseString("x-flow-id"), flow_id_}
                }
            );

            for (const auto& saved_header : saved_headers_) {
                std::cout << "forwarding!!" << *iter << std::endl;
                request_headers->setCopy(Http::LowerCaseString(saved_header.first), saved_header.second);
            }
            
            // Inject tracing context
            Envoy::Tracing::Span& active_span = encoder_callbacks_->activeSpan();
            active_span.injectContext(*(request_headers.get()));

            const auto trace_hdr = request_headers->get(Http::LowerCaseString("x-b3-traceid"));
            if (trace_hdr) {
                // return the trace-id to the caller
                headers.setCopy(Http::LowerCaseString("x-b3-traceid"), std::string(trace_hdr->value().getStringView()));
            }

            // Envoy speaks like "outbound|5000||secret-sauce.default.svc.cluster.local"
            std::string cluster_string = "outbound|" + std::to_string(upstream.port()) + "||" + upstream.full_hostname();
            Http::AsyncClient* client = nullptr;
            try {
                client = &(cluster_manager_.httpAsyncClientForCluster(cluster_string));
            } catch(const EnvoyException&) {
                std::cout << "Could not find the cluster " << cluster_string << " on WF Instance " << flow_id_;
                std::cout << "...sending traffic to Flowd instead." << std::endl;;

                // Try to send traffic to Flowd so at least we save the state of the WF Instance.
                try {
                    client = &(cluster_manager_.httpAsyncClientForCluster(config_->flowdCluster()));
                    request_headers->setCopy(Http::LowerCaseString("x-rexflow-original-path"), request_headers->getPathValue());
                    request_headers->setCopy(Http::LowerCaseString("x-rexflow-original-host"), request_headers->getHostValue());
                    request_headers->setPath(config_->flowdPath());
                    cluster_string = config_->flowdCluster();
                } catch(const EnvoyException&) {
                    std::cout << "Could not connect to Flowd on WF Instance " << flow_id_ << std::endl;
                    continue;
                }
            }
            if (!client) {
                // Completely out of luck.
                std::cout << "Could not connect to Flowd on WF Instance " << flow_id_ << std::endl;
                continue;
            }

            BavsRetriableCallbacks* callbacks = new BavsRetriableCallbacks(req_cb_key, std::move(request_headers), cluster_manager_,
                    upstream.totalAttempts(), config_->flowdCluster(), cluster_string, config_->flowdPath());
            callbacks->setStream(client->start(
                *callbacks, AsyncClient::StreamOptions())
            );

            if (config_->trafficShadowCluster() != "") {
                sendShadowHeaders(callbacks->requestHeaderMap());
            }
            callbacks->getStream()->sendHeaders(callbacks->requestHeaderMap(), end_stream);
            req_cb_keys.push_back(req_cb_key);
        }
    }
    return FilterHeadersStatus::Continue;
}

void BavsFilter::sendShadowHeaders(Http::RequestHeaderMapImpl& original_headers) {
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

FilterDataStatus BavsFilter::encodeData(Buffer::Instance& data, bool end_stream) {
    // intercepts the response data
    if (!is_workflow_ || !successful_response_) {
        return FilterDataStatus::Continue;
    }

    for (auto iter=req_cb_keys.begin(); iter != req_cb_keys.end(); iter++) {
        Envoy::Upstream::AsyncStreamCallbacksAndHeaders* cb = cluster_manager_.getCallbacksAndHeaders(*iter);
        if (cb != NULL) {

            BavsRetriableCallbacks* bavs_cb = dynamic_cast<BavsRetriableCallbacks*>(cb);
            if (bavs_cb) {
                bavs_cb->addData(data);
            }

            Http::AsyncClient::Stream* stream(cb->getStream());
            if (stream != NULL) {
                Buffer::OwnedImpl cpy{data};
                stream->sendData(cpy, end_stream);
            } else {
                std::cout << "NULL HTTP stream pointer!" << std::endl;
                // FIXME: Do something useful here since the request failed. Maybe notify flowd?
            }
        } else {
            std::cout << "NULL callback pointer!" << std::endl;
            // FIXME: Do something useful here since the request failed. Maybe notify flowd?
        }
    }
    return FilterDataStatus::Continue;
}

} // namespace Http
} // namespace Envoy
