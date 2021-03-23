#pragma once
#include <iostream>
#include <stdio.h>
#include <map>

#include "envoy/http/filter.h"
#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"
#include "envoy/http/async_client.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"
#include "common/common/random_generator.h"

#include "src/envoy/http/bavs_2.0/newbavs.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "common/upstream/cluster_manager_impl.h"

#include "common/json/json_loader.h"
#include "envoy/json/json_object.h"

namespace Envoy {
namespace Http {


class UpstreamConfig {
public:
    UpstreamConfig() {}
    UpstreamConfig(const newbavs::NewBAVSUpstream& proto_config) :
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
    BavsFilterConfig(const newbavs::NewBAVSFilter& proto_config);

    virtual const std::string& wfIdValue() { return wf_id_; }
    virtual const std::string& flowdCluster() { return flowd_cluster_; }
    virtual const std::string& flowdPath() { return flowd_path_; }
    virtual const std::string& taskId() { return task_id_; }
    virtual const std::string& trafficShadowCluster() { return traffic_shadow_cluster_; }
    virtual const std::string& trafficShadowPath() { return traffic_shadow_path_; }
    virtual const std::vector<std::string>& headersToForward() { return headers_to_forward_; }

    virtual const std::vector<const UpstreamConfigSharedPtr>& forwards() { return forwards_; }
    bool processInputParams() { return !input_params_.empty(); }
    bool processOutputParams() { return !output_params_.empty(); }
    const std::map<std::string, std::string>& inputParams() const { return input_params_; }
    const std::map<std::string, std::string>& outputParams() const { return output_params_; }

private:
    std::vector<const UpstreamConfigSharedPtr> forwards_;
    std::string wf_id_;
    std::string flowd_cluster_;
    std::string flowd_path_;
    std::string task_id_;
    std::string traffic_shadow_cluster_;
    std::string traffic_shadow_path_;
    std::vector<std::string> headers_to_forward_;
    std::vector<std::pair<std::string, std::string>> input_params_;
    std::vector<std::pair<std::string, std::string>> output_params_;
};

using BavsFilterConfigSharedPtr = std::shared_ptr<BavsFilterConfig>;


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

    void onData(Buffer::Instance& data, bool) override {
        std::cout << "Outboundcallbacks ondata: " << data.toString() << std::endl;
    }
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


class BavsInboundCallbacks : public Envoy::Upstream::AsyncStreamCallbacksAndHeaders {
public:
    BavsInboundCallbacks(std::string id, std::unique_ptr<Http::RequestHeaderMapImpl> headers,
            Upstream::ClusterManager& cm, BavsFilterConfigSharedPtr config,
            std::map<std::string, std::string> saved_headers, std::string instance_id,
            std::string spanid)
        : id_(id), headers_(std::move(headers)), cluster_manager_(cm), config_(config),
          saved_headers_(saved_headers), instance_id_(instance_id),
          spanid_(spanid) {
        cluster_manager_.storeCallbacksAndHeaders(id, this);
    }

