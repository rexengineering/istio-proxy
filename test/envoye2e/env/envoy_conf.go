// Copyright 2019 Istio Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package env

import (
	"fmt"
	"os"
	"strings"
	"text/template"
)

const envoyClientConfTemplYAML = `
admin:
  access_log_path: {{.ClientAccessLogPath}}
  address:
    socket_address:
      address: 127.0.0.1
      port_value: {{.Ports.ClientAdminPort}}
static_resources:
  clusters:
  - name: client
    connect_timeout: 5s
    type: STATIC
    hosts:
    - socket_address:
        address: 127.0.0.1
        port_value: {{.Ports.ClientToServerProxyPort}}
  - name: server
    connect_timeout: 5s
    type: STATIC
    hosts:
    - socket_address:
        address: 127.0.0.1
        port_value: {{.Ports.ProxyToServerProxyPort}}
  listeners:
  - name: app-to-client
    address:
      socket_address:
        address: 127.0.0.1
        port_value: {{.Ports.AppToClientProxyPort}}
    filter_chains:
    - filters:
      - name: envoy.http_connection_manager
        config:
          codec_type: AUTO
          stat_prefix: inbound_http
          access_log:
          - name: envoy.file_access_log
            config:
              path: {{.ClientAccessLogPath}}
          http_filters:
          - name: envoy.router
          route_config:
            name: app-to-client-route
            virtual_hosts:
            - name: app-to-client-route
              domains: ["*"]
              routes:
              - match:
                  prefix: /
                route:
                  cluster: client
                  timeout: 0s
  - name: client-to-proxy
    address:
      socket_address:
        address: 127.0.0.1
        port_value: {{.Ports.ClientToServerProxyPort}}
    filter_chains:
    - filters:
      - name: envoy.http_connection_manager
        config:
          codec_type: AUTO
          stat_prefix: outbound_http
          access_log:
          - name: envoy.file_access_log
            config:
              path: {{.ClientAccessLogPath}}
          http_filters:
          - name: envoy.router
          route_config:
            name: client-to-proxy-route
            virtual_hosts:
            - name: client-to-proxy-route
              domains: ["*"]
              routes:
              - match:
                  prefix: /
                route:
                  cluster: server
                  timeout: 0s
`

const envoyServerConfTemplYAML = `
admin:
  access_log_path: {{.ServerAccessLogPath}}
  address:
    socket_address:
      address: 127.0.0.1
      port_value: {{.Ports.ServerAdminPort}}
static_resources:
  clusters:
  - name: backend
    connect_timeout: 5s
    type: STATIC
    hosts:
    - socket_address:
        address: 127.0.0.1
        port_value: {{.Ports.BackendPort}}
  - name: server
    connect_timeout: 5s
    type: STATIC
    hosts:
    - socket_address:
        address: 127.0.0.1
        port_value: {{.Ports.ClientToAppProxyPort}}
  listeners:
  - name: proxy-to-server
    address:
      socket_address:
        address: 127.0.0.1
        port_value: {{.Ports.ProxyToServerProxyPort}}
    filter_chains:
    - filters:
      - name: envoy.http_connection_manager
        config:
          codec_type: AUTO
          stat_prefix: inbound_http
          access_log:
          - name: envoy.file_access_log
            config:
              path: {{.ServerAccessLogPath}}
          http_filters:
          - name: envoy.router
          route_config:
            name: proxy-to-server-route
            virtual_hosts:
            - name: proxy-to-server-route
              domains: ["*"]
              routes:
              - match:
                  prefix: /
                route:
                  cluster: server
                  timeout: 0s
  - name: client-to-app
    address:
      socket_address:
        address: 127.0.0.1
        port_value: {{.Ports.ClientToAppProxyPort}}
    filter_chains:
    - filters:
      - name: envoy.http_connection_manager
        config:
          codec_type: AUTO
          stat_prefix: outbound_http
          access_log:
          - name: envoy.file_access_log
            config:
              path: {{.ServerAccessLogPath}}
          http_filters:
          - name: envoy.router
          route_config:
            name: client-to-app-route
            virtual_hosts:
            - name: client-to-app-route
              domains: ["*"]
              routes:
              - match:
                  prefix: /
                route:
                  cluster: backend
                  timeout: 0s
`

// CreateEnvoyConf create envoy config.
func (s *TestSetup) CreateEnvoyConf(path, confTmpl  string) error {
	if s.stress {
		s.AccessLogPath = "/dev/null"
	}

	tmpl, err := template.New("test").Funcs(template.FuncMap{
		"indent": indent,
	}).Parse(confTmpl)
	if err != nil {
		return fmt.Errorf("failed to parse config template: %v", err)
	}
	tmpl.Funcs(template.FuncMap{})

	f, err := os.Create(path)
	if err != nil {
		return fmt.Errorf("failed to create file %v: %v", path, err)
	}
	defer func() {
		_ = f.Close()
	}()

	return tmpl.Execute(f, s)
}

func indent(n int, s string) string {
	pad := strings.Repeat(" ", n)
	return pad + strings.Replace(s, "\n", "\n"+pad, -1)
}
