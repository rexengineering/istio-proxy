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

#ifndef ISTIO_CONTROL_TCP_REQUEST_HANDLER_IMPL_H
#define ISTIO_CONTROL_TCP_REQUEST_HANDLER_IMPL_H

#include "include/istio/control/tcp/request_handler.h"
#include "src/istio/control/request_context.h"
#include "src/istio/control/tcp/client_context.h"

namespace istio {
namespace control {
namespace tcp {

// The class to implement RequestHandler interface.
class RequestHandlerImpl : public RequestHandler {
 public:
  RequestHandlerImpl(std::shared_ptr<ClientContext> client_context);

  // Make a Check call.
  ::istio::mixerclient::CancelFunc Check(
      CheckData* check_data, ::istio::mixerclient::DoneFunc on_done) override;

  // Make a Report call.
  // TODO(JimmyCYJ): Let TCP filter use
  // void Report(ReportData* report_data, bool is_final_report), and deprecate
  // this method.
  void Report(ReportData* report_data) override;

  // Make a Report call. If is_final_report is true, then report all attributes,
  // otherwise, report delta attributes.
  void Report(ReportData* report_data, bool is_final_report) override;

 private:
  // The request context object.
  RequestContext request_context_;

  // The client context object.
  std::shared_ptr<ClientContext> client_context_;

  // Records reported information in last Report() call. This is needed for
  // calculating delta information which will be sent in periodical report.
  // Delta information includes incremented sent bytes and received bytes
  // between last report and this report.
  ReportData::ReportInfo last_report_info_;
};

}  // namespace tcp
}  // namespace control
}  // namespace istio

#endif  // ISTIO_CONTROL_TCP_REQUEST_HANDLER_IMPL_H
