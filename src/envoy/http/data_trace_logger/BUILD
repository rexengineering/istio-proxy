load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_cc_test",
)

envoy_package()

envoy_cc_library(
    name = "data_trace_logger",
    srcs = ["data_trace_logger.cc"],
    hdrs = ["data_trace_logger.h"],
    repository = "@envoy",
    deps = [
    	":data_trace_logger_config",
        "@envoy//source/common/upstream:cluster_manager_lib",
        "@envoy//source/exe:envoy_common_lib",
        "@envoy//include/envoy/http:filter_interface",
        "@envoy//include/envoy/registry",
        "@envoy//include/envoy/server:filter_config_interface",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "data_trace_logger_config",
    hdrs = [
    	 "config.h",
	 "data_trace_logger.h",
    ],
    srcs = ["config.cc"],
    repository = "@envoy",
    deps = [
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy//include/envoy/server:filter_config_interface",
        "@envoy//include/envoy/registry",
        "@envoy//source/exe:envoy_common_lib",
    ],
)

envoy_cc_test(
    name = "dtl_filter_test",
    srcs = ["data_trace_logger_test.cc"],
    repository = "@envoy",
    deps = [
        ":data_trace_logger",
        "@envoy//test/integration:http_integration_lib",
        "@envoy//test/integration:integration_lib",
    ],
)
