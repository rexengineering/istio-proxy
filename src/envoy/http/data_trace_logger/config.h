#pragma once

#include <string>


#include "extensions/filters/http/common/pass_through_filter.h"
#include "common/common/base64.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "common/protobuf/protobuf.h"

#include "config.h"
#include "data_trace_logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

/**
 * Config registration for http filters that have empty configuration blocks.
 * The boiler plate instantiation functions (createFilterFactory, createFilterFactoryFromProto,
 * and createEmptyConfigProto) are implemented here. Users of this class have to implement
 * the createFilter function that instantiates the actual filter.
 */
class EmptyHttpFilterConfig : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  virtual Http::FilterFactoryCb createFilter(const std::string& stat_prefix,
                                             Server::Configuration::FactoryContext& context) PURE;

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilter(stat_prefix, context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return name_; }

protected:
  EmptyHttpFilterConfig(const std::string& name) : name_(name) {}

private:
  const std::string name_;
};


/**
 * I think this class is necessary to help tell Envoy where to find our filter.
 */
class DataTraceLoggerConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
    DataTraceLoggerConfig() : EmptyHttpFilterConfig("data_trace_logger") {}

    Http::FilterFactoryCb createFilter(const std::string&, Server::Configuration::FactoryContext& context) {
        return [&context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
            callbacks.addStreamFilter(std::make_shared<Envoy::Http::DataTraceLogger>(context.clusterManager()));
        };
    }
};


static Registry::RegisterFactory<DataTraceLoggerConfig, DataTraceLoggerConfig::NamedHttpFilterConfigFactory>
    register_;

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
