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

#include "bavs.h"
#include "src/envoy/http/bavs/bavs.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

namespace Config = Server::Configuration;

class HttpBavsFilterConfigFactory : public Config::NamedHttpFilterConfigFactory {
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
      Envoy::MessageUtil::downcastAndValidate<const bavs::BAVSFilter&>(
        proto_config, context.messageValidationVisitor()),
      context
    );
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new bavs::BAVSFilter()};
  }

  std::string name() const override { return "bavs_filter"; }

private:
  Http::FilterFactoryCb createFilter(const bavs::BAVSFilter& proto_config, Server::Configuration::FactoryContext& context) {
    Http::BavsFilterConfigSharedPtr config =
      std::make_shared<Http::BavsFilterConfig>(Http::BavsFilterConfig(proto_config, context.clusterManager()));

    return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      auto filter = new Http::BavsFilter(config);
      callbacks.addStreamFilter(Http::StreamFilterSharedPtr(filter));
    };
  }

};

static Registry::RegisterFactory<HttpBavsFilterConfigFactory, Config::NamedHttpFilterConfigFactory>
    register_;

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

