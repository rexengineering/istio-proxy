#include "bavs.h"

namespace Envoy{
namespace Http{

BavsTrafficShadowRequest::BavsTrafficShadowRequest(
            Upstream::ClusterManager& cm, std::string cluster,
            Buffer::Instance& data_to_send,
            Http::RequestHeaderMapImpl& headers_to_send,
            std::string path, std::string request_type)
            : cm_(cm), cluster_(cluster), data_to_send_(new Buffer::OwnedImpl),
            headers_to_send_(Http::RequestHeaderMapImpl::create()), path_(path),
            request_type_(request_type) {

    Random::RandomGeneratorImpl rng;
    cm_callback_id_ = rng.uuid();
    cm_.storeRequestCallbacks(cm_callback_id_, this);

    Http::RequestHeaderMapImpl* temp = headers_to_send_.get();
    headers_to_send.iterate([temp] (const HeaderEntry& header) -> HeaderMap::Iterate {
        std::string hdr_key(header.key().getStringView());
        temp->setCopy(Http::LowerCaseString(hdr_key), header.value().getStringView());
        return HeaderMap::Iterate::Continue;
    });

    data_to_send_->add(data_to_send);
}

void BavsTrafficShadowRequest::send() {
    if (cluster_ == "") {
        return;
    }
    // First, form the message
    headers_to_send_->setCopy(
        Http::LowerCaseString("x-rexflow-original-path"),
        headers_to_send_->getPathValue()
    );
    headers_to_send_->setCopy(
        Http::LowerCaseString("x-rexflow-original-host"),
        headers_to_send_->getHostValue()
    );
    headers_to_send_->setCopy(
        Http::LowerCaseString("x-rexflow-original-method"),
        headers_to_send_->getMethodValue()
    );
    headers_to_send_->setCopy(
        Http::LowerCaseString(REQ_TYPE_HEADER), request_type_
    );

    headers_to_send_->setHost("kafka-shadow.rexflow:5000");
    headers_to_send_->setPath(path_);
    headers_to_send_->setContentLength(data_to_send_->length());
    headers_to_send_->setMethod(Http::Headers::get().MethodValues.Post);

    std::unique_ptr<Http::RequestMessageImpl> message = std::make_unique<Http::RequestMessageImpl>(
        std::move(headers_to_send_)
    );

    message->body().add(*data_to_send_);

    // Second, send the message
    Http::AsyncClient* client = NULL;
    try {
        client = &(cm_.httpAsyncClientForCluster(cluster_));
        client->send(std::move(message), *this, Http::AsyncClient::RequestOptions());
    } catch(const EnvoyException& e) {
        // cleanup since the onFailure or onSuccess methods won't be called
        // Normally, an exception here means that we couldn't find the traffic shadow
        // cluster.
        cm_.eraseRequestCallbacks(cm_callback_id_);
    }
}

void BavsTrafficShadowRequest::onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) {
    cm_.eraseRequestCallbacks(cm_callback_id_);
}

void BavsTrafficShadowRequest::onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) {
    cm_.eraseRequestCallbacks(cm_callback_id_);
}

} // namespace Envoy
} // namespace Http