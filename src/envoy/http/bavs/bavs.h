#pragma once
#include <iostream>
#include <stdio.h>

#include "envoy/http/filter.h"
#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"
#include "envoy/http/async_client.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"
#include "common/common/random_generator.h"

#include "src/envoy/http/bavs/bavs.pb.h"

namespace Envoy {
namespace Http {


class BavsRetriableCallbacks : public Envoy::Upstream::AsyncStreamCallbacksAndHeaders {
public:
    ~BavsRetriableCallbacks() = default;
    BavsRetriableCallbacks() {};
    BavsRetriableCallbacks(std::string id, std::unique_ptr<Http::RequestHeaderMapImpl> headers,
                        Upstream::ClusterManager& cm, int num_retries, std::string fail_cluster, std::string cluster,
                        std::string fail_cluster_path)
        : id_(id), cluster_manager_(&cm), attempts_left_(num_retries),
          fail_cluster_(fail_cluster), cluster_(cluster), headers_(std::move(headers)), buffer_(new Buffer::OwnedImpl),
          headers_only_(false), fail_cluster_path_(fail_cluster_path) {
        cluster_manager_->storeCallbacksAndHeaders(id, this);
    }

    void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool) override {
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

    void onData(Buffer::Instance&, bool) override {}
    void onTrailers(Http::ResponseTrailerMapPtr&&) override {}
    void onReset() override {}
    void onComplete() override {
        if ((unretriable_failure_ || retriable_failure_) && attempts_left_) {
            doRetry(headers_only_);
        }
        // Finally, remove ourself from the clusterManager
        cluster_manager_->eraseCallbacksAndHeaders(id_);
    }
    Http::RequestHeaderMapImpl& requestHeaderMap() override {
        return *(headers_.get());
    }

    void setStream(Http::AsyncClient::Stream* stream) override { request_stream_ = stream;}
    Http::AsyncClient::Stream* getStream() override { return request_stream_; }

    void addData(Buffer::Instance& data) {
        buffer_->add(data);
    }

private:

    void doRetry(bool end_stream) {
        Http::AsyncClient* client = nullptr;
        try {
            client = &(cluster_manager_->httpAsyncClientForCluster(cluster_));
        } catch(const EnvoyException&) {
            std::cout << "Couldn't find the cluster " << cluster_ << std::endl;
        }
        if (client == nullptr) return;

        Random::RandomGeneratorImpl rng;
        std::string new_id = rng.uuid();
        BavsRetriableCallbacks *cb = new BavsRetriableCallbacks(new_id, std::move(headers_),
                *cluster_manager_, attempts_left_ - 1, fail_cluster_, cluster_, fail_cluster_path_);
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
            cb = static_cast<BavsRetriableCallbacks*>(cluster_manager_->getCallbacksAndHeaders(new_id));
            if (cb && cb->getStream()) {
                cb->getStream()->sendData(*buffer_, true);

                // Once again, there's a chance that it was terminated. Check for closure again.
                cb = static_cast<BavsRetriableCallbacks*>(cluster_manager_->getCallbacksAndHeaders(new_id));
                Buffer::OwnedImpl empty_buffer;
                if (cb && cb->getStream()) cb->getStream()->sendData(empty_buffer, true);
            }
        }
    }

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
};


class UpstreamConfig {
public:
    UpstreamConfig() {}
    UpstreamConfig(const bavs::Upstream& proto_config) :
        full_hostname_(proto_config.full_hostname()), port_(proto_config.port()),
        path_(proto_config.path()), method_(proto_config.method()), total_attempts_(proto_config.total_attempts()),
        task_id_(proto_config.task_id()) {}

    inline const std::string& full_hostname() const { return full_hostname_; }
    inline int port() const { return port_; }
    inline const std::string& path() const { return path_; }
    inline const std::string& method() const { return method_; }
    inline int totalAttempts() const { return total_attempts_; }
    inline const std::string& taskId() const { return task_id_; }

private:
    std::string full_hostname_;
    int port_;
    std::string path_;
    std::string method_;
    int total_attempts_;
    std::string task_id_;
};

using UpstreamConfigSharedPtr = std::shared_ptr<UpstreamConfig>;

class BavsFilterConfig {
public:
    virtual ~BavsFilterConfig() {}
    BavsFilterConfig() {}
    BavsFilterConfig(const bavs::BAVSFilter& proto_config);

    virtual const std::string& wfIdValue() { return wf_id_; }
    virtual const std::string& flowdCluster() { return flowd_cluster_; }
    virtual const std::string& flowdPath() { return flowd_path_; }
    virtual const std::string& taskId() { return task_id_; }
    virtual const std::string& trafficShadowCluster() { return traffic_shadow_cluster_; }
    virtual const std::string& trafficShadowPath() { return traffic_shadow_path_; }


    virtual const std::vector<const UpstreamConfigSharedPtr>& forwards() { return forwards_; }

private:
    std::vector<const UpstreamConfigSharedPtr> forwards_;
    std::string wf_id_;
    std::string flowd_cluster_;
    std::string flowd_path_;
    std::string task_id_;
    std::string traffic_shadow_cluster_;
    std::string traffic_shadow_path_;
};

using BavsFilterConfigSharedPtr = std::shared_ptr<BavsFilterConfig>;

class BavsFilter : public PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
private:
    void sendShadowHeaders(Http::RequestHeaderMapImpl& original_headers);
    const BavsFilterConfigSharedPtr config_;
    Upstream::ClusterManager& cluster_manager_;
    bool is_workflow_;
    bool successful_response_;
    std::vector<std::string> req_cb_keys;
    std::string flow_id_;
    std::string wf_template_id_;

public:
    BavsFilter(BavsFilterConfigSharedPtr config, Upstream::ClusterManager& cluster_manager)
    : config_(config), cluster_manager_(cluster_manager), is_workflow_(false), successful_response_(true) {};

    FilterDataStatus decodeData(Buffer::Instance&, bool);
    FilterDataStatus encodeData(Buffer::Instance&, bool);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool);
};

} // namespace Http
} // namespace Envoy
