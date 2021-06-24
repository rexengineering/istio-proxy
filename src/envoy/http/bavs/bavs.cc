#include "bavs.h"

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"


namespace Envoy {
namespace Http {

BavsFilterConfig::BavsFilterConfig(const bavs::BAVSFilter& proto_config, Upstream::ClusterManager& cm)
    : cluster_manager_(cm) {
    forward_upstreams_.reserve(proto_config.forward_upstreams_size());
    for (auto iter=proto_config.forward_upstreams().begin();
         iter != proto_config.forward_upstreams().end();
         iter++) {
        UpstreamConfigSharedPtr forwardee(
            std::make_shared<UpstreamConfig>(UpstreamConfig(*iter))
        );
        forward_upstreams_.push_back(forwardee);
    }
    for (auto iter=proto_config.error_gateway_upstreams().begin();
         iter != proto_config.error_gateway_upstreams().end();
         iter++) {
        UpstreamConfigSharedPtr forwardee(
            std::make_shared<UpstreamConfig>(UpstreamConfig(*iter))
        );
        error_gateway_upstreams_.push_back(forwardee);
    }
    wf_did_header_ = proto_config.wf_did_header();
    wf_iid_header_ = proto_config.wf_iid_header();
    wf_tid_header_ = proto_config.wf_tid_header();
    for (const std::string& header : proto_config.headers_to_forward()) {
        headers_to_forward_.push_back(header);
    }
    std::set<std::string> default_forward_headers({
        SPAN_ID_HEADER, TRACE_ID_HEADER, REQUEST_ID_HEADER, wf_did_header_,
        wf_iid_header_, wf_tid_header_
    });

    for (const auto& hdr: default_forward_headers) {
        headers_to_forward_.push_back(hdr);
    }
    for (auto param : proto_config.input_params()) {
        input_params_.push_back(param);
    }
    for (auto param : proto_config.output_params()) {
        output_params_.push_back(param);
    }
    wf_did_ = proto_config.wf_did();

    is_closure_transport_ = proto_config.closure_transport();
    inbound_upstream_ = std::make_shared<UpstreamConfig>(UpstreamConfig(proto_config.inbound_upstream()));
    flowd_upstream_ = std::make_shared<UpstreamConfig>(UpstreamConfig(proto_config.flowd_upstream()));
    shadow_upstream_ = std::make_shared<UpstreamConfig>(UpstreamConfig(proto_config.shadow_upstream()));
}

void BavsFilter::sendMessage() {
    std::string temp = saved_headers_[SPAN_ID_HEADER];

    // tell the original client who sent a request to this Envoy that we got you buddy
    decoder_callbacks_->sendLocalReply(
        Envoy::Http::Code::Accepted,
        "For my ally is the Force, and a powerful ally it is.",
        [temp] (ResponseHeaderMap& headers) -> void {
            headers.setCopy(Http::LowerCaseString(SPAN_ID_HEADER), temp);
        },
        absl::nullopt,
        ""
    );
    BavsInboundRequest* inbound_request = new BavsInboundRequest(
            config_,
            std::move(original_inbound_data_),
            std::move(inbound_headers_),
            saved_headers_,
            config_->inboundUpstream()->totalAttempts() - 1,
            config_->inboundUpstream(),
            REQ_TYPE_INBOUND);
    inbound_request->send();
}

FilterHeadersStatus BavsFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
    const Http::HeaderEntry* task_entry = headers.get(
        Http::LowerCaseString(config_->wfTIDHeader())
    );
    ENVOY_LOG(warn, std::string(headers.getPathValue()));
    if (task_entry == NULL) {
        ENVOY_LOG(warn, "BAVS ignoring request: NO task id header.");
        return FilterHeadersStatus::Continue;
    } else if (task_entry->value() == NULL) {
        ENVOY_LOG(warn, "BAVS ignoring request: Task Id header is NULL");
        return FilterHeadersStatus::Continue;
    } else if (task_entry->value().getStringView() != config_->inboundUpstream()->wfTID()) {
        std::string wrong_tid_msg = "BAVS ignoring request: incorrect task id: ";
        wrong_tid_msg += std::string(task_entry->value().getStringView());
        wrong_tid_msg += " != " + config_->inboundUpstream()->wfTID();
        ENVOY_LOG(warn, wrong_tid_msg);
        return FilterHeadersStatus::Continue;
    }

    const Http::HeaderEntry* wf_id_entry = headers.get(
        Http::LowerCaseString(config_->wfDIDHeader())
    );
    if (wf_id_entry == NULL || wf_id_entry->value() == NULL ||
            (wf_id_entry->value().getStringView() != config_->wfDID())) {
        ENVOY_LOG(warn, "BAVS ignoring request: missing or wrong x-rexflow-did header.");
        return FilterHeadersStatus::Continue;
    }

    const Http::HeaderEntry* instance_entry = headers.get(
        Http::LowerCaseString(config_->wfIIDHeader())
    );
    if ((instance_entry == NULL) || (instance_entry->value() == NULL)) {
        ENVOY_LOG(warn, "BAVS ignoring request: no x-rexflow-iid header.");
        return FilterHeadersStatus::Continue;
    }
    saved_headers_[config_->wfIIDHeader()] = instance_entry->value().getStringView();
    std::string instance_id(instance_entry->value().getStringView());
    ENVOY_LOG(warn, "BAVS commencing processing for " + instance_id);

    auto& active_span = decoder_callbacks_->activeSpan();
    active_span.injectContext(headers);

    RequestHeaderMapImpl* inbound_hdrs_tmp = inbound_headers_.get();
    std::map<std::string, std::string> *saved_hdrs_tmp = &saved_headers_;
    const std::vector<std::string> *forward_hdrs_tmp = &config_->headersToForward();
    headers.iterate([inbound_hdrs_tmp, saved_hdrs_tmp, forward_hdrs_tmp](const HeaderEntry& header)
            -> HeaderMap::Iterate {
            std::string key_string(header.key().getStringView());
            inbound_hdrs_tmp->setCopy(
                Http::LowerCaseString(key_string), header.value().getStringView()
            );
            if (std::find(forward_hdrs_tmp->begin(), forward_hdrs_tmp->end(), key_string)
                != forward_hdrs_tmp->end())
            {
                (*saved_hdrs_tmp)[key_string] = header.value().getStringView();
            }
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

    sendMessage();
    return FilterDataStatus::StopIterationAndBuffer;
}

FilterHeadersStatus BavsFilter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
    return FilterHeadersStatus::Continue;
}

FilterDataStatus BavsFilter::encodeData(Buffer::Instance&, bool) {
    return FilterDataStatus::Continue;
}

void BavsFilter::bavslog(std::string msg) {
    ENVOY_LOG(warn, msg);
}

} // namespace Http
} // namespace Envoy
