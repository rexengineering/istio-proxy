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

#pragma once

#include "src/envoy/auth/jwt_authenticator.h"

#include "common/common/logger.h"
#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Http {

// The Envoy filter to process JWT auth.
class JwtVerificationFilter : public StreamDecoderFilter,
                              public Auth::JwtAuthenticator::Callbacks,
                              public Logger::Loggable<Logger::Id::http> {
 public:
  JwtVerificationFilter(Upstream::ClusterManager& cm,
                        Auth::JwtAuthStore& store);
  ~JwtVerificationFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool) override;
  FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  FilterTrailersStatus decodeTrailers(HeaderMap&) override;
  void setDecoderFilterCallbacks(
      StreamDecoderFilterCallbacks& callbacks) override;

 private:
  // the function for Auth::Authenticator::Callbacks interface.
  // To be called when its Verify() call is completed.
  void onDone(const Auth::Status& status);

  // The callback funcion.
  StreamDecoderFilterCallbacks* decoder_callbacks_;
  // The auth object.
  Auth::JwtAuthenticator jwt_auth_;

  // The state of the request
  enum State { Init, Calling, Responded, Complete };
  State state_ = Init;
  // Mark if request has been stopped.
  bool stopped_ = false;
};

}  // Http
}  // Envoy
