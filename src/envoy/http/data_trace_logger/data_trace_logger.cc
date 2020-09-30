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

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/http/header_map_impl.h"
#include "common/common/base64.h"
#include "data_trace_logger.h"

namespace Util {
/**
 * Returns true if there are no non-ascii characters in the buffer AND
 * if there are no null-terminators except at the last position in
 * the buffer.
 */
bool is_print(const std::string& s) {
    auto it = s.crbegin();
    if (*it != '\0' && !std::isprint(*it)) {
        // last char is not NULL terminator and not otherwise
        // printable, so fail
        return false;
    }

    for (++it; it != s.crend(); ++it)
        if (!std::isprint(*it))
            return false;

    return true;
}
} // namespace Util

namespace Envoy {
namespace Http {

class DummyCb : public Envoy::Upstream::AsyncStreamCallbacksAndHeaders {
public:
    ~DummyCb() = default;
    DummyCb(std::string id, std::map<std::string, std::unique_ptr<AsyncStreamCallbacksAndHeaders>>& map_ref,
        std::unique_ptr<RequestHeaderMapImpl> headers, Upstream::ClusterManager& cm) :
            id_(id), map_(map_ref), headers_(std::move(headers)), cluster_manager_(cm) {}

    virtual void onHeaders(ResponseHeaderMapPtr&&, bool) override {}
    virtual void onData(Buffer::Instance&, bool) override {}
    virtual void onTrailers(ResponseTrailerMapPtr&&) override {}
    virtual void onReset() override {}
    virtual void onComplete() override {
        // remove ourself from the clusterManager
        std::cout << "erasing ourself now" << std::endl;
        std::lock_guard<std::mutex> lock(cluster_manager_.httpRequestStorageMutex());
        map_.erase(id_);
        std::cout << "done cleaning up" << std::endl;
    }
    virtual Http::RequestHeaderMapImpl& requestHeaderMap() override {return *(headers_.get());}

private:
    std::string id_;
    std::map<std::string, std::unique_ptr<AsyncStreamCallbacksAndHeaders>>& map_;
    std::unique_ptr<RequestHeaderMapImpl> headers_;
    Upstream::ClusterManager& cluster_manager_;
};

// static DummyStreamCB stream_callbacks_;
// static std::unique_ptr<Http::RequestHeaderMapImpl> request_headers_1_;  // id
// static std::unique_ptr<Http::RequestHeaderMapImpl> request_headers_2_;

void DataTraceLogger::dumpHeaders(RequestOrResponseHeaderMap& headers, std::string span_tag) {
    std::vector<std::string> vals;
    std::stringstream ss;
    headers.iterate(
        [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
            std::stringstream& ss = *static_cast<std::stringstream*>(context);
            ss << header.key().getStringView() << ':' << header.value().getStringView() << '\n';
            return HeaderMap::Iterate::Continue;
        },
        &ss
    );
    auto& active_span = decoder_callbacks_->activeSpan();
    active_span.setTag(span_tag, ss.str());
}

void DataTraceLogger::logBufferInstance(Buffer::Instance& data, Tracing::Span& active_span, std::string tag_name) {
    size_t data_length = data.length();

    
    std::string data_str(data.toString());
    bool data_can_print = Util::is_print(data_str);
    const char *data_buffer = data_str.c_str();

    if (!data_can_print) {
        data_str = Base64::encode(data_buffer, data_length);
        data_length = data_str.length();
        data_buffer = data_str.c_str();
    }

    size_t bytes_written = 0;
    while (bytes_written < data_length) {
        int stream_fragment_count = tag_name == "request_data" ? request_stream_fragment_count_++ : response_stream_fragment_count_++;

        if (stream_fragment_count >= MAX_REQUEST_OR_RESPONSE_TAGS) return;

        size_t cur_tag_length = bytes_written + TAG_SIZE > data_length ? data_length - bytes_written : TAG_SIZE;
        std::string data_chunk(data_buffer + bytes_written, cur_tag_length);
        std::string tag_key = tag_name + std::to_string(stream_fragment_count);
        active_span.setTag(tag_key, data_chunk);
        bytes_written += cur_tag_length;

        // I believe this is a no-op.
        ENVOY_LOG(debug, "encodeData(): {}", data_str);
    }
}


FilterDataStatus DataTraceLogger::decodeData(Buffer::Instance& data, bool end_stream) {
    // intercepts the request data
    if (!should_log_) return FilterDataStatus::Continue;
    if (request_stream_) {
        Buffer::OwnedImpl cpy{data};
        request_stream_->sendData(cpy, end_stream);
    }
    if (!end_stream) {

        auto& active_span = decoder_callbacks_->activeSpan();
        if (request_stream_fragment_count_ < MAX_REQUEST_OR_RESPONSE_TAGS) {
            logBufferInstance(data, active_span, "request_data");
        }
    // } else {
    //     auto cb = cluster_manager_.httpRequestStorageMap()[request_key_].get();
    //     cb->setStream(std::move(request_stream_));
    //     std::cout << "returned from setStream " << std::endl;
    }
    return FilterDataStatus::Continue;
}

FilterDataStatus DataTraceLogger::encodeData(Buffer::Instance& data, bool end_stream) {
    // intercepts the response data
    if (!should_log_) return FilterDataStatus::Continue;
    if (response_stream_) {
        Buffer::OwnedImpl cpy{data};
        response_stream_->sendData(cpy, end_stream);
    }
    if (!end_stream) {

        auto& active_span = encoder_callbacks_->activeSpan();
        if (response_stream_fragment_count_ < MAX_REQUEST_OR_RESPONSE_TAGS) {
            logBufferInstance(data, active_span, "response_data");
        }
    }
    return FilterDataStatus::Continue;
}

FilterHeadersStatus DataTraceLogger::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
    // intercepts the request headers.
    const Http::HeaderEntry* entry = headers.get(Http::LowerCaseString(DTL_FILTER_S3_HEADER));
    should_log_ = ((entry == NULL) || (entry->value() != DTL_FILTER_S3_DONTTRACEME));
    dumpHeaders(headers, "request_headers");
    if (should_log_) {
        initializeStream(headers, "request");
    }

