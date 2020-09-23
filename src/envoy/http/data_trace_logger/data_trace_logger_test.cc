#include <fstream>
#include <iostream>

#include "data_trace_logger.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "common/common/base64.h"

namespace Envoy {
namespace Http {
namespace Tracing {


class DtlFilterTest : public testing::Test {
 public:
  std::unique_ptr<DataTraceLogger> makeDtlOverrideFilter() {
    auto filter = std::make_unique<DataTraceLogger>();
    filter->setEncoderFilterCallbacks(encoder_callbacks_);
    filter->setDecoderFilterCallbacks(decoder_callbacks_);
    return filter;
  }
 protected:
   NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
   NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};


/**
 * The following are a bunch of utilities used to create fake test data.
 */

Http::TestResponseHeaderMapImpl BaseResponseHeaders() {
  return Http::TestResponseHeaderMapImpl{};
}

Http::TestRequestHeaderMapImpl createResponseHeaders(const std::map<std::string, std::string> hdrs_map) {
  auto headers = BaseResponseHeaders();
  for (auto const &k : hdrs_map) {
    headers.addCopy(k.first, k.second);
  }
  return headers;
}

Http::TestRequestHeaderMapImpl BaseRequestHeaders() {
  return Http::TestRequestHeaderMapImpl{};
}

Http::TestRequestHeaderMapImpl createRequestHeaders(const std::map<std::string, std::string> hdrs_map) {
  auto headers = BaseRequestHeaders();
  for (auto const &k : hdrs_map) {
    headers.addCopy(k.first, k.second);
  }
  return headers;
}

/**
 * The following defines a bunch of tests.
 */

TEST_F(DtlFilterTest, SimpleEncodeHeaders) {
  auto filter = makeDtlOverrideFilter();
  std::map<std::string, std::string> hdrs_map;
  hdrs_map["foo"] = "bar";
  Http::TestRequestHeaderMapImpl hdrs = createRequestHeaders(hdrs_map);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(hdrs, false));
};

TEST_F(DtlFilterTest, SimpleDecodeHeaders) {
  auto filter = makeDtlOverrideFilter();
  std::map<std::string, std::string> hdrs_map;
  hdrs_map["foo"] = "bar";
  Http::TestResponseHeaderMapImpl hdrs = createRequestHeaders(hdrs_map);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(hdrs, false));
}

/**
 * The next two tests check that simple string request and responses get logged.
 */

TEST_F(DtlFilterTest, SimpleDecodeData) {
  auto filter = makeDtlOverrideFilter();
  auto *span = new testing::NiceMock<Envoy::Tracing::MockSpan>();

  ON_CALL(decoder_callbacks_, activeSpan()).WillByDefault([&, span]() -> Envoy::Tracing::Span& {
    return *span;
  });
  EXPECT_CALL(*span, log(_, _));
  EXPECT_CALL(*span, setTag("request_data0", "Hello world"));

  std::string data = "Hello world";
  Buffer::OwnedImpl data_buf(data);
  EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data_buf, false));
  testing::Mock::AllowLeak(span);
};

TEST_F(DtlFilterTest, SimpleEncodeData) {
  auto filter = makeDtlOverrideFilter();
  auto *span = new testing::NiceMock<Envoy::Tracing::MockSpan>();

  ON_CALL(encoder_callbacks_, activeSpan()).WillByDefault([&, span]() -> Envoy::Tracing::Span& {
    return *span;
  });
  EXPECT_CALL(*span, log(_, _));
  EXPECT_CALL(*span, setTag("response_data0", "Hello world"));

  std::string data = "Hello world";
  Buffer::OwnedImpl data_buf(data);
  EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(data_buf, false));
  testing::Mock::AllowLeak(span);
};

/**
 * The next two tests check that binary data is properly b64-encoded.
 */

TEST_F(DtlFilterTest, BinaryDecodeData) {
  auto filter = makeDtlOverrideFilter();
  // NiceMock<Envoy::Tracing::MockSpan> mock_span_;
  auto *span = new testing::NiceMock<Envoy::Tracing::MockSpan>();

  ON_CALL(decoder_callbacks_, activeSpan()).WillByDefault([&, span]() -> Envoy::Tracing::Span& {
    return *span;
  });

  // Create some fake binary data
  char buf[20];
  for (int i = 0; i < 20; i++) {
    buf[i] = 128 + i;  // guaranteed to be non-ascii this way.
  }
  std::string encoded = Base64::encode(static_cast<const char*>(buf), 20);
  Buffer::OwnedImpl data_buf(static_cast<void*>(buf), 20);

  EXPECT_CALL(*span, log(_, _));
  EXPECT_CALL(*span, setTag("request_data0", encoded));
  EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data_buf, false));
  testing::Mock::AllowLeak(span);
};

TEST_F(DtlFilterTest, BinaryEncodeData) {
  auto filter = makeDtlOverrideFilter();
  // NiceMock<Envoy::Tracing::MockSpan> mock_span_;
  auto *span = new testing::NiceMock<Envoy::Tracing::MockSpan>();

  ON_CALL(encoder_callbacks_, activeSpan()).WillByDefault([&, span]() -> Envoy::Tracing::Span& {
    return *span;
  });

  // Create some fake binary data
  char buf[20];
  for (int i = 0; i < 20; i++) {
    buf[i] = 128 + i;  // guaranteed to be non-ascii this way.
  }
  std::string encoded = Base64::encode(static_cast<const char*>(buf), 20);
  Buffer::OwnedImpl data_buf(static_cast<void*>(buf), 20);

  EXPECT_CALL(*span, log(_, _));
  EXPECT_CALL(*span, setTag("response_data0", encoded));
  EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(data_buf, false));
  testing::Mock::AllowLeak(span);
};

