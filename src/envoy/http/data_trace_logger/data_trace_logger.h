#pragma once
#include <iostream>
#include <stdio.h>

#include "envoy/http/filter.h"
// #include "config.h"
#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"


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
#define MAX_SPAN_DATA_SIZE 0x3200000
#define MAX_REQUEST_SPAN_DATA_SIZE MAX_SPAN_DATA_SIZE / 2
#define MAX_RESPONSE_SPAN_DATA_SIZE MAX_SPAN_DATA_SIZE / 2
// For code simplicity:
#define MAX_REQUEST_OR_RESPONSE_TAGS MAX_REQUEST_SPAN_DATA_SIZE / TAG_SIZE


class DataTraceLogger : public PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
private:
    void dumpHeaders(RequestOrResponseHeaderMap& headers, std::string span_tag);
    void logBufferInstance(Buffer::Instance& data, Tracing::Span& active_span, std::string tag_name);
    bool is_print(const char* s, int n);
    int request_stream_fragment_count_;
    int response_stream_fragment_count_;
public:
    DataTraceLogger() : request_stream_fragment_count_(0), response_stream_fragment_count_(0) {};
    FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream);
    FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool);
};

} // namespace Http
} // namespace Envoy