    return FilterHeadersStatus::Continue;
}

FilterHeadersStatus DataTraceLogger::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
    // intercepts the Response headers.
    dumpHeaders(headers, "response_headers");

    if (should_log_) initializeStream(headers, "response");

    return FilterHeadersStatus::Continue;
}

void DataTraceLogger::initializeStream(Http::RequestOrResponseHeaderMap&, std::string type) {
    assert(type == "request" || type == "response");
    std::string s3_object_key = std::to_string(rand()) + std::to_string(rand());  // twice because rand is only up to 10 billion

    std::unique_ptr<RequestHeaderMapImpl> s3_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
        {
            {Http::Headers::get().Method, Http::Headers::get().MethodValues.Post},
            {Http::Headers::get().Host, S3_UPLOADER_HOST},
            {Http::Headers::get().Path, "/upload"},
            {Http::Headers::get().ContentType, "application/octet-stream"},
            {Http::LowerCaseString(DTL_FILTER_S3_HEADER), DTL_FILTER_S3_DONTTRACEME},
            {Http::LowerCaseString(S3_KEY_HEADER), s3_object_key}
        }
    );

    Envoy::Tracing::Span& active_span = decoder_callbacks_->activeSpan();
    active_span.injectContext(*(s3_headers.get()));  // Show this request on the same span
    active_span.setTag(type + "_s3_key", s3_object_key);  // let the eng know where to find data in s3

    std::map<std::string, std::unique_ptr<Upstream::AsyncStreamCallbacksAndHeaders>> &storage_map = cluster_manager_.httpRequestStorageMap();  

    std::unique_ptr<DummyCb> callbacks(new DummyCb(s3_object_key, storage_map, std::move(s3_headers), cluster_manager_));

    // we now start modifying the map, so we need to lock it
    std::lock_guard<std::mutex> lock(cluster_manager_.httpRequestStorageMutex());

    // important to move over the callbacks BEFORE we send headers, otherwise onData() could trigger before the 
    // callbacks are in the map. That would cause all kinds of issues.
    storage_map[s3_object_key] = std::move(callbacks);

    if (type == "request") {
        request_stream_ = cluster_manager_.httpAsyncClientForCluster(S3_UPLOADER_CLUSTER).start(
            *(storage_map[s3_object_key].get()), AsyncClient::StreamOptions());
        request_stream_->sendHeaders((storage_map[s3_object_key].get())->requestHeaderMap(), false);
        request_key_ = s3_object_key;
    } else {
        response_stream_ = cluster_manager_.httpAsyncClientForCluster(S3_UPLOADER_CLUSTER).start(
            *(storage_map[s3_object_key].get()), AsyncClient::StreamOptions());
        response_stream_->sendHeaders((storage_map[s3_object_key].get())->requestHeaderMap(), false);
        response_key_ = s3_object_key;
    }
}

} // namespace Http
} // namespace Envoy