/**
 * The next two tests check that the filter sets tags of max 32k so as to not cause
 * errors when reporting to Jaeger.
 */

// just to cause spillover
#define DELTA 0x1000

TEST_F(DtlFilterTest, LargeStringRequest) {
  auto filter = makeDtlOverrideFilter();
  // NiceMock<Envoy::Tracing::MockSpan> mock_span_;
  auto *span = new testing::NiceMock<Envoy::Tracing::MockSpan>();

  ON_CALL(decoder_callbacks_, activeSpan()).WillByDefault([&, span]() -> Envoy::Tracing::Span& {
    return *span;
  });

  // Create some fake string data, longer than 32k. This forces it to be broken up
  // into two bits.
  char buf[static_cast<int>(TAG_SIZE + DELTA)];
  for (unsigned long i = 0; i < sizeof(buf); i++) {
    buf[i] = 'a' + (i % 26);
  }
  Buffer::OwnedImpl data_buf(static_cast<void*>(buf), static_cast<int>(0x9000));

  std::string decoded_data(buf, TAG_SIZE);
  EXPECT_CALL(*span, setTag("request_data0", decoded_data));

  decoded_data.assign(buf + TAG_SIZE, sizeof (buf) - TAG_SIZE);
  EXPECT_CALL(*span, setTag("request_data1", decoded_data));

  EXPECT_CALL(*span, log(_, _)).Times(2);
  EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data_buf, false));
  testing::Mock::AllowLeak(span);
}

TEST_F(DtlFilterTest, LargeStringResponse) {
  auto filter = makeDtlOverrideFilter();
  // NiceMock<Envoy::Tracing::MockSpan> mock_span_;
  auto *span = new testing::NiceMock<Envoy::Tracing::MockSpan>();

  ON_CALL(decoder_callbacks_, activeSpan()).WillByDefault([&, span]() -> Envoy::Tracing::Span& {
    return *span;
  });

  // Create some fake string data, longer than 32k. This forces it to be broken up
  // into two bits.
  char buf[static_cast<int>(TAG_SIZE + DELTA)];
  for (unsigned long i = 0; i < sizeof(buf); i++) {
    buf[i] = 'a' + (i % 26);
  }
  Buffer::OwnedImpl data_buf(static_cast<void*>(buf), static_cast<int>(0x9000));

  std::string encoded_data(buf, TAG_SIZE);
  EXPECT_CALL(*span, setTag("response_data0", encoded_data));

  encoded_data.assign(buf + TAG_SIZE, sizeof(buf) - TAG_SIZE);
  EXPECT_CALL(*span, setTag("response_data1", encoded_data));

  EXPECT_CALL(*span, log(_, _)).Times(2);
  EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(data_buf, false));
  testing::Mock::AllowLeak(span);
}

TEST_F(DtlFilterTest, BinaryDecodeDataMulti) {
  auto filter = makeDtlOverrideFilter();
  // NiceMock<Envoy::Tracing::MockSpan> mock_span_;
  auto *span = new testing::NiceMock<Envoy::Tracing::MockSpan>();

  ON_CALL(decoder_callbacks_, activeSpan()).WillByDefault([&, span]() -> Envoy::Tracing::Span& {
    return *span;
  });

  // Create some fake binary data
  const size_t bufLen{TAG_SIZE * 2};
  char buf[bufLen];
  for (size_t i = 0; i < bufLen; i++) {
    buf[i] = i % 0x20;  // guaranteed to be non-ascii this way.
  }

  Buffer::OwnedImpl data_buf(static_cast<void*>(buf), bufLen);
  EXPECT_CALL(*span, log(_, _));
  EXPECT_CALL(*span, setTag(_, _)).Times(3);
  EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data_buf, false));

  testing::Mock::AllowLeak(span);
};

TEST_F(DtlFilterTest, BinaryEncodeDataMulti) {
  auto filter = makeDtlOverrideFilter();
  // NiceMock<Envoy::Tracing::MockSpan> mock_span_;
  auto *span = new testing::NiceMock<Envoy::Tracing::MockSpan>();

  ON_CALL(encoder_callbacks_, activeSpan()).WillByDefault([&, span]() -> Envoy::Tracing::Span& {
    return *span;
  });

  // Create some fake binary data
  const size_t bufLen{TAG_SIZE * 2};
  char buf[bufLen];
  for (size_t i = 0; i < bufLen; i++) {
    buf[i] = i % 0x20;  // guaranteed to be non-ascii this way.
  }

  Buffer::OwnedImpl data_buf(static_cast<void*>(buf), bufLen);
  EXPECT_CALL(*span, log(_, _));
  EXPECT_CALL(*span, setTag(_, _)).Times(3);
  EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(data_buf, false));

  testing::Mock::AllowLeak(span);
}

} // Namespace Tracing
} // Namespace Http
} // Namespace Envoy
