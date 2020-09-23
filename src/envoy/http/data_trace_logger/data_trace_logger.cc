#include <string>
#include <cstring>
#include <iostream>
#include <stdio.h>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"
#include "data_trace_logger.h"

namespace Util {
/**
 * Returns true if there are no non-ascii characters in the buffer AND
 * if there are no null-terminators except at the (n-1)th position in
 * the buffer.
 */
bool is_print(const char* s, int n) {
    const char* end = s + n - 1;
    for (const char *cur = s; cur < end; cur++) {
        if (!std::isprint(*cur)) return false;
    }
    return *end == '\0' || std::isprint(*end);
}
}


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
    bool data_can_print = Util::is_print(data_str.c_str(), data_length);
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
    if (!end_stream) {
        auto& active_span = decoder_callbacks_->activeSpan();

        /*  The current Zipkin trace reporter ignores logs.  I can verify the
            log message is being stored in the span, and that the serializer
            ignores log "annotations" completely.  So functionally, the
            following line does nothing. */
        active_span.log(std::chrono::system_clock::now(), "decodedData");
        if (request_stream_fragment_count_ < MAX_REQUEST_OR_RESPONSE_TAGS) {
            logBufferInstance(data, active_span, "request_data");
        }
    }
    return FilterDataStatus::Continue;
}

FilterDataStatus DataTraceLogger::encodeData(Buffer::Instance& data, bool end_stream) {
    // intercepts the response data
    if (!end_stream) {
        auto& active_span = encoder_callbacks_->activeSpan();

        active_span.log(std::chrono::system_clock::now(), "encodedData");

        if (response_stream_fragment_count_ < MAX_REQUEST_OR_RESPONSE_TAGS) {
            logBufferInstance(data, active_span, "response_data");
        }
    }
    return FilterDataStatus::Continue;
}

FilterHeadersStatus DataTraceLogger::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
    // intercepts the request headers.
    dumpHeaders(headers, "request_headers");
    return FilterHeadersStatus::Continue;
}

FilterHeadersStatus DataTraceLogger::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
    // intercepts the Response headers.
    dumpHeaders(headers, "response_headers");
    return FilterHeadersStatus::Continue;
}

} // namespace Http
} // namespace Envoy
