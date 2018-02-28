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

#include "src/envoy/tcp/mixer/control.h"
#include "src/envoy/utils/mixer_control.h"

using ::istio::mixerclient::Statistics;

namespace Envoy {
namespace Tcp {
namespace Mixer {

Control::Control(const Config& config, Upstream::ClusterManager& cm,
                 Event::Dispatcher& dispatcher,
                 Runtime::RandomGenerator& random,
                 Utils::MixerFilterStats& stats, const std::string& uuid)
    : config_(config),
      dispatcher_(dispatcher),
      stats_obj_(dispatcher, stats,
                 config_.config_pb().transport().stats_update_interval(),
                 [this](Statistics* stat) -> bool { return GetStats(stat); }),
      uuid_(uuid) {
  ::istio::control::tcp::Controller::Options options(config_.config_pb());

  Utils::CreateEnvironment(cm, dispatcher, random, config_.check_cluster(),
                           config_.report_cluster(), &options.env);

  controller_ = ::istio::control::tcp::Controller::Create(options);
}

// Call controller to get statistics.
bool Control::GetStats(Statistics* stat) {
  if (!controller_) {
    return false;
  }
  controller_->GetStatistics(stat);
  return true;
}

}  // namespace Mixer
}  // namespace Tcp
}  // namespace Envoy
