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
    }
}


FilterDataStatus DataTraceLogger::decodeData(Buffer::Instance& data, bool end_stream) {
    // intercepts the request data
    if (!should_log_) return FilterDataStatus::Continue;
    DummyCb* cb = static_cast<DummyCb*>(cluster_manager_.getCallbacksAndHeaders(req_cb_key_));
    if (cb && cb->requestStream()) {
        Buffer::OwnedImpl cpy{data};
        cb->requestStream()->sendData(cpy, end_stream);
    } else {
        std::cout << "These are not the droids you're trying to trace" << std::endl;
        decoder_callbacks_->activeSpan().setTag("request_s3_key", "Unable to store requests data");
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
    DummyCb* cb = static_cast<DummyCb*>(cluster_manager_.getCallbacksAndHeaders(res_cb_key_));

    if (cb && cb->responseStream()) {
        Buffer::OwnedImpl cpy{data};
        cb->responseStream()->sendData(cpy, end_stream);
    } else {
        std::cout << "These are not the droids you're trying to trace" << std::endl;
        encoder_callbacks_->activeSpan().setTag("response_s3_key", "Unable to store response data");
    }

    if (!end_stream) {
        auto& active_span = encoder_callbacks_->activeSpan();
        if (response_stream_fragment_count_ < MAX_REQUEST_OR_RESPONSE_TAGS) {
            logBufferInstance(data, active_span, "response_data");
        }
    }
    return FilterDataStatus::Continue;
}

FilterHeadersStatus DataTraceLogger::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
    // intercepts the request headers.
    const Http::HeaderEntry* entry = headers.get(Http::LowerCaseString(DTL_FILTER_S3_HEADER));
    should_log_ = ((entry == NULL) || (entry->value() != DTL_FILTER_S3_DONTTRACEME));
    dumpHeaders(headers, "request_headers");
    if (should_log_ && !end_stream) {
        std::cout << "enter initializeStream" << std::endl;
        initializeStream(headers, "request");
        std::cout << "exit initializeStream" << std::endl;
    }

    return FilterHeadersStatus::Continue;
}

FilterHeadersStatus DataTraceLogger::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
    // intercepts the Response headers.
    dumpHeaders(headers, "response_headers");

    if (should_log_ && !end_stream) initializeStream(headers, "response");

    return FilterHeadersStatus::Continue;
}

void DataTraceLogger::initializeStream(Http::RequestOrResponseHeaderMap&, std::string type) {
    assert(type == "request" || type == "response");

    Runtime::RandomGeneratorImpl rng;
    std::string s3_object_key = rng.uuid();

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
    std::cout << "hi" << std::endl;
    Envoy::Tracing::Span& active_span = decoder_callbacks_->activeSpan();
    std::cout << "hi2" << std::endl;
    active_span.injectContext(*(s3_headers.get()));  // Show this request on the same span
    std::cout << "hi3" << std::endl;
    active_span.setTag(type + "_s3_key", s3_object_key);  // let the eng know where to find data in s3
    std::cout << "hi4" << std::endl;

    DummyCb* callbacks = new DummyCb(s3_object_key, std::move(s3_headers), cluster_manager_);

    if (type == "request") {
        req_cb_key_ = s3_object_key;
        std::cout << "hi6" << std::endl;
        callbacks->setRequestStream(cluster_manager_.httpAsyncClientForCluster(S3_UPLOADER_CLUSTER).start(
            *callbacks, AsyncClient::StreamOptions())
        );
        std::cout << "hi7" << std::endl;
        callbacks->requestStream()->sendHeaders(callbacks->requestHeaderMap(), false);
        std::cout << "hi8" << std::endl;
        callbacks->setRequestKey(s3_object_key);
        std::cout << "hi9" << std::endl;
    } else {
        res_cb_key_ = s3_object_key;
        callbacks->setResponseStream(cluster_manager_.httpAsyncClientForCluster(S3_UPLOADER_CLUSTER).start(
            *callbacks, AsyncClient::StreamOptions())
        );
        callbacks->responseStream()->sendHeaders(callbacks->requestHeaderMap(), false);
        callbacks->setResponseKey(s3_object_key);
    }
}

} // namespace Http
} // namespace Envoy
