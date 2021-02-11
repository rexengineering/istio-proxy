#include <fstream>
#include <iostream>

#include "bavs.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/common/upstream/utility.h"
#include "envoy/upstream/cluster_manager.h"
#include "common/common/base64.h"

namespace Envoy {
namespace Http {
namespace Tracing {

class MockBavsCallbacks : public Envoy::Http::BavsRetriableCallbacks {
public:
    ~MockBavsCallbacks() {};
    MockBavsCallbacks(std::string, std::unique_ptr<Http::RequestHeaderMapImpl>,
                      Upstream::ClusterManager&, int, std::string, std::string,
                      std::string) {};
    MockBavsCallbacks() {};

    MOCK_METHOD(void, onHeaders, (ResponseHeaderMapPtr&&, bool));
    MOCK_METHOD(void, onData, (Buffer::Instance&, bool));
    MOCK_METHOD(void, onTrailers, (ResponseTrailerMapPtr&&));
    MOCK_METHOD(void, onReset, (), (override));
    MOCK_METHOD(void, onComplete, () ,(override));
    MOCK_METHOD(Http::RequestHeaderMapImpl&, requestHeaderMap, ());
    MOCK_METHOD(void, setRequestStream, (AsyncClient::Stream* stream));
    MOCK_METHOD(AsyncClient::Stream*, requestStream, ());
    MOCK_METHOD(void, setResponseStream, (AsyncClient::Stream* stream));
    MOCK_METHOD(AsyncClient::Stream*, responseStream, ());
    MOCK_METHOD(void, setRequestKey, (std::string& key));
    MOCK_METHOD(std::string&, getRequestKey, ());
    MOCK_METHOD(void, setResponseKey, (std::string& key));
    MOCK_METHOD(std::string&, getResponseKey, ());
    MOCK_METHOD(void, setStream, (AsyncClient::Stream*));
    MOCK_METHOD(AsyncClient::Stream*, getStream, ());
};

class MockUpstreamConfig : public Envoy::Http::UpstreamConfig {
public:
    ~MockUpstreamConfig() {};
    MockUpstreamConfig(const bavs::Upstream&) {};
    MockUpstreamConfig() {};
    // MockUpstreamConfig(std::string id, std::unique_ptr<Http::RequestHeaderMapImpl> headers,
    //                    Upstream::ClusterManager& cm, int num_retries, std::string fail_cluster, std::string cluster,
    //                    std::string fail_cluster_path) {}
    MOCK_METHOD(const std::string&, name, (), (const));
    MOCK_METHOD(const std::string&, cluster, (), (const));
    MOCK_METHOD(const std::string&, host, (), (const));
    MOCK_METHOD(const std::string&, path, (), (const));
    MOCK_METHOD(const std::string&, method, (), (const));
    MOCK_METHOD(const std::string&, taskId, (), (const));
    MOCK_METHOD(int, port, (), (const));
    MOCK_METHOD(int, totalAttempts, (), (const));
};

class MockBavsFilterConfig : public Envoy::Http::BavsFilterConfig {
public:
    virtual ~MockBavsFilterConfig() {};
    MockBavsFilterConfig(const bavs::BAVSFilter&) {};
    MockBavsFilterConfig() {};
    MOCK_METHOD(const std::string&, wfIdValue, (), (override));
    MOCK_METHOD(const std::string&, flowdCluster, (), (override));
    MOCK_METHOD(const std::string&, flowdPath, (), (override));
    MOCK_METHOD(const std::string&, taskId, (), (override));
    MOCK_METHOD(const std::vector<const UpstreamConfigSharedPtr>&, forwards, (), (override));
};


/**
 * The following example exercises almost all functionality, including Retries.
 */
TEST(BavsFilterTest, SimpleCall) {
    Upstream::MockClusterManager cluster_manager_;
    auto config = std::make_shared<NiceMock<MockBavsFilterConfig>>();
    auto filter = std::make_unique<BavsFilter>(config, cluster_manager_);
    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
    filter->setEncoderFilterCallbacks(encoder_callbacks);

    std::string name = "bavs-filter-name";
    std::string cluster = "next-task-cluster";
    std::string host = "next-task-host";
    std::string flowd_cluster = "outbound||9002|flowd.rexflow.svc.cluster.local";
    std::string flowd_path = "/instancefail";
    std::string task_id = "task-1234";
    std::string next_task_id = "task-5432";
    std::string wf_id_value = "wf-1234";
    std::string path = "/";
    std::string method = "post";
    int port = 1234;
    int total_attempts = 1;

    auto *upstream = new testing::NiceMock<MockUpstreamConfig>();
    std::shared_ptr<NiceMock<MockUpstreamConfig>> upstream_ptr(upstream);
    std::vector<const std::shared_ptr<Envoy::Http::UpstreamConfig>> upstreams{upstream_ptr};

    auto *span = new testing::NiceMock<Envoy::Tracing::MockSpan>();
    auto *cb = new testing::NiceMock<MockBavsCallbacks>();
    auto *stream = new testing::NiceMock<MockAsyncClientStream>();

    // Configure the `config`
    ON_CALL(*config, flowdCluster()).WillByDefault([&, flowd_cluster]() -> const std::string& {
      return flowd_cluster;
    });
    ON_CALL(*config, flowdPath()).WillByDefault([&, flowd_path]() -> const std::string& {
      return flowd_path;
    });
    ON_CALL(*config, wfIdValue()).WillByDefault([&, wf_id_value]() -> const std::string& {
      return wf_id_value;
    });
    ON_CALL(*config, forwards()).WillByDefault([&, upstreams]() -> const std::vector<const UpstreamConfigSharedPtr>& {
      return upstreams;
    });
    ON_CALL(*config, taskId()).WillByDefault([&, task_id]() -> const std::string& {
      return task_id;
    });

    // Configure the `upstream`
    ON_CALL(*upstream, cluster()).WillByDefault([&, cluster]() -> const std::string& {
        return cluster;
    });
    ON_CALL(*upstream, name()).WillByDefault([&, name]() -> const std::string& {
        return name;
    });
    ON_CALL(*upstream, host()).WillByDefault([&, host]() -> const std::string& {
        return host;
    });
    ON_CALL(*upstream, path()).WillByDefault([&, path]() -> const std::string& {
        return path;
    });
    ON_CALL(*upstream, method()).WillByDefault([&, method]() -> const std::string& {
        return method;
    });
    ON_CALL(*upstream, port()).WillByDefault([&, port]() -> int {
        return port;
    });
    ON_CALL(*upstream, taskId()).WillByDefault([&, next_task_id]() -> const std::string& {
        return next_task_id;
    });
    ON_CALL(*upstream, totalAttempts()).WillByDefault([&, total_attempts]() -> int {
        return total_attempts;
    });

    ON_CALL(encoder_callbacks, activeSpan()).WillByDefault([&, span]() -> Envoy::Tracing::Span& {
        return *span;
    });
    ON_CALL(cluster_manager_, getCallbacksAndHeaders).WillByDefault([&, cb](std::string& ) -> Upstream::AsyncStreamCallbacksAndHeaders* {
        return static_cast<Upstream::AsyncStreamCallbacksAndHeaders*>(cb);
    });
    ON_CALL(*cb, requestStream).WillByDefault([&, stream]() -> Http::AsyncClient::Stream* {
        return stream;
    });

    std::string flow_id = "my-flow-id";
    // Send some headers to simulate request.
    Http::TestRequestHeaderMapImpl request_headers;
    request_headers.addCopy(Http::Headers::get().Method, "post");
    request_headers.addCopy(Http::LowerCaseString("x-rexflow-wf-id"), wf_id_value);
    request_headers.addCopy(Http::LowerCaseString("x-rexflow-task-id"), task_id);
    request_headers.addCopy(Http::LowerCaseString("x-flow-id"), flow_id);

    std::cout << "test.cc: Config's address: " << &config << std::endl;
    EXPECT_CALL(*config, taskId()).WillOnce(testing::ReturnPointee(&task_id));
    EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));

