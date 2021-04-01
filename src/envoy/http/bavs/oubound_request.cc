#include "bavs.h"

namespace Envoy {
namespace Http {

void BavsOutboundCallbacks::onHeaders(Http::ResponseHeaderMapPtr&& headers, bool) {
    // TODO: Allow the user to configure a list of "retriable" headers.
    std::string status_str(headers->getStatusValue());
    int status = atoi(status_str.c_str());
    retriable_failure_ = false;
    unretriable_failure_ = false;

    // Retry on all 5xx's
    if (status >= 500) {
        retriable_failure_ = true;
    } else if (!retriable_failure_ && (status < 200 || status >= 300)) {
        unretriable_failure_ = true;
    }

    // If we have an unretriable failure OR we run out of retry attempts,
    // we must redirect to notify Flowd of our failure.
    if (unretriable_failure_ || attempts_left_ == 1) {
        // We're sending back to Flowd rather than to the initial destination, so we
        // need to tell Flowd what the original host+path headers were.
        headers_->setCopy(Http::LowerCaseString("x-rexflow-original-path"), headers_->getPathValue());
        headers_->setCopy(Http::LowerCaseString("x-rexflow-original-host"), headers_->getHostValue());
        headers_->setPath(fail_cluster_path_);
        cluster_ = fail_cluster_;
    }
}

void BavsOutboundCallbacks::onComplete() {
    if ((unretriable_failure_ || retriable_failure_) && attempts_left_) {
        doRetry(headers_only_);
    }
    // Finally, remove ourself from the clusterManager
    cluster_manager_->eraseCallbacksAndHeaders(id_);
}

Http::RequestHeaderMapImpl& BavsOutboundCallbacks::requestHeaderMap() {
    return *(headers_.get());
}

void BavsOutboundCallbacks::setStream(Http::AsyncClient::Stream* stream) {
    request_stream_ = stream;
}

Http::AsyncClient::Stream* BavsOutboundCallbacks::getStream() {
    return request_stream_;
}

void BavsOutboundCallbacks::addData(Buffer::Instance& data) {
    buffer_->add(data);
}

void BavsOutboundCallbacks::doRetry(bool end_stream) {
    Http::AsyncClient* client = nullptr;
    try {
        client = &(cluster_manager_->httpAsyncClientForCluster(cluster_));
    } catch(const EnvoyException&) {
        std::cout << "Couldn't find the cluster " << cluster_ << std::endl;
    }
    if (client == nullptr) return;

    Random::RandomGeneratorImpl rng;
    std::string new_id = rng.uuid();
    BavsOutboundCallbacks *cb = new BavsOutboundCallbacks(new_id, std::move(headers_),
            *cluster_manager_, attempts_left_ - 1, fail_cluster_, cluster_,
            fail_cluster_path_, config_);
    cb->addData(*buffer_);

    auto stream = client->start(*cb, AsyncClient::StreamOptions());
    cb->setStream(stream);
    if (cb->getStream()) {
        cb->getStream()->sendHeaders(cb->requestHeaderMap(), end_stream);
    }
    if (!end_stream) {
        // After sending, the connection may have been auto-closed if the service is down.
        // Therefore, we have to re-check to make sure that the `cb` is still valid and its
        // stream is also still valid.
        cb = static_cast<BavsOutboundCallbacks*>(cluster_manager_->getCallbacksAndHeaders(new_id));
        if (cb && cb->getStream()) {
            cb->getStream()->sendData(*buffer_, true);

            // Once again, there's a chance that it was terminated. Check for closure again.
            cb = static_cast<BavsOutboundCallbacks*>(cluster_manager_->getCallbacksAndHeaders(new_id));
            Buffer::OwnedImpl empty_buffer;
            if (cb && cb->getStream()) cb->getStream()->sendData(empty_buffer, true);
        }
    }
}



} // namespace Envoy
} // namespace Http


class BavsOutboundCallbacks : public Envoy::Upstream::AsyncStreamCallbacksAndHeaders {
public:
    ~BavsOutboundCallbacks() = default;
    BavsOutboundCallbacks() {};
    BavsOutboundCallbacks(std::string id,
                        std::unique_ptr<Http::RequestHeaderMapImpl> headers,
                        Upstream::ClusterManager& cm, int num_retries,
                        std::string fail_cluster, std::string cluster,
                        std::string fail_cluster_path, UpstreamConfigSharedPtr config)
        : id_(id), cluster_manager_(&cm), attempts_left_(num_retries),
          fail_cluster_(fail_cluster), cluster_(cluster), headers_(std::move(headers)), buffer_(new Buffer::OwnedImpl),
          headers_only_(false), fail_cluster_path_(fail_cluster_path), config_(config) {
        cluster_manager_->storeCallbacksAndHeaders(id, this);
    }

    void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool) override;
    void onComplete() override;
    void onData(Buffer::Instance&, bool) override {}

    void onTrailers(Http::ResponseTrailerMapPtr&&) override {}
    void onReset() override {}
    Http::RequestHeaderMapImpl& requestHeaderMap() override;
    void setStream(Http::AsyncClient::Stream* stream) override;
    Http::AsyncClient::Stream* getStream() override;
    void addData(Buffer::Instance& data);

private:

    void createAndSendErrorMessage(std::string msg);
    // void doRetry(bool end_stream);
    std::string id_;
    Upstream::ClusterManager* cluster_manager_;
    int attempts_left_;
    std::string fail_cluster_;
    std::string cluster_;
    std::unique_ptr<Http::RequestHeaderMapImpl> headers_;
    std::unique_ptr<Buffer::OwnedImpl> buffer_;
    bool headers_only_ = false;
    std::string fail_cluster_path_;
    bool unretriable_failure_ = false;
    bool retriable_failure_ = false;
    Http::AsyncClient::Stream* request_stream_;
    UpstreamConfigSharedPtr config_;
};
