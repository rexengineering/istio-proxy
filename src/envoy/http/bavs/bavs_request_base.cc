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
                                    retries_left_(retries_left),
                                    request_type_(request_type),
                                    saved_headers_(saved_headers),
                                    target_(target),
                                    headers_(std::move(headers)) {
    Random::RandomGeneratorImpl rng;
    cm_callback_id_ = rng.uuid();
    config_->clusterManager().storeRequestCallbacks(cm_callback_id_, this);
    preprocessHeaders(*headers_);
}

void BavsRequestBase::preprocessHeaders(RequestHeaderMap& headers) const {
    for (const auto& saved_header : saved_headers_) {
        headers.setCopy(Http::LowerCaseString(saved_header.first), saved_header.second);
    }
    std::string authority = target_->fullHostName();
    headers.setCopy(LowerCaseString(":authority"), authority);
    headers.setHost(authority);
    headers.setMethod(target_->method());
    headers.setPath(target_->path());
}

void BavsRequestBase::send() {
    // Find the client
    Http::AsyncClient* client = nullptr;
    bool bombs_away = false;
    try {
        std::string cluster;
        if (request_type_ == REQ_TYPE_INBOUND) {
            cluster = "inbound|" + std::to_string(target_->port()) + "||";
        } else {
            cluster = target_->cluster();
        }

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

            BavsFilter::bavslog("about to send these headers:");
            msg->headers().iterate([](const HeaderEntry& header) -> HeaderMap::Iterate{
                std::string msg(header.key().getStringView());
                msg += ": ";
                msg += std::string(header.value().getStringView());
                BavsFilter::bavslog(msg);
                return HeaderMap::Iterate::Continue;
            });

            std::string logMessage(
                "Sending " + request_type_ + " request:\n" + msg->body().toString()
            );
           BavsFilter::bavslog(logMessage);
            client->send(std::move(msg), *this, AsyncClient::RequestOptions());
            bombs_away = true;
        }
    } catch(const EnvoyException&) {
        // The cluster wasn't found, so we need to begin error processing.
        // Implementors of this class must implement the handleConnectionError() method.
        // The method may be a no-op but should only be a no-op if the child class
        // implements the last-ditch error request attempt (i.e. at `flowd:9001/instancefail`)
       BavsFilter::bavslog("Caught connection error.");
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
    if (!msg) {
        BavsFilter::bavslog(
            "Failed to shadow traffic on request; likely because of failed context parsing");
        return;
    }

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
    auto headers = copyHeaders(*headers_);
    preprocessHeaders(*headers);
    return headers;
}

void BavsRequestBase::onSuccess(
        const Http::AsyncClient::Request& request, Http::ResponseMessagePtr&& response) {
    auto status = response->headers().getStatusValue();
    std::string logMessage = "BAVS Got response: " + std::string(status) +
        "\n" + response->body().toString();
   BavsFilter::bavslog(logMessage);
    processSuccess(request, response.get());
    config_->clusterManager().eraseRequestCallbacks(cm_callback_id_);
}

void BavsRequestBase::onFailure(
        const Http::AsyncClient::Request& request, Http::AsyncClient::FailureReason reason) {
   BavsFilter::bavslog("BAVS: Request stream broken before request could be sent.");
    processFailure(request, reason);
    config_->clusterManager().eraseRequestCallbacks(cm_callback_id_);
}

} // namespace Http
} // namespace Envoy