    void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override {
        // If bad response from upstream, don't send to next step in workflow.
        std::string status_str(headers->getStatusValue());
        int status = atoi(status_str.c_str());
        if (status < 200 || status >= 300) {
            std::cout << "got bad status: " << status << std::endl;
            // FIXME: Do something useful here, perhaps let Flowd know?
            return;
        }

        const Http::HeaderEntry* content_type_entry(headers->get(Http::Headers::get().ContentType));
        // Safe since onHeaders() can only be called once per http request
        std::string content_type(((content_type_entry != NULL) && (content_type_entry->value() != NULL)) ?
            content_type_entry->value().getStringView() : "application/json"
        );
        Random::RandomGeneratorImpl rng;
        for (auto iter = config_->forwards().begin(); iter != config_->forwards().end(); iter++) {
            const UpstreamConfig& upstream(**iter);
            std::string req_cb_key = rng.uuid();

            // Create headers to send over to the next place.
            std::unique_ptr<RequestHeaderMapImpl> request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
                {
                    {Http::Headers::get().Method, upstream.method()},
                    {Http::Headers::get().Host, upstream.full_hostname() + ":" + std::to_string(upstream.port())},
                    {Http::Headers::get().Path, upstream.path()},
                    {Http::Headers::get().ContentType, content_type},
                    {Http::Headers::get().ContentLength, std::string(headers->getContentLengthValue())},
                    {Http::LowerCaseString("x-rexflow-wf-id"), config_->wfIdValue()},
                    {Http::LowerCaseString("x-rexflow-error-after"), std::to_string(upstream.totalAttempts())},
                    {Http::LowerCaseString("x-rexflow-error-path"), config_->flowdPath()},
                    {Http::LowerCaseString("x-rexflow-task-id"), upstream.taskId()},
                    {Http::LowerCaseString("x-flow-id"), instance_id_}
                }
            );

            for (const auto& saved_header : saved_headers_) {
                std::cout << "forwarding!!" << *iter << std::endl;
                request_headers->setCopy(Http::LowerCaseString(saved_header.first), saved_header.second);
            }
            // Inject tracing context
            request_headers->setCopy(Http::LowerCaseString("x-b3-spanid"), spanid_);

            // Envoy speaks like "outbound|5000||secret-sauce.default.svc.cluster.local"
            std::string cluster_string = "outbound|" + std::to_string(upstream.port()) + "||" + upstream.full_hostname();
            Http::AsyncClient* client = nullptr;
            try {
                client = &(cluster_manager_.httpAsyncClientForCluster(cluster_string));
            } catch(const EnvoyException&) {
                std::cout << "Could not find the cluster " << cluster_string << " on WF Instance " << instance_id_;
                std::cout << "...sending traffic to Flowd instead." << std::endl;;

                // Try to send traffic to Flowd so at least we save the state of the WF Instance.
                try {
                    client = &(cluster_manager_.httpAsyncClientForCluster(config_->flowdCluster()));
                    request_headers->setCopy(Http::LowerCaseString("x-rexflow-original-path"), request_headers->getPathValue());
                    request_headers->setCopy(Http::LowerCaseString("x-rexflow-original-host"), request_headers->getHostValue());
                    request_headers->setPath(config_->flowdPath());
                    cluster_string = config_->flowdCluster();
                } catch(const EnvoyException&) {
                    std::cout << "Could not connect to Flowd on WF Instance " << instance_id_ << std::endl;
                    continue;
                }
            }
            if (!client) {
                // Completely out of luck.
                std::cout << "Could not connect to Flowd on WF Instance " << instance_id_ << std::endl;
                continue;
            }

            BavsOutboundCallbacks* callbacks = new BavsOutboundCallbacks(
                req_cb_key, std::move(request_headers), cluster_manager_,
                upstream.totalAttempts(), config_->flowdCluster(), cluster_string, config_->flowdPath(),
                *iter /* `*iter` is an UpstreamConfigSharedPtr*/);
            callbacks->setStream(client->start(
                *callbacks, AsyncClient::StreamOptions())
            );

            if (config_->trafficShadowCluster() != "") {
                sendShadowHeaders(callbacks->requestHeaderMap());
            }
            callbacks->getStream()->sendHeaders(callbacks->requestHeaderMap(), end_stream);
            req_cb_keys.push_back(req_cb_key);
        }
    }

    void sendShadowHeaders(Http::RequestHeaderMapImpl& original_headers) {
        // copy the headers
        std::unique_ptr<RequestHeaderMapImpl> headers = Http::RequestHeaderMapImpl::create();
        original_headers.iterate(
            [&headers](const HeaderEntry& header) -> HeaderMap::Iterate {
                RequestHeaderMapImpl* hdrs = headers.get();
                std::string key(header.key().getStringView());
                hdrs->addCopy(Http::LowerCaseString(key), header.value().getStringView());
                return HeaderMap::Iterate::Continue;
            }
        );
        headers->setCopy(Http::LowerCaseString("x-rexflow-original-host"), headers->getHostValue()); // host gets overwritten
        headers->setCopy(Http::LowerCaseString("x-rexflow-original-path"), headers->getPathValue()); // host gets overwritten
        headers->setPath(config_->trafficShadowPath());

        Random::RandomGeneratorImpl rng;
        std::string guid = rng.uuid();
        Envoy::Upstream::CallbacksAndHeaders* callbacks = new Upstream::CallbacksAndHeaders(guid, std::move(headers), cluster_manager_);
        Http::AsyncClient* client = nullptr;
        try {
            client = &(cluster_manager_.httpAsyncClientForCluster(config_->trafficShadowCluster()));
        } catch(const EnvoyException&) {
            std::cout << "Couldn't find Kafka cluster: " << config_->trafficShadowCluster() << std::endl;
            return;
        }
        if (!client) return;
        callbacks->setStream(client->start(*callbacks, AsyncClient::StreamOptions()));
        if (callbacks->getStream()) {
            callbacks->getStream()->sendHeaders(callbacks->requestHeaderMap(), false);
        }
        req_cb_keys.push_back(guid);
    }

