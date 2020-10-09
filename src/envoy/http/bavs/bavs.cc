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
        forwards_.push_back(*iter);
    }
}

FilterHeadersStatus BavsFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
    const Http::HeaderEntry* entry = headers.get(Http::LowerCaseString("x-flow-id"));
    if ((entry != NULL) && (entry->value() != NULL)) {
        is_workflow_ = true;
        std::cout << "is_workflow is true" << std::endl;
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
    std::map<std::string, Envoy::VirtualServiceRoute> next_cluster_map = cluster_manager_.nextClusterMap();

    // If bad response from upstream, don't send to next step in workflow.
    std::string status_str(headers.getStatusValue());
    int status = atoi(status_str.c_str());
    if (status < 200 || status >= 300) {
        successful_response_ = false;
        std::cout << "didn't get a 200 of some sort, so leaving now" << std::endl;
        return FilterHeadersStatus::Continue;
    }

    if (req_cb_keys.empty()) {
        req_cb_keys.reserve(config_->forwards().size());
        Runtime::RandomGeneratorImpl rng;
        for (auto iter = config_->forwards().begin(); iter != config_->forwards().end(); iter++) {
            std::string req_cb_key = rng.uuid();

            // Create headers to send over to the next place.
            std::unique_ptr<RequestHeaderMapImpl> request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
                {
                    // TODO: Create headers based on forwards configuration.
                }
            );

            // Inject tracing context
            Envoy::Tracing::Span& active_span = encoder_callbacks_->activeSpan();
            active_span.injectContext(*(request_headers.get()));

            Upstream::CallbacksAndHeaders* callbacks = new Upstream::CallbacksAndHeaders(req_cb_key, std::move(request_headers), cluster_manager_);

            callbacks->setRequestStream(cluster_manager_.httpAsyncClientForCluster(*iter).start(
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
    std::cout << "encodeData enter" << std::endl;

    // intercepts the response data
    if (!is_workflow_ || !successful_response_) {
        std::cout << "not workflow - encodeData exit" << std::endl;
        return FilterDataStatus::Continue;
    }

    for (auto iter=req_cb_keys.begin(); iter != req_cb_keys.end(); iter++) {
        Upstream::CallbacksAndHeaders* cb = static_cast<Upstream::CallbacksAndHeaders*>(cluster_manager_.getCallbacksAndHeaders(*iter));

        if (cb && cb->requestStream()) {
            Buffer::OwnedImpl cpy{data};
            // sendData clears out the Buffer::Instance
            cb->requestStream()->sendData(cpy, end_stream);
        } else {
            std::cout << "These are not the droids you're trying to trace" << std::endl;
            // encoder_callbacks_->activeSpan().setTag("response_s3_key", "Unable to store response data");
        }
    }

    return FilterDataStatus::Continue;
}

} // namespace Http
} // namespace Envoy
