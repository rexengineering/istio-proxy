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

std::string createErrorMessage(std::string error_code, std::string error_msg,
                               Buffer::OwnedImpl& input_data, Http::RequestHeaderMap& input_headers) {
    std::map<std::string, std::string> elements;
    elements["error_code"] = jstringify(error_code);
    elements["error_msg"] = jstringify(error_msg);
    elements["input_data_encoded"] = jstringify(Base64::encode(input_data, input_data.length()));

    std::string dumped_hdrs = dumpHeaders(input_headers);
    elements["input_headers_encoded"] = jstringify(Base64::encode(dumped_hdrs.c_str(), dumped_hdrs.size()));
    return create_json_string(elements);
}

std::string createErrorMessage(std::string error_code, std::string error_msg,
                               Buffer::OwnedImpl& input_data, Http::RequestHeaderMap& input_headers,
                               Http::ResponseMessage& response) {
    std::map<std::string, std::string> elements;
    elements["error_code"] = jstringify(error_code);
    elements["error_msg"] = jstringify(error_msg);
    elements["input_data_encoded"] = jstringify(Base64::encode(input_data, input_data.length()));
    elements["output_data_encoded"] = jstringify(Base64::encode(response.body(), response.body().length()));

    std::string dumped_hdrs = dumpHeaders(input_headers);
    elements["input_headers_encoded"] = jstringify(Base64::encode(dumped_hdrs.c_str(), dumped_hdrs.size()));

    std::string dumped_output_hdrs = dumpHeaders(response.headers());
    elements["output_headers_encoded"] = jstringify(
        Base64::encode(dumped_output_hdrs.c_str(), dumped_output_hdrs.size()));
    return create_json_string(elements);
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
    is_closure_transport_ = proto_config.closure_transport();
    upstream_port_ = proto_config.upstream_port();
    inbound_retries_ = proto_config.inbound_retries();
    for (const std::string& header : proto_config.headers_to_forward()) {
        headers_to_forward_.push_back(header);
    }
    for (auto param : proto_config.input_params()) {
        input_params_.push_back(param);
    }
    for (auto param : proto_config.output_params()) {
        output_params_.push_back(param);
    }
}

void BavsFilter::raiseContextInputError(std::string msg) {
    std::string error_data = createErrorMessage(CONTEXT_INPUT_PARSING_ERROR, msg, 
                                                *original_inbound_data_,
                                                *inbound_headers_);
    std::unique_ptr<Buffer::OwnedImpl> buf = std::make_unique<Buffer::OwnedImpl>();
    buf->add(error_data);
    BavsErrorRequest* error_req = new BavsErrorRequest(
                                cluster_manager_, config_->flowdCluster(), std::move(buf),
                                std::move(inbound_headers_), config_->flowdPath());
    error_req->send();
}

void BavsFilter::sendMessage() {
    std::string temp = spanid_;
    decoder_callbacks_->sendLocalReply(
        Envoy::Http::Code::Accepted,
        "For my ally is the Force, and a powerful ally it is.",
        [temp] (ResponseHeaderMap& headers) -> void {
            headers.setCopy(Http::LowerCaseString("x-b3-traceid"), temp);
        },
        absl::nullopt,
        ""
    );
    BavsInboundRequest* inbound_request = new BavsInboundRequest(
            config_, cluster_manager_, std::move(inbound_headers_),
            std::move(original_inbound_data_),
            std::move(inbound_data_to_send_), config_->inboundRetries(),
            spanid_, instance_id_, saved_headers_,
            inbound_data_is_json_, service_cluster_);
    inbound_request->send();
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

    RequestHeaderMapImpl* temp = inbound_headers_.get();
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

    auto& active_span = decoder_callbacks_->activeSpan();
    active_span.injectContext(*(inbound_headers_.get()));

    auto *span_entry = inbound_headers_->get(Http::LowerCaseString("x-b3-traceid"));
    if (span_entry) {
        spanid_ = span_entry->value().getStringView();
    }

    is_workflow_ = true;

    if (end_stream) sendMessage();

    return FilterHeadersStatus::StopIteration;
}

FilterDataStatus BavsFilter::decodeData(Buffer::Instance& data, bool end_stream) {
    if (!is_workflow_) return FilterDataStatus::Continue;

    original_inbound_data_->add(data);
    if (!end_stream) {
        return FilterDataStatus::StopIterationAndBuffer;
    }

    if (config_->isClosureTransport()) {
        if (inbound_headers_->getContentTypeValue() != "application/json") {
            raiseContextInputError("Expected to get JSON input because was expecting a context.");
            return FilterDataStatus::StopIterationAndBuffer;
        }

        std::string raw_input(original_inbound_data_->toString());

        std::string new_input = "{}";
        try {
            Json::ObjectSharedPtr json = Json::Factory::loadFromString(raw_input);
            if (!json->isObject()) {
                if (!config_->inputParams().empty()) {
                    throw EnvoyException("Incoming data not an object, yet we needed to unmarshal.");
                }
                inbound_data_to_send_->add(*original_inbound_data_);
            } else {
                new_input = build_json_from_params(json, config_->inputParams());
                inbound_data_to_send_->add(new_input);
                inbound_headers_->setContentLength(inbound_data_to_send_->length());
                inbound_headers_->setContentType("application/json");
                inbound_data_is_json_ = true;
            }
        } catch(const EnvoyException& exn) {
            raiseContextInputError("Failed to build inbound request from context parameters.");
            return FilterDataStatus::StopIterationAndBuffer;
        } // try/catch()

    } /* if (config_->isClosureTransport()) */ else { 
        inbound_data_to_send_->add(*original_inbound_data_);
    }

    sendMessage();
    return FilterDataStatus::StopIterationAndBuffer;
}

FilterHeadersStatus BavsFilter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
    return FilterHeadersStatus::Continue;
}

FilterDataStatus BavsFilter::encodeData(Buffer::Instance&, bool) {
    return FilterDataStatus::Continue;
}

} // namespace Http
} // namespace Envoy
