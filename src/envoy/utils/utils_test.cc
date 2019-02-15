/* Copyright 2017 Istio Authors. All Rights Reserved.
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

#include "src/envoy/utils/utils.h"
#include "mixer/v1/config/client/client_config.pb.h"
#include "src/istio/mixerclient/check_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

namespace {

using Envoy::Utils::CheckResponseInfoToStreamInfo;
using Envoy::Utils::ParseJsonMessage;

TEST(UtilsTest, ParseNormalMessage) {
  std::string config_str = R"({
        "default_destination_service": "service.svc.cluster.local",
      })";
  ::istio::mixer::v1::config::client::HttpClientConfig http_config;

  auto status = ParseJsonMessage(config_str, &http_config);
  EXPECT_OK(status) << status;
  EXPECT_EQ(http_config.default_destination_service(),
            "service.svc.cluster.local");
}

TEST(UtilsTest, ParseMessageWithUnknownField) {
  std::string config_str = R"({
        "default_destination_service": "service.svc.cluster.local",
        "unknown_field": "xxx",
      })";
  ::istio::mixer::v1::config::client::HttpClientConfig http_config;

  EXPECT_OK(ParseJsonMessage(config_str, &http_config));
  EXPECT_EQ(http_config.default_destination_service(),
            "service.svc.cluster.local");
}

TEST(UtilsTest, CheckResponseInfoToStreamInfo) {
  auto attributes = std::make_shared<::istio::mixerclient::SharedAttributes>();
  ::istio::mixerclient::CheckContext check_response(
      false /* fail_open */, attributes);  // by default status is unknown
  Envoy::StreamInfo::MockStreamInfo mock_stream_info;

  EXPECT_CALL(
      mock_stream_info,
      setResponseFlag(
          Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService));
  EXPECT_CALL(mock_stream_info, setDynamicMetadata(_, _))
      .WillOnce(Invoke(
          [](const std::string& key, const Envoy::ProtobufWkt::Struct& value) {
            EXPECT_EQ("istio.mixer", key);
            EXPECT_EQ(1, value.fields().count("status"));
            EXPECT_EQ("UNKNOWN", value.fields().at("status").string_value());
          }));

  CheckResponseInfoToStreamInfo(check_response, mock_stream_info);
}

}  // namespace
