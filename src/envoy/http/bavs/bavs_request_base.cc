#include "bavs.h"

namespace Envoy {
namespace Http {

BavsRequestBase::BavsRequestBase(BavsFilterConfigSharedPtr config,
                                 std::unique_ptr<Buffer::OwnedImpl> data,
                                 std::unique_ptr<Http::RequestHeaderMap> headers,
                                 std::map<std::string, std::string> saved_headers,
                                 int retries_left,
                                 UpstreamConfigSharedPtr target,
                                 std::string request_type)
                                 :  config_(config),
                                    data_(std::move(data)),
                                    headers_(std::move(headers)),
                                    retries_left_(retries_left),
                                    request_type_(request_type),
                                    saved_headers_(saved_headers),
                                    target_(target) {
    Random::RandomGeneratorImpl rng;
    cm_callback_id_ = rng.uuid();
    config_->clusterManager().storeRequestCallbacks(cm_callback_id_, this);
}

void BavsRequestBase::preprocessHeaders(RequestHeaderMap& headers) const {
    for (const auto& saved_header : saved_headers_) {
        headers.setCopy(Http::LowerCaseString(saved_header.first), saved_header.second);
        std::cout << saved_header.first << " : " << saved_header.second << std::endl;
    }
}

void BavsRequestBase::send() {
    // Find the client
    Http::AsyncClient* client = nullptr;
    bool bombs_away = false;
    try {
        std::string cluster = (request_type_ == REQ_TYPE_INBOUND)
            ? "inbound|" + std::to_string(target_->port()) + "||"
            : target_->cluster();
        client = &(config_->clusterManager().httpAsyncClientForCluster(cluster));
        std::unique_ptr<Http::RequestMessage> msg = getMessage();
        if (msg != nullptr && client != nullptr) {
            preprocessHeaders(msg->headers());
            msg->headers().setContentLength(msg->body().length());
            if (target_->wfTID() != "") {
                msg->headers().setCopy(
                    LowerCaseString(config_->wfTIDHeader()), target_->wfTID()
                );
            }
            client->send(std::move(msg), *this, AsyncClient::RequestOptions());
            bombs_away = true;
        }
    } catch(const EnvoyException&) {
        // The cluster wasn't found, so we need to begin error processing.
        // Implementors of this class must implement the handleConnectionError() method.
        // The method may be a no-op but should only be a no-op if the child class
        // implements the last-ditch error request attempt (i.e. at `flowd:9001/instancefail`)
        handleConnectionError();
    }
    sendShadowRequest(bombs_away);
    if (!bombs_away) {
        // If we never ended up fully sending the request, we're not gonna
        // have a call to the `onSuccess` or the `onFailure` methods.
        // Therefore, we must do some cleanup.
        config_->clusterManager().eraseRequestCallbacks(cm_callback_id_);
    }
}


// This does nothing and one of these objects is sufficient for all requests,
// therefore it's fine to leave static. We only use it for fire-and-forget
// requests; eg. traffic shadow requests.
static NullCallbacks null_callbacks;

void BavsRequestBase::sendShadowRequest(bool original_req_connected) {
    if (config_->shadowUpstream()->fullHostName() == "") return;
    UpstreamConfigSharedPtr shadow_upstream = config_->shadowUpstream();

    std::unique_ptr<Http::RequestMessage> msg = getMessage();

    preprocessHeaders(msg->headers());

    msg->headers().setCopy(
        LowerCaseString(config_->requestFailedHeader()),
        original_req_connected ? "false" : "true"
    );
    msg->headers().setCopy(LowerCaseString(
        config_->requestTypeHeader()), request_type_);
    msg->headers().setCopy(LowerCaseString(
        config_->originalHostHeader()), msg->headers().getHostValue());
    msg->headers().setCopy(LowerCaseString(
        config_->originalHostHeader()), msg->headers().getPathValue());
    msg->headers().setCopy(LowerCaseString(
        config_->originalMethodHeader()), msg->headers().getMethodValue());

    // msg->headers().setHost(shadow_upstream->fullHostname());
    msg->headers().setPath(shadow_upstream->path());
    msg->headers().setMethod(Http::Headers::get().MethodValues.Post);
    msg->headers().setContentLength(msg->body().length());

    Http::AsyncClient* client = NULL;
    try {
        // This call sometimes throws upon failure.
        client = &(config_->clusterManager().httpAsyncClientForCluster(shadow_upstream->cluster()));

        if (client != nullptr) {
            client->send(std::move(msg), null_callbacks, Http::AsyncClient::RequestOptions());
        } else {
            throw EnvoyException("Failed to find the traffic shadow cluster.");
        }
    } catch(const EnvoyException& e) {
        auto instance_iter = saved_headers_.find(config_->wfIIDHeader());
        ASSERT(instance_iter != saved_headers_.end());
        std::string instance_id = instance_iter->second;
        std::cout << "Failed to do traffic shadowing on workflow " << instance_id << std::endl;
    }
}

// Utilities
Http::RequestHeaderMapPtr BavsRequestBase::copyHeaders(Http::RequestOrResponseHeaderMap& headers) {
    Http::RequestHeaderMapPtr new_headers = Http::RequestHeaderMapImpl::create();
    RequestHeaderMap* temp = new_headers.get();
    headers.iterate([temp] (const HeaderEntry& header) -> HeaderMap::Iterate {
        std::string hdr_key(header.key().getStringView());
        temp->setCopy(Http::LowerCaseString(hdr_key), header.value().getStringView());
        return HeaderMap::Iterate::Continue; 
    });
    return new_headers;
}

Http::RequestHeaderMapPtr BavsRequestBase::copyHeaders() {
    return copyHeaders(*headers_);
}

void BavsRequestBase::onSuccess(
        const Http::AsyncClient::Request& request, Http::ResponseMessagePtr&& response) {
    processSuccess(request, response.get());
    config_->clusterManager().eraseRequestCallbacks(cm_callback_id_);
}

void BavsRequestBase::onFailure(
        const Http::AsyncClient::Request& request, Http::AsyncClient::FailureReason reason) {
    processFailure(request, reason);
    config_->clusterManager().eraseRequestCallbacks(cm_callback_id_);
}

} // namespace Http
} // namespace Envoy