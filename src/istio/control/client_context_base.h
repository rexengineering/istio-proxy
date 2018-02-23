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

#ifndef ISTIO_CONTROL_CLIENT_CONTEXT_BASE_H
#define ISTIO_CONTROL_CLIENT_CONTEXT_BASE_H

#include "include/istio/mixerclient/client.h"
#include "mixer/v1/config/client/client_config.pb.h"
#include "request_context.h"

namespace istio {
namespace control {

// The global context object to hold the mixer client object
// to call Check/Report with cache.
class ClientContextBase {
 public:
  ClientContextBase(
      const ::istio::mixer::v1::config::client::TransportConfig& config,
      const ::istio::mixerclient::Environment& env);

  // A constructor for unit-test to pass in a mock mixer_client
  ClientContextBase(
      std::unique_ptr<::istio::mixerclient::MixerClient> mixer_client)
      : mixer_client_(std::move(mixer_client)) {}
  // virtual destrutor
  virtual ~ClientContextBase() {}

  // Use mixer client object to make a Check call.
  ::istio::mixerclient::CancelFunc SendCheck(
      ::istio::mixerclient::TransportCheckFunc transport,
      ::istio::mixerclient::DoneFunc on_done, RequestContext* request);

  // Use mixer client object to make a Report call.
  void SendReport(const RequestContext& request);

  // Get statistics.
  void GetStatistics(::istio::mixerclient::Statistics* stat) const;

 private:
  // The mixer client object with check cache and report batch features.
  std::unique_ptr<::istio::mixerclient::MixerClient> mixer_client_;
};

}  // namespace control
}  // namespace istio

#endif  // ISTIO_CONTROL_CLIENT_CONTEXT_BASE_H
