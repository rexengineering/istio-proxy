load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_cc_test",
    "envoy_proto_library",
)

envoy_package()

envoy_proto_library(
    name = "newbavs_proto",
    srcs = ["newbavs.proto"],
)

envoy_cc_library(
    name = "newbavs_lib",
    srcs = ["bavs_20.cc"],
    hdrs = ["bavs_20.h"],
    repository = "@envoy",
    deps = [
        ":newbavs_proto_cc_proto",
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
    name = "bavs_2_config",
    srcs = ["bavs_config.cc"],
    repository = "@envoy",
    deps = [
        ":newbavs_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy//include/envoy/server:filter_config_interface",
        "@envoy//include/envoy/registry",
        "@envoy//source/exe:envoy_common_lib",
    ],
)
