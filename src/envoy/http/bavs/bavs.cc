#include <string>
#include <cstring>
#include <iostream>
#include <stdio.h>
#include <cstdlib>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/common/base64.h"
#include "bavs.h"
#include "common/upstream/cluster_manager_impl.h"
#include "common/common/random_generator.h"

#include <typeinfo>
#include <future>
#include <random>
#include <thread>
#include <chrono>


namespace Envoy {
namespace Http {

std::string process_json_input(std::string& input_str,
        std::vector<std::pair<std::string, std::string>> input_params) {

    std::cout << " going to parse " << std::endl;
    // Json::ObjectSharedPtr json_obj = Json::Factory::loadFromString(input_str);
    Json::Factory::loadFromString(input_str);
    std::cout << "just parsed" << std::endl;

    std::cout << input_str << std::endl;
    for (auto const& param : input_params) {
        std::cout << "param: " << param.first << " " << param.second << std::endl;
        // if (!json_obj->hasObject(pair.first)) {
        //     std::cout << pair.first << " not found!!!\n" << std::endl;
        //     continue;
        // }
        // std::cout << "found " << pair.first << std::endl;
        // Json::ObjectSharedPtr cur_obj = json_obj->getObject(pair.first);

        // std::cout << "asJsonString(): " << cur_obj->asJsonString() << std::endl;
    }

    return "{\"val\":12345}";
}


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
    for (const std::string& header : proto_config.headers_to_forward()) {
        headers_to_forward_.push_back(header);
    }
    for (auto param : proto_config.input_params()) {
        input_params_.push_back(std::make_pair(param.name(), param.value()));
    }
    for (auto param : proto_config.output_params()) {
        output_params_.push_back(std::make_pair(param.name(), param.value()));
    }
}

FilterHeadersStatus BavsFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
    const Http::HeaderEntry* task_entry = headers.get(Http::LowerCaseString("x-rexflow-task-id"));
    if (task_entry == NULL || (task_entry->value() != NULL && task_entry->value().getStringView() != config_->taskId())) {
        return FilterHeadersStatus::Continue;
    }


    const Http::HeaderEntry* wf_id_entry = headers.get(Http::LowerCaseString("x-rexflow-wf-id"));
    if (wf_id_entry == NULL || wf_id_entry->value() == NULL ||
            (wf_id_entry->value().getStringView() != config_->wfIdValue())) {
        return FilterHeadersStatus::Continue;
    }

    const Http::HeaderEntry* instance_entry = headers.get(Http::LowerCaseString("x-flow-id"));
    if ((instance_entry == NULL) || (instance_entry->value() == NULL)) {
        return FilterHeadersStatus::Continue;
    }
    instance_id_ = instance_entry->value().getStringView();

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
            return HeaderMap::Iterate::Continue; // for lambda function, not the whole thing.
        }
    );

    // Now, save headers that we need to propagate to the next service in line.
    for (auto iter = config_->headersToForward().begin(); 
                iter != config_->headersToForward().end(); 
                iter++ ) {
        // check if *iter is in headers. if so, add to request_headers
        const Http::HeaderEntry* entry = headers.get(Http::LowerCaseString(*iter));
        if (entry != NULL && entry->value() != NULL) {
            saved_headers_[*iter] = std::string(entry->value().getStringView());
        }
    }

    successful_response_ = false;
    is_workflow_ = true;
    if (end_stream) sendHeaders(true);
    return FilterHeadersStatus::StopIteration;
}

void BavsFilter::sendHeaders(bool end_stream) {
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

    callbacks_ = new BavsInboundCallbacks(
        callback_key_, std::move(request_headers_), cluster_manager_, config_,
        saved_headers_, instance_id_, spanid_);

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


FilterDataStatus BavsFilter::decodeData(Buffer::Instance& data, bool end_stream) {
    if (!is_workflow_) return FilterDataStatus::Continue;

    if (!end_stream) {
        request_data_.add(data);
        return FilterDataStatus::StopIterationAndBuffer;
    }

    std::cout << "here i am, config is at " << config_ << std::endl;
    std::cout << "flowd cluster: " << config_->flowdCluster() << std::endl;// << " and protoconfig at " << &(config_->proto_config()) << std::endl;
    if (!config_->inputParams().empty()) {
        std::cout << "got through it" << std::endl;
        request_headers_->setContentType("application/json");
        std::string raw_input(request_data_.toString());
        request_data_.drain(request_data_.length());

        std::string new_input = process_json_input(raw_input, config_->inputParams());

        request_data_.add(new_input);
        request_headers_->setContentLength(std::to_string(request_data_.length()));
    }

    sendHeaders(false);

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

FilterHeadersStatus BavsFilter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
    return FilterHeadersStatus::Continue;
}

FilterDataStatus BavsFilter::encodeData(Buffer::Instance&, bool) {
    return FilterDataStatus::Continue;
    // intercepts the response data
}

} // namespace Http
} // namespace Envoy