    // Send some request data. Don't care.
    std::string data = "Who is Padawan Colt McNealy's Jedi Instructor?";
    Buffer::OwnedImpl data_buf(data);
    EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data_buf, false));
    data_buf.drain(data.length());
    EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data_buf, true));

    std::cout << "got to 177" << std::endl;
    // Simulate some response headers
    std::string content_type = "foobar_content-type";
    std::string response_data = "Padawan McNealy's Master is Jedi Master HongDa Tang.";
    int content_length = response_data.length();
    Http::TestResponseHeaderMapImpl response_headers;
    response_headers.addCopy(Http::LowerCaseString("content-length"), std::to_string(content_length));
    response_headers.addCopy(Http::LowerCaseString("content-type"), content_type);
    response_headers.addCopy(Http::LowerCaseString("status"), "200");
    ON_CALL(*stream, sendHeaders).WillByDefault([&, content_type, next_task_id, wf_id_value, flow_id] 
                                                (Http::RequestHeaderMap& hdrs, bool end_stream) -> void {
        EXPECT_EQ(false, end_stream);
        EXPECT_EQ(hdrs.get(Http::LowerCaseString("x-flow-id"))->value().getStringView(), flow_id);
        EXPECT_EQ(hdrs.get(Http::LowerCaseString("x-rexflow-wf-id"))->value().getStringView(), wf_id_value);
        EXPECT_EQ(hdrs.get(Http::LowerCaseString("content-type"))->value().getStringView(), content_type);
        EXPECT_EQ(hdrs.get(Http::LowerCaseString("x-rexflow-task-id"))->value().getStringView(), next_task_id);
    });
    EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));

    // Send some response data.
    Buffer::OwnedImpl response_data_buf(response_data);

    ON_CALL(*stream, sendData).WillByDefault([&, response_data](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ(response_data, data.toString());
        EXPECT_EQ(false, end_stream);
    });
    EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(response_data_buf, false));

    // Up to this point, everything is "happy path". Now, we use the callbacks to simulate a
    // failed response from the next task. This should trigger a retry.
    std::unique_ptr<Http::ResponseHeaderMap> resp_hdrs = Http::ResponseHeaderMapImpl::create();
    resp_hdrs->setStatus("500");

    // Expect call to happen again.
    EXPECT_CALL(*stream, sendData).WillOnce([&, response_data](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ(response_data, data.toString());
        EXPECT_EQ(false, end_stream);
    });

    cb->onHeaders(std::move(resp_hdrs), true);
    cb->onComplete();


    testing::Mock::AllowLeak(span);
    testing::Mock::AllowLeak(cb);
    testing::Mock::AllowLeak(stream);
    testing::Mock::AllowLeak(upstream);
}

} // Namespace Tracing
} // Namespace Http
} // Namespace Envoy