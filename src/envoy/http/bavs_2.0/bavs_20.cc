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
#include "bavs_20.h"
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

void BavsFilter20::sendHeaders(bool end_stream) {
    if (!is_workflow_) return;
    auto& active_span = decoder_callbacks_->activeSpan();
    active_span.injectContext(*(request_headers_.get()));

    auto *entry = request_headers_->get(Http::LowerCaseString("x-b3-spanid"));
    if (entry) {
        spanid_ = entry->value().getStringView();
    }

    Http::AsyncClient* client = nullptr;
    try {
        client = &(cluster_manager_.httpAsyncClientForCluster(service_cluster_));
    } catch(const EnvoyException&) {
        std::cout << "The cluster wasn't found" << std::endl;
    }

    Random::RandomGeneratorImpl rng;
    callback_key_ = rng.uuid();

    callbacks_ = new BavsCallbacks(callback_key_, std::move(request_headers_), cluster_manager_);

    Http::AsyncClient::Stream* stream;
    callbacks_->setStream(client->start(*callbacks_, AsyncClient::StreamOptions()));

    // perform check to make sure it worked
    stream = callbacks_->getStream();
    if (!stream) {
        std::cout << "Failed to connect to service." << std::endl;
        return;
    }
    Http::RequestHeaderMapImpl& hdrs = callbacks_->requestHeaderMap();
    stream->sendHeaders(hdrs, end_stream);
}


FilterHeadersStatus BavsFilter20::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
    RequestHeaderMapImpl* temp = request_headers_.get();
    headers.iterate(
        [temp](const HeaderEntry& header) -> HeaderMap::Iterate {
            std::string key_string(header.key().getStringView());
            if (key_string == "x-request-id" || key_string == "user-agent") {
                return HeaderMap::Iterate::Continue;
            }
            temp->setCopy(
                Http::LowerCaseString(key_string), header.value().getStringView()
            );
            return HeaderMap::Iterate::Continue;
        }
    );
    wf_id_ = "hello";
    instance_id_ = "hello";
    successful_response_ = false;
    is_workflow_ = true;
    sendHeaders(end_stream);
    return FilterHeadersStatus::StopIteration;
}

FilterDataStatus BavsFilter20::decodeData(Buffer::Instance& data, bool end_stream) {
    if (!is_workflow_) return FilterDataStatus::Continue;

    if (!end_stream) {
        request_data_.add(data);
        return FilterDataStatus::StopIterationAndBuffer;
    }

    // first notify caller that we gotchu buddy
    std::string temp = spanid_;
    decoder_callbacks_->sendLocalReply(
        Envoy::Http::Code::Accepted,
        "For my ally is the Force, and a powerful ally it is.",
        [temp] (ResponseHeaderMap& headers) -> void {
            headers.setCopy(Http::LowerCaseString("x-b3-spanid"), temp);
        },
        absl::nullopt,
        ""
    );

    Http::AsyncClient::Stream* stream = callbacks_->getStream();
    if (!stream) {
        std::cout << "Lost connection to service." << std::endl;
    }

    stream->sendData(request_data_, true);
    return FilterDataStatus::StopIterationAndBuffer;
}

FilterHeadersStatus BavsFilter20::encodeHeaders(Http::ResponseHeaderMap&, bool) {
    return FilterHeadersStatus::Continue;
}

FilterDataStatus BavsFilter20::encodeData(Buffer::Instance&, bool) {
    // intercepts the response data
    return FilterDataStatus::Continue;
}

} // namespace Http
} // namespace Envoy
