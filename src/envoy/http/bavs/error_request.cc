#include "bavs.h"

namespace Envoy{
namespace Http{

BavsErrorRequest::BavsErrorRequest(
            Upstream::ClusterManager& cm, std::string cluster,
            std::unique_ptr<Buffer::OwnedImpl> data_to_send,
            std::unique_ptr<Http::RequestHeaderMapImpl> headers_to_send,
            std::string error_path, std::string shadow_cluster, std::string shadow_path)
            : cm_(cm), cluster_(cluster), data_to_send_(std::move(data_to_send)),
            headers_to_send_(std::move(headers_to_send)), error_path_(error_path),
            shadow_cluster_(shadow_cluster), shadow_path_(shadow_path) {
    Random::RandomGeneratorImpl rng;
    cm_callback_id_ = rng.uuid();
    cm_.storeRequestCallbacks(cm_callback_id_, this);
}

void BavsErrorRequest::send() {
    // First, form the message
    headers_to_send_->setPath(error_path_);
    headers_to_send_->setContentLength(data_to_send_->length());

    std::unique_ptr<Http::RequestHeaderMapImpl> shadow_hdrs = Http::RequestHeaderMapImpl::create();
    RequestHeaderMap* shadow_temp = &(*shadow_hdrs);
    headers_to_send_->iterate([shadow_temp] (const HeaderEntry& header) -> HeaderMap::Iterate {
        std::string hdr_key(header.key().getStringView());
        shadow_temp->setCopy(Http::LowerCaseString(hdr_key), header.value().getStringView());
        return HeaderMap::Iterate::Continue;
    });

    std::unique_ptr<Http::RequestMessageImpl> message = std::make_unique<Http::RequestMessageImpl>(
        std::move(headers_to_send_)
    );

    message->body().add(*data_to_send_);

    // Second, send the message
    Http::AsyncClient* client = NULL;

    try {
        client = &(cm_.httpAsyncClientForCluster(cluster_));
        client->send(std::move(message), *this, Http::AsyncClient::RequestOptions());
    } catch(const EnvoyException&) {
        std::cout << "Fully out of luck." << std::endl;
        shadow_hdrs->setCopy(Http::LowerCaseString(ORIGINAL_REQ_FAILED_HDR), "true");
    }

    if (shadow_cluster_ != "") {
        BavsTrafficShadowRequest *shadow_req = new BavsTrafficShadowRequest(
            cm_, shadow_cluster_, *data_to_send_,
            *shadow_hdrs, shadow_path_, REQ_TYPE_ERROR
        );
        shadow_req->send();
    }
}

void BavsErrorRequest::onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) {
    cm_.eraseRequestCallbacks(cm_callback_id_);
}

void BavsErrorRequest::onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) {
    cm_.eraseRequestCallbacks(cm_callback_id_);
}

} // namespace Envoy
} // namespace Http