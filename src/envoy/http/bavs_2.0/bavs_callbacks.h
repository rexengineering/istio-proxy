#pragma once
#include <iostream>
#include <stdio.h>
#include <map>

#include "envoy/http/filter.h"
#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"
#include "envoy/http/async_client.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"
#include "common/common/random_generator.h"
#include "bavs_20.h"

#include "src/envoy/http/bavs_2.0/newbavs.pb.h"


// void notifyFlowd(Buffer::Instance& data, Http::RequestHeaderMapImpl& hdrs,
//                  std::string msg, Upstream::ClusterManager& cm, BavsFilterConfig& config) {
//     std::string cluster = config->flowdCluster();
//     auto* client = cm.httpAsyncClientForCluster(cluster);
//     if (!client) {
//         std::cout << "Too bad, couldn't find flowd cluster" << std::endl;
//         return;
//     }

//     hdrs.setCopy(Http::LowerCaseString("x-rexflow-error-msg"), msg);
//     stream = client->start()


//     std::cout << "call to flowd" << std::endl;
// }

namespace Envoy {
namespace Http {



} // namespace Http
} // namespace Envoy