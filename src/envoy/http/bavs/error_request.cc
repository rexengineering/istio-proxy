#include "bavs.h"

namespace Envoy{
namespace Http{

std::unique_ptr<RequestMessage> BavsErrorRequest::getMessage() {
    // First, form the message
    std::unique_ptr<Http::RequestMessageImpl> message = std::make_unique<Http::RequestMessageImpl>();

    RequestHeaderMap* temp = &message->headers();
    copyHeaders()->iterate([temp] (const HeaderEntry& header) -> HeaderMap::Iterate {
        std::string hdr_key(header.key().getStringView());
        temp->setCopy(Http::LowerCaseString(hdr_key), header.value().getStringView());
        return HeaderMap::Iterate::Continue; 
    });
    message->headers().setPath(target_->path());
    message->body().add(*getData());
    return message;
}

void BavsErrorRequest::processSuccess(const AsyncClient::Request&, ResponseMessage*) {}

void BavsErrorRequest::processFailure(const AsyncClient::Request&, AsyncClient::FailureReason) {}

void BavsErrorRequest::handleConnectionError() {}

} // namespace Envoy
} // namespace Http