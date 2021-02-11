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

FilterHeadersStatus BavsFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
    const Http::HeaderEntry* entry = headers.get(Http::LowerCaseString("decisionpoint"));
    if ((entry != NULL) && (entry->value() != NULL)) {
        is_workflow_ = true;
        absl::string_view sv = entry->value().getStringView();
        std::string ss(sv.data(), static_cast<int>(sv.size()));
        decisionpoint_id_ = atoi(ss.c_str());
        std::cout << "is_workflow is true" << std::endl;
    }
    return FilterHeadersStatus::Continue;
}

FilterDataStatus BavsFilter::decodeData(Buffer::Instance&, bool) {
    return FilterDataStatus::Continue;
}

FilterHeadersStatus BavsFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
    // intercepts the Response headers.
    if (!is_workflow_) {
        std::cout << "not workflow - encodeHeaders exit" << std::endl;
        return FilterHeadersStatus::Continue;
    }
    std::map<std::string, Envoy::VirtualServiceRoute> next_cluster_map = cluster_manager_.nextClusterMap();
    if (!next_cluster_map.count(std::to_string(decisionpoint_id_ + 1))) {
        is_workflow_ = false;
        std::cout << "At the last step in the workflow, so doing nothing" << std::endl;
        return FilterHeadersStatus::Continue;
    }

    // If bad response from upstream, don't send to next step in workflow.
    std::string status_str(headers.getStatusValue());
    int status = atoi(status_str.c_str());
    if (status < 200 || status >= 300) {
        successful_response_ = false;
        std::cout << "didn't get a 200 of some sort, so leaving now" << std::endl;
        return FilterHeadersStatus::Continue;
    }

    const auto vsr = next_cluster_map[std::to_string(decisionpoint_id_ + 1)];

    Runtime::RandomGeneratorImpl rng;
    req_cb_key_ = rng.uuid();

    // Create headers to send over to the next place.
    std::unique_ptr<RequestHeaderMapImpl> request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
        {
            {Http::Headers::get().Method, vsr.getMethod()},
            {Http::Headers::get().Host, "bavs-host:9881"},
            {Http::Headers::get().Path, vsr.getPath()},
            {Http::Headers::get().ContentType, "application/json"},
            {Http::Headers::get().ContentLength, std::string(headers.getContentLengthValue())},
            {Http::LowerCaseString("decisionpoint"), std::to_string(decisionpoint_id_ + 1)}
        }
    );

    // Inject tracing context
    Envoy::Tracing::Span& active_span = encoder_callbacks_->activeSpan();
    active_span.injectContext(*(request_headers.get()));

    std::string cluster = vsr.getCluster();

    Upstream::CallbacksAndHeaders* callbacks = new Upstream::CallbacksAndHeaders(req_cb_key_, std::move(request_headers), cluster_manager_);

    callbacks->setRequestStream(cluster_manager_.httpAsyncClientForCluster(cluster).start(
        *callbacks, AsyncClient::StreamOptions())
    );
    callbacks->requestStream()->sendHeaders(callbacks->requestHeaderMap(), end_stream);
    callbacks->setRequestKey(req_cb_key_);

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

    Upstream::CallbacksAndHeaders* cb = static_cast<Upstream::CallbacksAndHeaders*>(cluster_manager_.getCallbacksAndHeaders(req_cb_key_));

    if (cb && cb->requestStream()) {
        Buffer::OwnedImpl cpy{data};
        // sendData clears out the Buffer::Instance
        cb->requestStream()->sendData(cpy, end_stream);
    } else {
        std::cout << "These are not the droids you're trying to trace" << std::endl;
        // encoder_callbacks_->activeSpan().setTag("response_s3_key", "Unable to store response data");
    }

    return FilterDataStatus::Continue;
}

} // namespace Http
} // namespace Envoy
