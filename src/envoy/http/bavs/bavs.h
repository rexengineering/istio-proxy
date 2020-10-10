#pragma once
#include <iostream>
#include <stdio.h>

#include "envoy/http/filter.h"
#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"
#include "envoy/http/async_client.h"
#include "common/http/message_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"

#include "src/envoy/http/bavs/bavs.pb.h"

namespace Envoy {
namespace Http {

#define BAVS_CLUSTER "outbound|9080||svc-two.default.svc.cluster.local"
//#define BAVS_HOST "http://svc-two:9881"
#define BAVS_HOST "http://bavs-host:9881"

class UpstreamConfig {
public:
    UpstreamConfig(const bavs::Upstream& proto_config) :
        name_(proto_config.name()), cluster_(proto_config.cluster()),
        host_(proto_config.host()), port_(proto_config.port()),
        path_(proto_config.path()), method_(proto_config.method()) {}

    inline const std::string& name() const { return name_; }
    inline const std::string& cluster() const { return cluster_; }
    inline const std::string& host() const { return host_; }
    inline int port() const { return port_; }
    inline const std::string& path() const { return path_; }
    inline const std::string& method() const { return method_; }

private:
    std::string name_;
    std::string cluster_;
    std::string host_;
    int port_;
    std::string path_;
    std::string method_;
};

using UpstreamConfigSharedPtr = std::shared_ptr<UpstreamConfig>;

class BavsFilterConfig {
public:
    BavsFilterConfig(const bavs::BAVSFilter& proto_config);

    const std::vector<const UpstreamConfigSharedPtr>& forwards() { return forwards_; }

private:
    std::vector<const UpstreamConfigSharedPtr> forwards_;
};

using BavsFilterConfigSharedPtr = std::shared_ptr<BavsFilterConfig>;

class BavsFilter : public PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
private:
    void httpCallAtOnce(std::string, int);
    // void saveResponseHeaders(Http::ResponseHeaderMap&);
    const BavsFilterConfigSharedPtr config_;
    Upstream::ClusterManager& cluster_manager_;
    bool is_workflow_;
    bool successful_response_;
    std::vector<std::string> req_cb_keys;
    std::string flow_id_;

public:
    BavsFilter(BavsFilterConfigSharedPtr config, Upstream::ClusterManager& cluster_manager)
    : config_(config), cluster_manager_(cluster_manager), is_workflow_(false), successful_response_(true) {};

    FilterDataStatus decodeData(Buffer::Instance&, bool);
    FilterDataStatus encodeData(Buffer::Instance&, bool);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool);
};

} // namespace Http
} // namespace Envoy
