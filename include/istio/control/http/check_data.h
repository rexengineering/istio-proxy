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

#ifndef ISTIO_CONTROL_HTTP_CHECK_DATA_H
#define ISTIO_CONTROL_HTTP_CHECK_DATA_H

#include <map>
#include <string>

#include "src/istio/authn/context.pb.h"

namespace istio {
namespace control {
namespace http {

// The interface to extract HTTP data for Mixer check.
// Implemented by the environment (Envoy) and used by the library.
class CheckData {
 public:
  virtual ~CheckData() {}

  // Find "x-istio-attributes" HTTP header.
  // If found, base64 decode its value,  pass it out
  virtual bool ExtractIstioAttributes(std::string *data) const = 0;

  // Get downstream tcp connection ip and port.
  virtual bool GetSourceIpPort(std::string *ip, int *port) const = 0;

  // If SSL is used, get peer SAN URI.
  virtual bool GetPeerPrincipal(std::string *user) const = 0;

  // If SSL is used, get local SAN URI.
  virtual bool GetLocalPrincipal(std::string *user) const = 0;

  // Get request HTTP headers
  virtual std::map<std::string, std::string> GetRequestHeaders() const = 0;

  // Returns true if connection is mutual TLS enabled.
  virtual bool IsMutualTLS() const = 0;

  // These headers are extracted into top level attributes.
  // This is for standard HTTP headers.  It supports both HTTP/1.1 and HTTP2
  // They can be retrieved at O(1) speed by environment (Envoy).
  // It is faster to use the map from GetRequestHeader() call.
  //
  enum HeaderType {
    HEADER_PATH = 0,
    HEADER_HOST,
    HEADER_SCHEME,
    HEADER_USER_AGENT,
    HEADER_METHOD,
    HEADER_REFERER,
    HEADER_CONTENT_TYPE,
  };
  virtual bool FindHeaderByType(HeaderType header_type,
                                std::string *value) const = 0;

  // A generic way to find any HTTP header.
  // This is for custom HTTP headers, such as x-api-key
  // Envoy platform requires "name" to be lower_case.
  virtual bool FindHeaderByName(const std::string &name,
                                std::string *value) const = 0;

  // Find query parameter by name.
  virtual bool FindQueryParameter(const std::string &name,
                                  std::string *value) const = 0;

  // Find Cookie header.
  virtual bool FindCookie(const std::string &name,
                          std::string *value) const = 0;

  // If the request has a JWT token and it is verified, get its payload as
  // string map, and return true. Otherwise return false.
  virtual bool GetJWTPayload(
      std::map<std::string, std::string> *payload) const = 0;

  // If the request has authentication result in header, parses data into the
  // output result; returns true if success. Otherwise, returns false.
  virtual bool GetAuthenticationResult(istio::authn::Result *result) const = 0;
};

// An interfact to update request HTTP headers with Istio attributes.
class HeaderUpdate {
 public:
  virtual ~HeaderUpdate() {}

  // Remove "x-istio-attributes" HTTP header.
  virtual void RemoveIstioAttributes() = 0;

  // Base64 encode data, and add it as "x-istio-attributes" HTTP header.
  virtual void AddIstioAttributes(const std::string &data) = 0;
};

}  // namespace http
}  // namespace control
}  // namespace istio

#endif  // ISTIO_CONTROL_HTTP_CHECK_DATA_H
