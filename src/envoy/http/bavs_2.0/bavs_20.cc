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


FilterHeadersStatus BavsFilter20::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
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

    request_headers_->iterate(
        [](const HeaderEntry& header) -> HeaderMap::Iterate {
            std::cout << header.key().getStringView() << ": " << header.value().getStringView() << std::endl;
            return HeaderMap::Iterate::Continue;
        }
    );

    wf_id_ = "hello";
    instance_id_ = "hello";
    successful_response_ = false;
    is_workflow_ = true;
    return FilterHeadersStatus::Continue;
}

FilterDataStatus BavsFilter20::decodeData(Buffer::Instance& data, bool end_stream) {
    if (!end_stream) {
        request_data_.add(data);
        return FilterDataStatus::Continue;
    }

    Http::AsyncClient* client = nullptr;
    try {
        client = &(cluster_manager_.httpAsyncClientForCluster(service_cluster_));
    } catch(const EnvoyException&) {
        std::cout << "The cluster wasn't found" << std::endl;
    }
    if (!client) return FilterDataStatus::Continue;

    Random::RandomGeneratorImpl rng;
    std::string callback_key = rng.uuid();

    BavsCallbacks* callbacks = new BavsCallbacks(
        callback_key, std::move(request_headers_), cluster_manager_);

    Http::AsyncClient::Stream* stream;
    callbacks->setStream(client->start(*callbacks, AsyncClient::StreamOptions()));

    // perform check to make sure it worked
    stream = callbacks->getStream();
    if (!stream) {
        std::cout << "Failed to connect to service." << std::endl;
    }
    Http::RequestHeaderMapImpl& hdrs = callbacks->requestHeaderMap();
    stream->sendHeaders(hdrs, end_stream);

    stream = callbacks->getStream();
    if (!stream) {
        std::cout << "Lost connection to service." << std::endl;
    }

    std::cout << " sending data**" << request_data_.toString() << "**" << std::endl;
    std::cout << " sending data**" << request_data_.toString() << "**" << std::endl;
    stream->sendData(request_data_, true);
    // Buffer::OwnedImpl empty_buf;
    // stream->sendData(empty_buf, true);
    // std::cout << "done" << std::endl;
    return FilterDataStatus::Continue;
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
