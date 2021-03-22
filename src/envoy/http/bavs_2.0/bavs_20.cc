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
    std::cout << "going to iterate" << std::endl;
    std::unique_ptr<RequestHeaderMap> request_headers = Http::RequestHeaderMapImpl::create();

    // RequestHeaderMapImpl* temp = static_cast<RequestHeaderMapImpl*>(request_headers.get());
    std::cout << "request_headers are " << &request_headers << std::endl;
    // headers.iterate(
    //     [temp](const HeaderEntry& header) -> HeaderMap::Iterate {
    //         std::string key_string(header.key().getStringView());
    //         temp->setCopy(
    //             Http::LowerCaseString(key_string), header.value().getStringView()
    //         );
    //         return HeaderMap::Iterate::Continue;
    //     }
    // );
    request_headers->setCopy(Http::LowerCaseString("method"), headers.getMethodValue());
    request_headers->setCopy(Http::LowerCaseString("content-type"), headers.getContentTypeValue());
    std::cout << "done iterating " << std::endl;
    message_.reset(new RequestMessageImpl(std::move(request_headers)));

    wf_id_ = "hello";
    instance_id_ = "hello";
    successful_response_ = false;
    is_workflow_ = true;
    std::cout << "hello there" << std::endl;
    return FilterHeadersStatus::Continue;
}

static BavsCallbacks *callbacks = new BavsCallbacks();

FilterDataStatus BavsFilter20::decodeData(Buffer::Instance& data, bool end_stream) {
    if (!end_stream) {
        message_->body().add(data);
        return FilterDataStatus::Continue;
    }
    for (const auto& pair : cluster_manager_.clusters()) {
        std::cout << pair.first << std::endl;
    }

    Http::AsyncClient* client = nullptr;
    try {
        client = &(cluster_manager_.httpAsyncClientForCluster(service_cluster_));
    } catch(const EnvoyException&) {
        std::cout << "The cluster wasn't found" << std::endl;
    }
    if (!client) return FilterDataStatus::Continue;

    std::cout << "got the client, now sending the message" << std::endl;
    // callbacks = new BavsCallbacks();
    client->send(std::move(message_), *callbacks, AsyncClient::RequestOptions());
    std::cout << "sent the message." << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));

    return FilterDataStatus::Continue;
}

FilterHeadersStatus BavsFilter20::encodeHeaders(Http::ResponseHeaderMap&, bool) {
    return FilterHeadersStatus::Continue;
}

FilterDataStatus BavsFilter20::encodeData(Buffer::Instance& data, bool end_stream) {
    // intercepts the response data
    std::cout << "encodeData: " << data.toString() << end_stream << std::endl;
    return FilterDataStatus::Continue;
}

} // namespace Http
} // namespace Envoy
