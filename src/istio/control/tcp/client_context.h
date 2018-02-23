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

#ifndef ISTIO_CONTROL_TCP_CLIENT_CONTEXT_H
#define ISTIO_CONTROL_TCP_CLIENT_CONTEXT_H

#include "include/istio/control/tcp/controller.h"
#include "include/istio/quota_config/config_parser.h"
#include "src/istio/control/client_context_base.h"
#include "src/istio/control/request_context.h"

namespace istio {
namespace control {
namespace tcp {

// The global context object to hold:
// * the tcp client config
// * the mixer client object to call Check/Report with cache.
class ClientContext : public ClientContextBase {
 public:
  ClientContext(const Controller::Options& data)
      : ClientContextBase(data.config.transport(), data.env),
        config_(data.config) {
    BuildQuotaParser();
  }

  // A constructor for unit-test to pass in a mock mixer_client
  ClientContext(
      std::unique_ptr<::istio::mixerclient::MixerClient> mixer_client,
      const ::istio::mixer::v1::config::client::TcpClientConfig& config)
      : ClientContextBase(std::move(mixer_client)), config_(config) {
    BuildQuotaParser();
  }

  // Add static mixer attributes.
  void AddStaticAttributes(RequestContext* request) const {
    if (config_.has_mixer_attributes()) {
      request->attributes.MergeFrom(config_.mixer_attributes());
    }
  }

  // Add quota requirements from quota configs.
  void AddQuotas(RequestContext* request) const {
    if (quota_parser_) {
      quota_parser_->GetRequirements(request->attributes, &request->quotas);
    }
  }

  bool enable_mixer_check() const { return !config_.disable_check_calls(); }
  bool enable_mixer_report() const { return !config_.disable_report_calls(); }

 private:
  // If there is quota config, build quota parser.
  void BuildQuotaParser() {
    if (config_.has_connection_quota_spec()) {
      quota_parser_ = ::istio::quota_config::ConfigParser::Create(
          config_.connection_quota_spec());
    }
  }
  // The mixer client config.
  const ::istio::mixer::v1::config::client::TcpClientConfig& config_;

  // The quota parser.
  std::unique_ptr<::istio::quota_config::ConfigParser> quota_parser_;
};

}  // namespace tcp
}  // namespace control
}  // namespace istio

#endif  // ISTIO_CONTROL_TCP_CLIENT_CONTEXT_H
