#include "bavs.h"

namespace Envoy{
namespace Http{

BavsErrorRequest::BavsErrorRequest(
            Upstream::ClusterManager& cm, std::string cluster,
            std::unique_ptr<Buffer::OwnedImpl> data_to_send,
            std::unique_ptr<Http::RequestHeaderMapImpl> headers_to_send,
            std::string error_path)
            : cm_(cm), cluster_(cluster), data_to_send_(std::move(data_to_send)),
            headers_to_send_(std::move(headers_to_send)), error_path_(error_path) {
    Random::RandomGeneratorImpl rng;
    cm_callback_id_ = rng.uuid();
    cm_.storeRequestCallbacks(cm_callback_id_, this);
}

void BavsErrorRequest::send() {
    // First, form the message
    headers_to_send_->setPath(error_path_);
    std::unique_ptr<Http::RequestMessageImpl> message = std::make_unique<Http::RequestMessageImpl>(
        std::move(headers_to_send_)
    );

    message->body().add(*data_to_send_);

    // Second, send the message
    Http::AsyncClient* client = NULL;
    try {
        client = &(cm_.httpAsyncClientForCluster(cluster_));
    } catch(const EnvoyException&) {
        std::cout << "Fully out of luck." << std::endl;
        return;
    }
    client->send(std::move(message), *this, Http::AsyncClient::RequestOptions());
}

void BavsErrorRequest::onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) {
    cm_.eraseRequestCallbacks(cm_callback_id_);
}

void BavsErrorRequest::onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) {
    cm_.eraseRequestCallbacks(cm_callback_id_);
}

} // namespace Envoy
} // namespace Http