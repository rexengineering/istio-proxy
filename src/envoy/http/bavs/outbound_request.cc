#include "bavs.h"

namespace Envoy {
namespace Http{

BavsOutboundRequest::BavsOutboundRequest(Upstream::ClusterManager& cm, std::string target_cluster,
                                         std::string error_cluster, int retries_left,
                                         std::unique_ptr<Http::RequestHeaderMapImpl> headers_to_send,
                                         std::unique_ptr<Buffer::OwnedImpl> data_to_send,
                                         std::string task_id) :
                                         cm_(cm), target_cluster_(target_cluster),
                                         error_cluster_(error_cluster), retries_left_(retries_left),
                                         headers_to_send_(std::move(headers_to_send)),
                                         data_to_send_(std::move(data_to_send)), task_id_(task_id) {
    Random::RandomGeneratorImpl rng;
    cm_callback_id_ = rng.uuid();
    cm_.storeRequestCallbacks(cm_callback_id_, this);
}

void BavsOutboundRequest::send() {
    // First, form the message
    std::unique_ptr<Http::RequestMessageImpl> message = std::make_unique<Http::RequestMessageImpl>();

    RequestHeaderMap* temp = &message->headers();
    headers_to_send_->iterate([temp] (const HeaderEntry& header) -> HeaderMap::Iterate {
        std::string hdr_key(header.key().getStringView());
        temp->setCopy(Http::LowerCaseString(hdr_key), header.value().getStringView());
        return HeaderMap::Iterate::Continue; 
    });

    message->body().add(*data_to_send_);

    // Second, send the message
    Http::AsyncClient* client = NULL;
    try {
        client = &(cm_.httpAsyncClientForCluster(target_cluster_));
    } catch(const EnvoyException&) {
        // The cluster wasn't found, so we need to begin error processing.
        raiseConnectionError();
        return;
    }
    client->send(std::move(message), *this, Http::AsyncClient::RequestOptions());
}

void BavsOutboundRequest::onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) {
    std::string status_str(response->headers().getStatusValue());
    int status = atoi(status_str.c_str());

    if (status < 200 || status >= 300) {
        raiseConnectionError();
    }
}

void BavsOutboundRequest::onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) {
    if (retries_left_ > 0) {
        BavsOutboundRequest *retry_request = new BavsOutboundRequest(
            cm_, target_cluster_, error_cluster_, retries_left_ - 1, std::move(headers_to_send_),
            std::move(data_to_send_), task_id_);
        retry_request->send();
    } else {
        raiseConnectionError();
    }
}

} // namespace Http
} // namespace Envoy