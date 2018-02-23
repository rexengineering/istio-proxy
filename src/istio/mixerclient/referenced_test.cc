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

#include "src/istio/mixerclient/referenced.h"

#include "include/istio/utils/attributes_builder.h"
#include "include/istio/utils/md5.h"

#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

using ::istio::mixer::v1::Attributes;
using ::google::protobuf::TextFormat;

namespace istio {
namespace mixerclient {
namespace {

const char kReferencedText[] = R"(
words: "bool-key"
words: "bytes-key"
words: "string-key"
words: "double-key"
words: "int-key"
words: "time-key"
words: "duration-key"
words: "string-map-key"
words: "User-Agent"
words: "If-Match"
attribute_matches {
  name: 9,
  condition: ABSENCE,
}
attribute_matches {
  name: 10,
  condition: ABSENCE,
}
attribute_matches {
  name: -1,
  condition: EXACT,
}
attribute_matches {
  name: -2,
  condition: EXACT,
}
attribute_matches {
  name: -3,
  condition: EXACT,
}
attribute_matches {
  name: -4,
  condition: EXACT,
}
attribute_matches {
  name: -5,
  condition: EXACT,
}
attribute_matches {
  name: -6,
  condition: EXACT,
}
attribute_matches {
  name: -7,
  condition: EXACT,
}
attribute_matches {
  name: -8,
  map_key: -10,
  condition: EXACT,
}
attribute_matches {
  name: -8,
  map_key: -9,
  condition: ABSENCE,
}
)";

const char kAttributesText[] = R"(
attributes {
   key: "string-map-key"
   value {
     string_map_value {
       entries {
         key: "User-Agent"
         value: "chrome60"
       }
       entries {
         key: "path"
         value: "/books"
       }
     }
   }
}
)";

// Global index (positive) is too big
const char kReferencedFailText1[] = R"(
attribute_matches {
  name: 10000,
  condition: EXACT,
}
)";

// Per message index (negative) is too big
const char kReferencedFailText2[] = R"(
words: "bool-key"
words: "bytes-key"
attribute_matches {
  name: -10,
  condition: ABSENCE,
}
)";

TEST(ReferencedTest, FillSuccessTest) {
  ::istio::mixer::v1::ReferencedAttributes pb;
  ASSERT_TRUE(TextFormat::ParseFromString(kReferencedText, &pb));

  ::istio::mixer::v1::Attributes attrs;
  ASSERT_TRUE(TextFormat::ParseFromString(kAttributesText, &attrs));

  Referenced referenced;
  EXPECT_TRUE(referenced.Fill(attrs, pb));

  EXPECT_EQ(referenced.DebugString(),
            "Absence-keys: string-map-key[User-Agent], target.name, "
            "target.service, Exact-keys: bool-key, bytes-key, double-key, "
            "duration-key, int-key, string-key, string-map-key[If-Match], "
            "time-key, ");

  EXPECT_EQ(utils::MD5::DebugString(referenced.Hash()),
            "602d5bbd45b623c3560d2bdb6104f3ab");
}

TEST(ReferencedTest, FillFail1Test) {
  ::istio::mixer::v1::ReferencedAttributes pb;
  ASSERT_TRUE(TextFormat::ParseFromString(kReferencedFailText1, &pb));

  ::istio::mixer::v1::Attributes attrs;
  Referenced referenced;
  EXPECT_FALSE(referenced.Fill(attrs, pb));
}

TEST(ReferencedTest, FillFail2Test) {
  ::istio::mixer::v1::ReferencedAttributes pb;
  ASSERT_TRUE(TextFormat::ParseFromString(kReferencedFailText2, &pb));
  ::istio::mixer::v1::Attributes attrs;

  Referenced referenced;
  EXPECT_FALSE(referenced.Fill(attrs, pb));
}

TEST(ReferencedTest, NegativeSignature1Test) {
  ::istio::mixer::v1::ReferencedAttributes pb;
  ASSERT_TRUE(TextFormat::ParseFromString(kReferencedText, &pb));
  ::istio::mixer::v1::Attributes attrs;
  ASSERT_TRUE(TextFormat::ParseFromString(kAttributesText, &attrs));
  Referenced referenced;
  EXPECT_TRUE(referenced.Fill(attrs, pb));

  std::string signature;

  Attributes attributes1;
  // "target.service" should be absence.
  utils::AttributesBuilder(&attributes1).AddString("target.service", "foo");
  EXPECT_FALSE(referenced.Signature(attributes1, "", &signature));

  Attributes attributes2;
  // many keys should exist.
  utils::AttributesBuilder(&attributes2).AddString("bytes-key", "foo");
  EXPECT_FALSE(referenced.Signature(attributes2, "", &signature));
}

TEST(ReferencedTest, OKSignature1Test) {
  ::istio::mixer::v1::ReferencedAttributes pb;
  ASSERT_TRUE(TextFormat::ParseFromString(kReferencedText, &pb));

  Attributes attributes;
  utils::AttributesBuilder builder(&attributes);
  builder.AddString("string-key", "this is a string value");
  builder.AddBytes("bytes-key", "this is a bytes value");
  builder.AddDouble("double-key", 99.9);
  builder.AddInt64("int-key", 35);
  builder.AddBool("bool-key", true);

  std::chrono::time_point<std::chrono::system_clock> time0;
  std::chrono::seconds secs(5);
  builder.AddTimestamp("time-key", time0);
  builder.AddDuration(
      "duration-key",
      std::chrono::duration_cast<std::chrono::nanoseconds>(secs));

  std::map<std::string, std::string> string_map = {{"If-Match", "value1"},
                                                   {"key2", "value2"}};
  builder.AddStringMap("string-map-key", std::move(string_map));

  Referenced referenced;
  EXPECT_TRUE(referenced.Fill(attributes, pb));

  std::string signature;
  EXPECT_TRUE(referenced.Signature(attributes, "extra", &signature));

  EXPECT_EQ(utils::MD5::DebugString(signature),
            "751b028b2e2c230ef9c4e59ac556ca04");
}

}  // namespace
}  // namespace mixerclient
}  // namespace istio
