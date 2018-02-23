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

#include "src/istio/control/http/attributes_builder.h"

#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "include/istio/utils/attributes_builder.h"
#include "src/istio/control/attribute_names.h"
#include "src/istio/control/http/mock_check_data.h"
#include "src/istio/control/http/mock_report_data.h"

using ::google::protobuf::TextFormat;
using ::google::protobuf::util::MessageDifferencer;
using ::istio::mixer::v1::Attributes;
using ::istio::mixer::v1::Attributes_StringMap;

using ::testing::_;
using ::testing::Invoke;

namespace istio {
namespace control {
namespace http {
namespace {

const char kCheckAttributes[] = R"(
attributes {
  key: "context.protocol"
  value {
    string_value: "http"
  }
}
attributes {
  key: "request.headers"
  value {
    string_map_value {
      entries {
        key: "host"
        value: "localhost"
      }
      entries {
        key: "path"
        value: "/books"
      }
    }
  }
}
attributes {
  key: "request.host"
  value {
    string_value: "localhost"
  }
}
attributes {
  key: "request.path"
  value {
    string_value: "/books"
  }
}
attributes {
  key: "request.scheme"
  value {
    string_value: "http"
  }
}
attributes {
  key: "request.time"
  value {
    timestamp_value {
    }
  }
}
attributes {
  key: "source.ip"
  value {
    bytes_value: "1.2.3.4"
  }
}
attributes {
  key: "source.port"
  value {
    int64_value: 8080
  }
}
attributes {
  key: "connection.mtls"
  value {
    bool_value: true
  }
}
attributes {
  key: "source.user"
  value {
    string_value: "test_user"
  }
}
attributes {
  key: "request.auth.audiences"
  value {
    string_value: "thisisaud"
  }
}
attributes {
  key: "request.auth.claims"
  value {
    string_map_value {
      entries {
        key: "aud"
        value: "thisisaud"
      }
      entries {
        key: "azp"
        value: "thisisazp"
      }
      entries {
        key: "email"
        value: "thisisemail@email.com"
      }
      entries {
        key: "exp"
        value: "5112754205"
      }
      entries {
        key: "iat"
        value: "1512754205"
      }
      entries {
        key: "iss"
        value: "thisisiss"
      }
      entries {
        key: "sub"
        value: "thisissub"
      }
    }
  }
}
attributes {
  key: "request.auth.presenter"
  value {
    string_value: "thisisazp"
  }
}
attributes {
  key: "request.auth.principal"
  value {
    string_value: "thisisiss/thisissub"
  }
}
)";

const char kReportAttributes[] = R"(
attributes {
  key: "request.size"
  value {
    int64_value: 100
  }
}
attributes {
  key: "response.code"
  value {
    int64_value: 404
  }
}
attributes {
  key: "response.duration"
  value {
    duration_value {
      nanos: 1
    }
  }
}
attributes {
  key: "response.headers"
  value {
    string_map_value {
      entries {
        key: "content-length"
        value: "123456"
      }
      entries {
        key: "server"
        value: "my-server"
      }
    }
  }
}
attributes {
  key: "response.size"
  value {
    int64_value: 200
  }
}
attributes {
  key: "response.time"
  value {
    timestamp_value {
    }
  }
}
)";

void ClearContextTime(const std::string &name, RequestContext *request) {
  // Override timestamp with -
  utils::AttributesBuilder builder(&request->attributes);
  std::chrono::time_point<std::chrono::system_clock> time0;
  builder.AddTimestamp(name, time0);
}

TEST(AttributesBuilderTest, TestExtractForwardedAttributes) {
  Attributes attr;
  (*attr.mutable_attributes())["test_key"].set_string_value("test_value");

  ::testing::NiceMock<MockCheckData> mock_data;
  EXPECT_CALL(mock_data, ExtractIstioAttributes(_))
      .WillOnce(Invoke([&attr](std::string *data) -> bool {
        attr.SerializeToString(data);
        return true;
      }));

  RequestContext request;
  AttributesBuilder builder(&request);
  builder.ExtractForwardedAttributes(&mock_data);
  EXPECT_TRUE(MessageDifferencer::Equals(request.attributes, attr));
}

TEST(AttributesBuilderTest, TestForwardAttributes) {
  Attributes forwarded_attr;
  ::testing::NiceMock<MockHeaderUpdate> mock_header;
  EXPECT_CALL(mock_header, AddIstioAttributes(_))
      .WillOnce(Invoke([&forwarded_attr](const std::string &data) {
        EXPECT_TRUE(forwarded_attr.ParseFromString(data));
      }));

  Attributes origin_attr;
  (*origin_attr.mutable_attributes())["test_key"].set_string_value(
      "test_value");

  AttributesBuilder::ForwardAttributes(origin_attr, &mock_header);
  EXPECT_TRUE(MessageDifferencer::Equals(origin_attr, forwarded_attr));
}

TEST(AttributesBuilderTest, TestCheckAttributes) {
  ::testing::NiceMock<MockCheckData> mock_data;
  EXPECT_CALL(mock_data, GetSourceIpPort(_, _))
      .WillOnce(Invoke([](std::string *ip, int *port) -> bool {
        *ip = "1.2.3.4";
        *port = 8080;
        return true;
      }));
  EXPECT_CALL(mock_data, GetSourceUser(_))
      .WillOnce(Invoke([](std::string *user) -> bool {
        *user = "test_user";
        return true;
      }));
  EXPECT_CALL(mock_data, IsMutualTLS())
      .WillOnce(Invoke([]() -> bool { return true; }));
  EXPECT_CALL(mock_data, GetRequestHeaders())
      .WillOnce(Invoke([]() -> std::map<std::string, std::string> {
        std::map<std::string, std::string> map;
        map["path"] = "/books";
        map["host"] = "localhost";
        return map;
      }));
  EXPECT_CALL(mock_data, FindHeaderByType(_, _))
      .WillRepeatedly(Invoke(
          [](CheckData::HeaderType header_type, std::string *value) -> bool {
            if (header_type == CheckData::HEADER_PATH) {
              *value = "/books";
              return true;
            } else if (header_type == CheckData::HEADER_HOST) {
              *value = "localhost";
              return true;
            }
            return false;
          }));
  EXPECT_CALL(mock_data, GetJWTPayload(_))
      .WillOnce(Invoke([](std::map<std::string, std::string> *payload) -> bool {
        (*payload)["iss"] = "thisisiss";
        (*payload)["sub"] = "thisissub";
        (*payload)["aud"] = "thisisaud";
        (*payload)["azp"] = "thisisazp";
        (*payload)["email"] = "thisisemail@email.com";
        (*payload)["iat"] = "1512754205";
        (*payload)["exp"] = "5112754205";
        return true;
      }));

  RequestContext request;
  AttributesBuilder builder(&request);
  builder.ExtractCheckAttributes(&mock_data);

  ClearContextTime(AttributeName::kRequestTime, &request);

  std::string out_str;
  TextFormat::PrintToString(request.attributes, &out_str);
  GOOGLE_LOG(INFO) << "===" << out_str << "===";

  Attributes expected_attributes;
  ASSERT_TRUE(
      TextFormat::ParseFromString(kCheckAttributes, &expected_attributes));
  EXPECT_TRUE(
      MessageDifferencer::Equals(request.attributes, expected_attributes));
}

TEST(AttributesBuilderTest, TestReportAttributes) {
  ::testing::NiceMock<MockReportData> mock_data;
  EXPECT_CALL(mock_data, GetResponseHeaders())
      .WillOnce(Invoke([]() -> std::map<std::string, std::string> {
        std::map<std::string, std::string> map;
        map["content-length"] = "123456";
        map["server"] = "my-server";
        return map;
      }));
  EXPECT_CALL(mock_data, GetReportInfo(_))
      .WillOnce(Invoke([](ReportData::ReportInfo *info) {
        info->received_bytes = 100;
        info->send_bytes = 200;
        info->duration = std::chrono::nanoseconds(1);
        info->response_code = 404;
      }));

  RequestContext request;
  AttributesBuilder builder(&request);
  builder.ExtractReportAttributes(&mock_data);

  ClearContextTime(AttributeName::kResponseTime, &request);

  std::string out_str;
  TextFormat::PrintToString(request.attributes, &out_str);
  GOOGLE_LOG(INFO) << "===" << out_str << "===";

  Attributes expected_attributes;
  ASSERT_TRUE(
      TextFormat::ParseFromString(kReportAttributes, &expected_attributes));
  EXPECT_TRUE(
      MessageDifferencer::Equals(request.attributes, expected_attributes));
}

}  // namespace
}  // namespace http
}  // namespace control
}  // namespace istio
