#include <string>
#include <cstring>
#include <iostream>
#include <stdio.h>
#include <cstdlib>

#include <curl/curl.h>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/common/base64.h"
#include "bavs.h"
#include "envoy/upstream/cluster_manager.h"


#include <typeinfo>
#include <future>
#include <random>
#include <thread> 
#include <chrono>


namespace Envoy {
namespace Http {

class DummyStreamCB : public AsyncClient::StreamCallbacks {
    // virtual void onHeaders(ResponseHeaderMapPtr&&, bool end_stream) {
    virtual void onHeaders(ResponseHeaderMapPtr&&, bool) {
        std::cout << "StreamCallback::onHeaders" << std::endl;
    }

    // virtual void onData(Buffer::Instance& data, bool end_stream) {
    virtual void onData(Buffer::Instance& data, bool) {
        std::cout << "StreamCallback::onData:\n" << data.toString() << std::endl;
    }

    // virtual void onTrailers(ResponseTrailerMapPtr&& trailers) {
    virtual void onTrailers(ResponseTrailerMapPtr&&) {
        std::cout << "StreamCallback::onTrailers" << std::endl;
    }

    virtual void onComplete() {
        std::cout << "StreamCallback::onComplete" << std::endl;
    }

    virtual void onReset() {
        std::cout << "StreamCallback::onReset" << std::endl;
    }
};

static DummyStreamCB stream_callbacks_;
static std::unique_ptr<Http::RequestHeaderMapImpl> request_headers_;

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

    // Create headers to send over to the next place.
    request_headers_ = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
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
    active_span.injectContext(*(request_headers_.get()));

    std::string cluster = vsr.getCluster();
    stream_ = cluster_manager_.httpAsyncClientForCluster(cluster).start(stream_callbacks_, AsyncClient::StreamOptions());
    stream_->sendHeaders(*request_headers_.get(), end_stream);

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

    // sendData clears out the Buffer::Instance
    Buffer::OwnedImpl cpy{data};
    stream_->sendData(cpy, end_stream);

    return FilterDataStatus::Continue;
}

} // namespace Http
} // namespace Envoy
