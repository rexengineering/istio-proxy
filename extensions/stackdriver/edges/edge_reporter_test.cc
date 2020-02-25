/* Copyright 2019 Istio Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "extensions/stackdriver/edges/edge_reporter.h"

#include <memory>

#include "extensions/stackdriver/common/constants.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "google/protobuf/util/time_util.h"
#include "gtest/gtest.h"

namespace Extensions {
namespace Stackdriver {
namespace Edges {

using google::cloud::meshtelemetry::v1alpha1::ReportTrafficAssertionsRequest;
using ::google::protobuf::TextFormat;
using google::protobuf::util::MessageDifferencer;
using google::protobuf::util::TimeUtil;

namespace {

#define EXPECT_PROTO_EQUAL(want, got, message)   \
  std::string diff;                              \
  MessageDifferencer differ;                     \
  differ.ReportDifferencesToString(&diff);       \
  bool equal = differ.Compare(want, got);        \
  if (!equal) {                                  \
    std::cerr << message << " " << diff << "\n"; \
    FAIL();                                      \
  }                                              \
  return

class TestMeshEdgesServiceClient : public MeshEdgesServiceClient {
 public:
  typedef std::function<void(const ReportTrafficAssertionsRequest&)> TestFn;

  TestMeshEdgesServiceClient(TestFn test_func)
      : request_callback_(std::move(test_func)){};

  void reportTrafficAssertions(
      const ReportTrafficAssertionsRequest& request) const override {
    request_callback_(request);
  };

 private:
  TestFn request_callback_;
};

const char kNodeInfo[] = R"(
  name: "test_pod"
  namespace: "test_namespace"
  workload_name: "test_workload"
  owner: "kubernetes://test_owner"
  platform_metadata: {
    key: "gcp_project"
    value: "test_project"
  }
  platform_metadata: {
    key: "gcp_gke_cluster_name"
    value: "test_cluster"
  }
  platform_metadata: {
    key: "gcp_location"
    value: "test_location"
  }
  mesh_id: "test-mesh"
)";

const char kPeerInfo[] = R"(
  name: "test_peer_pod"
  namespace: "test_peer_namespace"
  workload_name: "test_peer_workload"
  owner: "kubernetes://peer_owner"
  platform_metadata: {
    key: "gcp_project"
    value: "test_project"
  }
  platform_metadata: {
    key: "gcp_gke_cluster_name"
    value: "test_cluster"
  }
  platform_metadata: {
    key: "gcp_location"
    value: "test_location"
  }
  mesh_id: "test-mesh"
)";

const char kWantGrpcRequest[] = R"(
  parent: "projects/test_project"
  mesh_uid: "test-mesh"
  traffic_assertions: {
    protocol: PROTOCOL_HTTP
    destination_service_name: "httpbin"
    destination_service_namespace: "test_namespace"
    source: {
      workload_namespace: "test_peer_namespace"
      workload_name: "test_peer_workload"
      cluster_name: "test_cluster"
      location: "test_location"
      owner_uid: "kubernetes://peer_owner"
      uid: "kubernetes://test_peer_pod.test_peer_namespace"
    }
    destination: {
      workload_namespace: "test_namespace"
      workload_name: "test_workload"
      cluster_name: "test_cluster"
      location: "test_location"
      owner_uid: "kubernetes://test_owner"
      uid: "kubernetes://test_pod.test_namespace"
    }
  }
)";

const char kWantUnknownGrpcRequest[] = R"(
  parent: "projects/test_project"
  mesh_uid: "test-mesh"
  traffic_assertions: {
    protocol: PROTOCOL_HTTP
    destination_service_name: "httpbin"
    destination_service_namespace: "test_namespace"
    source: {}
    destination: {
      workload_namespace: "test_namespace"
      workload_name: "test_workload"
      cluster_name: "test_cluster"
      location: "test_location"
      owner_uid: "kubernetes://test_owner"
      uid: "kubernetes://test_pod.test_namespace"
    }
  }
)";

wasm::common::NodeInfo nodeInfo() {
  wasm::common::NodeInfo node_info;
  TextFormat::ParseFromString(kNodeInfo, &node_info);
  return node_info;
}

wasm::common::NodeInfo peerNodeInfo() {
  wasm::common::NodeInfo node_info;
  TextFormat::ParseFromString(kPeerInfo, &node_info);
  return node_info;
}

::Wasm::Common::RequestInfo requestInfo() {
  ::Wasm::Common::RequestInfo request_info;
  request_info.destination_service_host = "httpbin.org";
  request_info.destination_service_name = "httpbin";
  request_info.request_protocol = "HTTP";
  return request_info;
}

ReportTrafficAssertionsRequest want() {
  ReportTrafficAssertionsRequest req;
  TextFormat::ParseFromString(kWantGrpcRequest, &req);
  return req;
}

ReportTrafficAssertionsRequest wantUnknown() {
  ReportTrafficAssertionsRequest req;
  TextFormat::ParseFromString(kWantUnknownGrpcRequest, &req);
  return req;
}

}  // namespace

TEST(EdgesTest, TestAddEdge) {
  int calls = 0;
  ReportTrafficAssertionsRequest got;

  auto test_client = std::make_unique<TestMeshEdgesServiceClient>(
      [&calls, &got](const ReportTrafficAssertionsRequest& request) {
        calls++;
        got = request;
      });

  auto edges = std::make_unique<EdgeReporter>(
      nodeInfo(), std::move(test_client), 10, TimeUtil::GetCurrentTime);
  edges->addEdge(requestInfo(), "test", peerNodeInfo());
  edges->reportEdges(false /* only report new edges */);

  // must ensure that we used the client to report the edges
  EXPECT_EQ(1, calls);

  // ignore timestamps in proto comparisons.
  got.set_allocated_timestamp(nullptr);

  EXPECT_PROTO_EQUAL(want(), got,
                     "ERROR: addEdge() produced unexpected result.");

  edges->reportEdges(true /* report all edges */);
  // must ensure that we used the client to report the edges
  EXPECT_EQ(2, calls);
}

