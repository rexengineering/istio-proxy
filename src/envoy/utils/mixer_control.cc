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

#include "src/envoy/utils/mixer_control.h"
#include "src/envoy/utils/grpc_transport.h"

using ::istio::mixerclient::Statistics;

namespace Envoy {
namespace Utils {
namespace {

// A class to wrap envoy timer for mixer client timer.
class EnvoyTimer : public ::istio::mixerclient::Timer {
 public:
  EnvoyTimer(Event::TimerPtr timer) : timer_(std::move(timer)) {}

  void Stop() override { timer_->disableTimer(); }
  void Start(int interval_ms) override {
    timer_->enableTimer(std::chrono::milliseconds(interval_ms));
  }

 private:
  Event::TimerPtr timer_;
};

}  // namespace

// Create all environment functions for mixerclient
void CreateEnvironment(Upstream::ClusterManager& cm,
                       Event::Dispatcher& dispatcher,
                       Runtime::RandomGenerator& random,
                       const std::string& check_cluster,
                       const std::string& report_cluster,
                       ::istio::mixerclient::Environment* env) {
  env->check_transport = CheckTransport::GetFunc(cm, check_cluster, nullptr);
  env->report_transport = ReportTransport::GetFunc(cm, report_cluster);

  env->timer_create_func = [&dispatcher](std::function<void()> timer_cb)
      -> std::unique_ptr<::istio::mixerclient::Timer> {
        return std::unique_ptr<::istio::mixerclient::Timer>(
            new EnvoyTimer(dispatcher.createTimer(timer_cb)));
      };

  env->uuid_generate_func = [&random]() -> std::string {
    return random.uuid();
  };
}

}  // namespace Utils
}  // namespace Envoy
