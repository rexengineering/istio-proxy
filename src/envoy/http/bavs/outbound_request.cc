#include "bavs.h"

namespace Envoy {
namespace Http{

void BavsOutboundRequest::handleConnectionError() {
    std::string error_message = "Failed connecting to next step of workflow on outbound edge.";
    std::string error_data = BavsUtil::createErrorMessage(
        CONNECTION_ERROR, error_message, *getData(), *getHeaders()
    );

    std::unique_ptr<Buffer::OwnedImpl> request_data = std::make_unique<Buffer::OwnedImpl>();
    request_data->add(error_data);
    RequestHeaderMapPtr request_headers = copyHeaders();

    BavsErrorRequest* error_req = new BavsErrorRequest(
        config_, std::move(request_data), std::move(request_headers), saved_headers_, 0,
        config_->flowdUpstream(), REQ_TYPE_ERROR
    );
    error_req->send();
}

std::unique_ptr<RequestMessage> BavsOutboundRequest::getMessage() {
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

void BavsOutboundRequest::processSuccess(const Http::AsyncClient::Request&, ResponseMessage* response) {
    std::string status_str(response->headers().getStatusValue());
    int status = atoi(status_str.c_str());

    if (status < 200 || status >= 300) {
        handleConnectionError();
    }
}

void BavsOutboundRequest::processFailure(const AsyncClient::Request&, AsyncClient::FailureReason) {
    if (retries_left_ > 0) {
        std::unique_ptr<Buffer::OwnedImpl> request_data = std::make_unique<Buffer::OwnedImpl>();
        request_data->add(*getData());
        RequestHeaderMapPtr request_headers = copyHeaders();

        BavsOutboundRequest *retry_request = new BavsOutboundRequest(
            config_, std::move(request_data), std::move(request_headers), saved_headers_,
            retries_left_ - 1, target_, REQ_TYPE_OUTBOUND
        );
        retry_request->send();
    } else {
        handleConnectionError();
    }
}

} // namespace Http
} // namespace Envoy