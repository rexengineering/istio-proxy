load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_cc_test",
    "envoy_proto_library",
)

envoy_package()

envoy_proto_library(
    name = "bavs_proto",
    srcs = ["bavs.proto"],
)

envoy_cc_library(
    name = "bavs_lib",
    srcs = ["bavs.cc"],
    hdrs = ["bavs.h"],
    repository = "@envoy",
    deps = [
        ":bavs_proto_cc_proto",
        "@envoy//source/exe:envoy_common_lib",
        "@envoy//include/envoy/http:filter_interface",
        "@envoy//include/envoy/registry",
        "@envoy//include/envoy/server:filter_config_interface",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/http:headers_lib",
        "@envoy//source/common/http:message_lib",
        "@envoy//include/envoy/http:async_client_interface",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "bavs_config",
    srcs = ["config.cc"],
    repository = "@envoy",
    deps = [
        ":bavs_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy//include/envoy/server:filter_config_interface",
        "@envoy//include/envoy/registry",
        "@envoy//source/exe:envoy_common_lib",
    ],
)

envoy_cc_test(
    name = "bavs_filter_test",
    srcs = ["bavs_test.cc"],
    repository = "@envoy",
    deps = [
        ":bavs",
        "@envoy//test/integration:http_integration_lib",
        "@envoy//test/integration:integration_lib",
    ],
)
