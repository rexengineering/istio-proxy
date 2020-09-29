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

class DummyStreamCB : public AsyncClient::StreamCallbacks {
    virtual void onHeaders(ResponseHeaderMapPtr&&, bool) {std::cout << "hi" << std::endl;}

    virtual void onData(Buffer::Instance&, bool) {std::cout << "hi" << std::endl;}

    virtual void onTrailers(ResponseTrailerMapPtr&&) {}

    virtual void onComplete() {std::cout << "complete" << std::endl;}

    virtual void onReset() {}
};

static DummyStreamCB stream_callbacks_;
static std::unique_ptr<Http::RequestHeaderMapImpl> request_headers_1_;
static std::unique_ptr<Http::RequestHeaderMapImpl> request_headers_2_;


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

void DataTraceLogger::initializeStream(Http::RequestOrResponseHeaderMap& headers, std::string type) {
    assert(type == "request" || type == "response");

    if (headers.TransferEncoding() && !headers.ContentLength()) return;  // No body...don't nobody got time for that

    std::string s3_object_key = std::to_string(rand());
    if (type == "request") {
        request_headers_1_ = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
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
        active_span.injectContext(*(request_headers_1_.get()));
        active_span.setTag(type + "_s3_key", s3_object_key);
        request_stream_ = cluster_manager_.httpAsyncClientForCluster(S3_UPLOADER_CLUSTER).start(
        stream_callbacks_, AsyncClient::StreamOptions());
        request_stream_->sendHeaders(*request_headers_1_.get(), false);
    } else {
        request_headers_2_ = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
            {
                {Http::Headers::get().Method, Http::Headers::get().MethodValues.Post},
                {Http::Headers::get().Host, S3_UPLOADER_HOST},
                {Http::Headers::get().Path, "/upload"},
                {Http::Headers::get().ContentType, "application/octet-stream"},
                {Http::LowerCaseString(DTL_FILTER_S3_HEADER), DTL_FILTER_S3_DONTTRACEME},
                {Http::LowerCaseString(S3_KEY_HEADER), s3_object_key}
            }
        );
        Envoy::Tracing::Span& active_span = encoder_callbacks_->activeSpan();
        active_span.injectContext(*(request_headers_2_.get()));
        active_span.setTag(type + "_s3_key", s3_object_key);
        response_stream_ = cluster_manager_.httpAsyncClientForCluster(S3_UPLOADER_CLUSTER).start(
            stream_callbacks_, AsyncClient::StreamOptions());
        response_stream_->sendHeaders(*request_headers_2_.get(), false);
    }
}

} // namespace Http
} // namespace Envoy