TEST(EdgeReporterTest, TestRequestEdgeCache) {
  int calls = 0;
  int num_assertions = 0;

  auto test_client = std::make_unique<TestMeshEdgesServiceClient>(
      [&calls, &num_assertions](const ReportTrafficAssertionsRequest& request) {
        calls++;
        num_assertions += request.traffic_assertions_size();
      });

  auto edges = std::make_unique<EdgeReporter>(
      nodeInfo(), std::move(test_client), 1000, TimeUtil::GetCurrentTime);

  // force at least three queued reqs + current (four total)
  for (int i = 0; i < 3500; i++) {
    edges->addEdge(requestInfo(), "test", peerNodeInfo());
  }
  edges->reportEdges(false /* only send current request */);

  // nothing has changed in the peer info, so only a single edge should be
  // reported.
  EXPECT_EQ(1, calls);
  EXPECT_EQ(1, num_assertions);
}

TEST(EdgeReporterTest, TestPeriodicFlushAndCacheReset) {
  int calls = 0;
  int num_assertions = 0;

  auto test_client = std::make_unique<TestMeshEdgesServiceClient>(
      [&calls, &num_assertions](const ReportTrafficAssertionsRequest& request) {
        calls++;
        num_assertions += request.traffic_assertions_size();
      });

  auto edges = std::make_unique<EdgeReporter>(
      nodeInfo(), std::move(test_client), 100, TimeUtil::GetCurrentTime);

  // this should work as follows: 1 assertion in 1 request, the rest dropped
  // (due to cache)
  for (int i = 0; i < 350; i++) {
    edges->addEdge(requestInfo(), "test", peerNodeInfo());
    // flush on 100, 200, 300
    if (i % 100 == 0 && i > 0) {
      edges->reportEdges(false /* only send current */);
    }
  }
  // then a final assertion and additional request for a full flush.
  edges->reportEdges(true /* send full epoch-observed results */);

  // nothing has changed in the peer info, but reportEdges should be called four
  // times. two of the calls will result in no new edges or assertions. the last
  // call will have the full set.
  EXPECT_EQ(2, calls);
  EXPECT_EQ(2, num_assertions);
}

TEST(EdgeReporterTest, TestCacheMisses) {
  int calls = 0;
  int num_assertions = 0;

  auto test_client = std::make_unique<TestMeshEdgesServiceClient>(
      [&calls, &num_assertions](const ReportTrafficAssertionsRequest& request) {
        calls++;
        num_assertions += request.traffic_assertions_size();
      });

  auto edges = std::make_unique<EdgeReporter>(
      nodeInfo(), std::move(test_client), 1000, TimeUtil::GetCurrentTime);

  // force at least three queued reqs + current (four total)
  for (int i = 0; i < 3500; i++) {
    edges->addEdge(requestInfo(), std::to_string(i), peerNodeInfo());
    // flush on 1000, 2000, 3000
    if (i % 1000 == 0 && i > 0) {
      edges->reportEdges(false /* only send current */);
    }
  }
  edges->reportEdges(true /* send full epoch */);

  EXPECT_EQ(7, calls);
  // the last 500 new are not sent as part of the current
  // only as part of the epoch. and since we don't flush i == 0,
  // the initial batch is 1001.
  // so, 3001 + 3500 = 6500.
  EXPECT_EQ(6501, num_assertions);
}

TEST(EdgeReporterTest, TestMissingPeerMetadata) {
  ReportTrafficAssertionsRequest got;

  auto test_client = std::make_unique<TestMeshEdgesServiceClient>(
      [&got](const ReportTrafficAssertionsRequest& req) { got = req; });
  auto edges = std::make_unique<EdgeReporter>(
      nodeInfo(), std::move(test_client), 100, TimeUtil::GetCurrentTime);
  edges->addEdge(requestInfo(), "test", wasm::common::NodeInfo());
  edges->reportEdges(false /* only send current */);

  // ignore timestamps in proto comparisons.
  got.set_allocated_timestamp(nullptr);
  EXPECT_PROTO_EQUAL(wantUnknown(), got,
                     "ERROR: addEdge() produced unexpected result.");
}

}  // namespace Edges
}  // namespace Stackdriver
}  // namespace Extensions