    void onData(Buffer::Instance& data, bool end_stream) override {
        std::cout << "InboundCallbacks onData: " << data.toString() << std::endl;
        request_data_.add(data);
        if (!end_stream) return;

        // TODO: fancy json processing

        for (auto iter=req_cb_keys.begin(); iter != req_cb_keys.end(); iter++) {
            Envoy::Upstream::AsyncStreamCallbacksAndHeaders* cb = 
                cluster_manager_.getCallbacksAndHeaders(*iter);
            if (cb != NULL) {
                BavsOutboundCallbacks* bavs_cb = dynamic_cast<BavsOutboundCallbacks*>(cb);
                if (bavs_cb) {
                    // special method for the BavsOutboundCallbacks to enable retries, etc.
                    bavs_cb->addData(request_data_);
                }
                Http::AsyncClient::Stream* stream(cb->getStream());
                if (stream != NULL) {
                    Buffer::OwnedImpl cpy;
                    cpy.add(request_data_);
                    stream->sendData(cpy, true);
                } else {
                    std::cout << "NULL HTTP stream pointer!" << std::endl;
                    // FIXME: Do something useful here since the request failed. Maybe notify flowd?
                }
            } else {
                std::cout << "NULL callback pointer!" << std::endl;
                // FIXME: Do something useful here since the request failed. Maybe notify flowd?
            }
        }
    }

    void onTrailers(Http::ResponseTrailerMapPtr&&) override {}
    void onReset() override {
    }
    void onComplete() override {
        // remove ourself from the clusterManager
        cluster_manager_.eraseCallbacksAndHeaders(id_);
    }
    Http::RequestHeaderMapImpl& requestHeaderMap() override {
        return *(headers_.get());
    }

    void setStream(Http::AsyncClient::Stream* stream) override { request_stream_ = stream;}
    Http::AsyncClient::Stream* getStream() override { return request_stream_; }

private:

    std::string id_;
    std::unique_ptr<Http::RequestHeaderMapImpl> headers_;
    Upstream::ClusterManager& cluster_manager_;
    BavsFilterConfigSharedPtr config_;
    Http::AsyncClient::Stream* request_stream_;
    std::map<std::string, std::string> saved_headers_;
    std::string instance_id_;
    std::string spanid_;
    std::vector<std::string> req_cb_keys;
    Buffer::OwnedImpl request_data_;

};


class BavsFilter20 : public PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
private:
    // void sendShadowHeaders(Http::RequestHeaderMapImpl& original_headers, bool);
    // void sendShadowData(Buffer::Instance&, bool);
    const BavsFilterConfigSharedPtr config_;
    Upstream::ClusterManager& cluster_manager_;
    bool is_workflow_;
    bool successful_response_;
    // std::vector<std::string> req_cb_keys_;
    std::string instance_id_;
    std::unique_ptr<RequestMessageImpl> message_;
    std::map<std::string, std::string> saved_headers_;
    std::string service_cluster_;
    std::unique_ptr<RequestHeaderMapImpl> request_headers_;
    Buffer::OwnedImpl request_data_;
    BavsInboundCallbacks* callbacks_;
    std::string callback_key_;
    std::string spanid_;

    // Envoy::Upstream::AsyncStreamCallbacksAndHeaders* shadow_callbacks_;
    void sendHeaders(bool end_stream);

public:
    BavsFilter20(BavsFilterConfigSharedPtr config, Upstream::ClusterManager& cluster_manager)
    : config_(config), cluster_manager_(cluster_manager), is_workflow_(false), successful_response_(true),
      service_cluster_("inbound|5000||"),
      request_headers_(Http::RequestHeaderMapImpl::create()) {};

    FilterDataStatus decodeData(Buffer::Instance&, bool);
    FilterDataStatus encodeData(Buffer::Instance&, bool);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool);
};

} // namespace Http
} // namespace Envoy
