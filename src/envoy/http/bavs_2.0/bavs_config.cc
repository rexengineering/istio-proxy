#include <string>

#include "extensions/filters/http/router/config.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"


#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "common/protobuf/protobuf.h"

#include "bavs_20.h"
#include "src/envoy/http/bavs_2.0/newbavs.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

namespace Config = Server::Configuration;

class HttpBavsFilter20ConfigFactory : public Config::NamedHttpFilterConfigFactory {
public:
  Http::FilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                     const std::string&,
                                                     Config::FactoryContext& context) override {
    std::string proto_config_json;
    google::protobuf::util::JsonPrintOptions opts;
    opts.add_whitespace = true;
    opts.always_print_primitive_fields = true;
    opts.preserve_proto_field_names = true;
    google::protobuf::util::MessageToJsonString(proto_config, &proto_config_json, opts);
    std::cout << proto_config_json << std::endl;
    return createFilter(
      // Envoy::MessageUtil::downcastAndValidate<const bavs::BAVSFilter&>(
      //   proto_config, context.messageValidationVisitor()),
      context
    );
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new newbavs::NewBAVSFilter()};
  }

  std::string name() const override { return "newbavs"; }

private:
  // Http::FilterFactoryCb createFilter(const bavs::BAVSFilter& proto_config, Server::Configuration::FactoryContext& context) {
  Http::FilterFactoryCb createFilter(Server::Configuration::FactoryContext& context) {
    return [&context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      // auto filter = new Http::BavsFilter(config, context.clusterManager());
      auto filter = new Http::BavsFilter20(context.clusterManager());
      callbacks.addStreamFilter(Http::StreamFilterSharedPtr(filter));
    };
  }

};

static Registry::RegisterFactory<HttpBavsFilter20ConfigFactory, Config::NamedHttpFilterConfigFactory>
    register_;

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

