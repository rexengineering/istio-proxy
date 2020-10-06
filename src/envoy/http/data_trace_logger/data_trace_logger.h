#pragma once
#include <iostream>
#include <stdio.h>

#include "envoy/http/filter.h"
#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"
#include "common/http/header_map_impl.h"
#include <string>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <time.h>
#include <iostream>
#include <stdio.h>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "common/buffer/buffer_impl.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/http/header_map_impl.h"
#include "common/common/base64.h"
#include "data_trace_logger.h"


namespace Envoy {
namespace Http {

/**
 * The filter defined in this filter is a StreamFilter, which means that the data is passed
 * through the filter in chunks as part of a larger stream. The span (active_span) is the
 * same throughout. We store each chunk in a separate tag. Tags in the span object function
 * like keys in a dictionary; therefore, we keep a counter tok eep track of the order of
 * chunks, and we name each tag with the counter appended.
 * 
 * Furthermore, Jaeger empirically does not accept any tags longer than 64k. Hong also
 * mentioned that he had seen this number in documentation somewhere. Therefore, we limit
 * the size of any one tag to 32k.
 * 
 * Lastly, we b64-encode any binary data in the request.
 */

#define TAG_SIZE 0x8000

/**
 * Currently, Envoy stores spans in memory before flushing several entire spans at once
 * to Jaeger. Therefore, if we have a few large (1GB) requests in a row, Envoy's pod
 * will quickly run out of memory, causing a crash. Until we "gut" Envoy's trace
 * reporter and make it use streaming, we will solve this issue by imposing a cap of
 * 50M for data that can be reported in the span. Anything larger will not be
 * put into the span and therefore not logged into Jaeger.
 */
#define MAX_SPAN_DATA_SIZE 0x100000 //0x3200000
#define MAX_REQUEST_SPAN_DATA_SIZE MAX_SPAN_DATA_SIZE / 2
#define MAX_RESPONSE_SPAN_DATA_SIZE MAX_SPAN_DATA_SIZE / 2
// For code simplicity:
#define MAX_REQUEST_OR_RESPONSE_TAGS MAX_REQUEST_SPAN_DATA_SIZE / TAG_SIZE

// Don't trace requests to the S3 storage service.
#define DTL_FILTER_S3_HEADER "x-rextrace-is-s3-request"
#define S3_KEY_HEADER "x-rextrace-s3-object-key"
#define S3_UPLOADER_CLUSTER "outbound|9080||s3-uploader.default.svc.cluster.local"
#define S3_UPLOADER_HOST "s3-uploader:9080"
#define DTL_FILTER_S3_DONTTRACEME "donttraceme"

class DataTraceLogger : public PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
private:
    void dumpHeaders(RequestOrResponseHeaderMap& headers, std::string span_tag);
    void logBufferInstance(Buffer::Instance& data, Tracing::Span& active_span, std::string tag_name);
    void initializeStream(Http::RequestOrResponseHeaderMap& headers, std::string type);
    bool is_print(const char* s, int n);
    int request_stream_fragment_count_;
    int response_stream_fragment_count_;
    Upstream::ClusterManager& cluster_manager_;

    bool should_log_;
    std::string req_cb_key_;
    std::string res_cb_key_;
public:
    DataTraceLogger(Upstream::ClusterManager& cm) : request_stream_fragment_count_(0),
        response_stream_fragment_count_(0), cluster_manager_(cm) {
                srand(time(NULL));
    };
    ~DataTraceLogger() {}
    FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream);
    FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool);
};

class DummyCb : public Envoy::Upstream::AsyncStreamCallbacksAndHeaders {
public:
    ~DummyCb() {}
    DummyCb(std::string id, std::unique_ptr<RequestHeaderMapImpl> headers, Upstream::ClusterManager& cm) 
        : id_(id), headers_(std::move(headers)), cluster_manager_(cm) {
        cluster_manager_.storeCallbacksAndHeaders(id, this);
    }

    void onHeaders(ResponseHeaderMapPtr&&, bool) override {}
    void onData(Buffer::Instance&, bool) override {}
    void onTrailers(ResponseTrailerMapPtr&&) override {}
    void onReset() override {}
    void onComplete() override {
        // remove ourself from the clusterManager
        cluster_manager_.eraseCallbacksAndHeaders(id_);
    }
    Http::RequestHeaderMapImpl& requestHeaderMap() override {
        return *(headers_.get());
    }

    void setRequestStream(AsyncClient::Stream* stream) { request_stream_ = stream;}
    AsyncClient::Stream* requestStream() { return request_stream_; }

    void setResponseStream(AsyncClient::Stream* stream) { response_stream_ = stream;}
    AsyncClient::Stream* responseStream() { return response_stream_; }

    void setRequestKey(std::string& key) { request_key_ = key;}
    std::string& getRequestKey() { return request_key_;}

    void setResponseKey(std::string& key) { response_key_ = key;}
    std::string& getResponseKey() { return response_key_;}

private:
    std::string id_;
    std::unique_ptr<RequestHeaderMapImpl> headers_;
    Upstream::ClusterManager& cluster_manager_;

    AsyncClient::Stream* request_stream_;
    AsyncClient::Stream* response_stream_;

    std::string request_key_;
    std::string response_key_;

};


} // namespace Http
} // namespace Envoy
