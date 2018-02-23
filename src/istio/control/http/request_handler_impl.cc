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

#include "src/istio/control/http/request_handler_impl.h"
#include "src/istio/control/http/attributes_builder.h"

using ::google::protobuf::util::Status;
using ::istio::mixerclient::CancelFunc;
using ::istio::mixerclient::TransportCheckFunc;
using ::istio::mixerclient::DoneFunc;
using ::istio::quota_config::Requirement;

namespace istio {
namespace control {
namespace http {

RequestHandlerImpl::RequestHandlerImpl(
    std::shared_ptr<ServiceContext> service_context)
    : service_context_(service_context) {}

void RequestHandlerImpl::ExtractRequestAttributes(CheckData* check_data) {
  if (service_context_->enable_mixer_check() ||
      service_context_->enable_mixer_report()) {
    service_context_->AddStaticAttributes(&request_context_);

    AttributesBuilder builder(&request_context_);
    builder.ExtractForwardedAttributes(check_data);
    builder.ExtractCheckAttributes(check_data);

    service_context_->AddApiAttributes(check_data, &request_context_);
  }
}

CancelFunc RequestHandlerImpl::Check(CheckData* check_data,
                                     HeaderUpdate* header_update,
                                     TransportCheckFunc transport,
                                     DoneFunc on_done) {
  ExtractRequestAttributes(check_data);

  if (service_context_->client_context()->config().has_forward_attributes()) {
    AttributesBuilder::ForwardAttributes(
        service_context_->client_context()->config().forward_attributes(),
        header_update);
  } else {
    header_update->RemoveIstioAttributes();
  }

  if (!service_context_->enable_mixer_check()) {
    on_done(Status::OK);
    return nullptr;
  }

  service_context_->AddQuotas(&request_context_);

  return service_context_->client_context()->SendCheck(transport, on_done,
                                                       &request_context_);
}

// Make remote report call.
void RequestHandlerImpl::Report(ReportData* report_data) {
  if (!service_context_->enable_mixer_report()) {
    return;
  }
  AttributesBuilder builder(&request_context_);
  builder.ExtractReportAttributes(report_data);

  service_context_->client_context()->SendReport(request_context_);
}

}  // namespace http
}  // namespace control
}  // namespace istio
