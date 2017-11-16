# Copyright 2016 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################
#

load(
    "//src/envoy/mixer:repositories.bzl",
    "mixer_client_repositories",
)

mixer_client_repositories()

load(
    "@mixerclient_git//:repositories.bzl",
    "mixerapi_repositories",
)

mixerapi_repositories()

bind(
    name = "boringssl_crypto",
    actual = "//external:ssl",
)

ENVOY_SHA = "e593fedc3232fbb694f3ec985567a2c7dff05212"  # Oct 31, 2017

http_archive(
    name = "envoy",
    strip_prefix = "envoy-" + ENVOY_SHA,
    url = "https://github.com/envoyproxy/envoy/archive/" + ENVOY_SHA + ".zip",
)

load("@envoy//bazel:repositories.bzl", "envoy_dependencies")

envoy_dependencies(repository="@envoy", skip_targets=["io_bazel_rules_go"])

load("@envoy//bazel:cc_configure.bzl", "cc_configure")

cc_configure()

load("@envoy_api//bazel:repositories.bzl", "api_dependencies")

api_dependencies()

git_repository(
    name = "io_bazel_rules_go",
    commit = "9cf23e2aab101f86e4f51d8c5e0f14c012c2161c",  # Oct 12, 2017 (Add `build_external` option to `go_repository`)
    remote = "https://github.com/bazelbuild/rules_go.git",
)

load("@mixerapi_git//:api_dependencies.bzl", "mixer_api_for_proxy_dependencies")
mixer_api_for_proxy_dependencies()

MIXER = "0e4f2e772003adfc0387cdbd5bab885fa026855f"

# TODO: to use official Mixer in istio/istio.
git_repository(
    name = "com_github_istio_mixer",
    commit = MIXER,
    remote = "https://github.com/qiwzhang/mixer",
)

load("@com_github_istio_mixer//test:repositories.bzl", "mixer_test_repositories")
mixer_test_repositories()
