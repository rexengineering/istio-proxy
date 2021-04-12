#include "bavs.h"

namespace Envoy{
namespace Http{

BavsTaskErrorRequest::BavsTaskErrorRequest(
            Upstream::ClusterManager& cm, std::string cluster,
            std::unique_ptr<Buffer::OwnedImpl> data_to_send,
            std::unique_ptr<Http::RequestHeaderMapImpl> headers_to_send,
            BavsFilterConfigSharedPtr config, UpstreamConfigSharedPtr upstream)
            : cm_(cm), cluster_(cluster), data_to_send_(std::move(data_to_send)),
            headers_to_send_(std::move(headers_to_send)), config_(config),
            upstream_(upstream) {
    Random::RandomGeneratorImpl rng;
    cm_callback_id_ = rng.uuid();
    cm_.storeRequestCallbacks(cm_callback_id_, this);
}

void BavsTaskErrorRequest::send() {
    // First, form the message
    std::unique_ptr<Http::RequestHeaderMapImpl> temp_hdrs = Http::RequestHeaderMapImpl::create();
    RequestHeaderMap* temp = &(*temp_hdrs);
    headers_to_send_->iterate([temp] (const HeaderEntry& header) -> HeaderMap::Iterate {
        std::string hdr_key(header.key().getStringView());
        temp->setCopy(Http::LowerCaseString(hdr_key), header.value().getStringView());
        return HeaderMap::Iterate::Continue; 
    });

    temp_hdrs->setContentLength(data_to_send_->length());
    temp_hdrs->setPath(upstream_->path());

    std::unique_ptr<Http::RequestMessageImpl> message = std::make_unique<Http::RequestMessageImpl>(
        std::move(temp_hdrs)
    );

    message->body().add(*data_to_send_);

    // Second, send the message
    Http::AsyncClient* client = nullptr;
    try {
        client = &(cm_.httpAsyncClientForCluster(cluster_));
    } catch(const EnvoyException&) {
        std::cout << "Fully out of luck." << std::endl;
        return;
    }
    if (client) {
        client->send(std::move(message), *this, Http::AsyncClient::RequestOptions());
    }
}

void BavsTaskErrorRequest::onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) {
    BavsErrorRequest* error_req = new BavsErrorRequest(cm_, config_->flowdCluster(), 
                                                       std::move(data_to_send_), std::move(headers_to_send_),
                                                       config_->flowdPath());
    error_req->send();
    cm_.eraseRequestCallbacks(cm_callback_id_);
}

void BavsTaskErrorRequest::onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) {
    cm_.eraseRequestCallbacks(cm_callback_id_);
}

} // namespace Envoy
} // namespace Http