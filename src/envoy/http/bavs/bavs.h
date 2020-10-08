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


namespace Envoy {
namespace Http {

#define BAVS_CLUSTER "outbound|9080||svc-two.default.svc.cluster.local"
//#define BAVS_HOST "http://svc-two:9881"
#define BAVS_HOST "http://bavs-host:9881"

class BavsFilter : public PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
private:
    void httpCallAtOnce(std::string, int);
    // void saveResponseHeaders(Http::ResponseHeaderMap&);
    Upstream::ClusterManager& cluster_manager_;
    bool is_workflow_;
    int decisionpoint_id_;
    bool successful_response_;
    std::string req_cb_key_;

public:
    BavsFilter(Upstream::ClusterManager& cluster_manager) 
    : cluster_manager_(cluster_manager), is_workflow_(false), successful_response_(true) {};

    FilterDataStatus decodeData(Buffer::Instance&, bool);
    FilterDataStatus encodeData(Buffer::Instance&, bool);
    FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool);
    FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool);
};

} // namespace Http
} // namespace Envoy
