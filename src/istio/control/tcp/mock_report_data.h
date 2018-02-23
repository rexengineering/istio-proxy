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

#ifndef ISTIO_CONTROL_TCP_MOCK_REPORT_DATA_H
#define ISTIO_CONTROL_TCP_MOCK_REPORT_DATA_H

#include "gmock/gmock.h"
#include "include/istio/control/tcp/report_data.h"

namespace istio {
namespace control {
namespace tcp {

// The mock object for ReportData interface.
class MockReportData : public ReportData {
 public:
  MOCK_CONST_METHOD2(GetDestinationIpPort, bool(std::string* ip, int* port));
  MOCK_CONST_METHOD1(GetReportInfo, void(ReportInfo* info));
};

}  // namespace tcp
}  // namespace control
}  // namespace istio

#endif  // ISTIO_CONTROL_TCP_MOCK_REPORT_DATA_H
