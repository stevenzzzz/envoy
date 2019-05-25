#pragma once

#include <typeinfo>

#include "envoy/api/v2/srds.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/router/router.h"
#include "envoy/router/scopes.h"
#include "envoy/thread_local/thread_local.h"

#include "common/router/config_impl.h"
#include "common/router/scoped_config_manager.h"

namespace Envoy {
namespace Router {

using envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes;

/**
 * Scope key fragment base class.
 */
class ScopeKeyFragmentBase {
 public:
  bool operator ==(const ScopeKeyFragmentBase& other)const {
    if(typeid(*this) == typeid(other)){
      return equals(other);
    }
    return false;
  }
  virtual ~ScopeKeyFragmentBase() = default;
 private:
  // Returns true if the two value equals else false.
  virtual bool equals (const ScopeKeyFragmentBase&) const PURE;
};
typedef std::vector<std::unique_ptr<ScopeKeyFragmentBase>> ScopeKey;

// String fragment.
class StringKeyFragment: public ScopeKeyFragmentBase{
 public:
  explicit StringKeyFragment(absl::string_view value):value_(value){}

 private:
  bool equals (const ScopeKeyFragmentBase& other) const override{
    return value_ == dynamic_cast<const StringKeyFragment&>(other).value_;
  }

  std::string value_;
};

/**
 * Base class for fragment builders.
 */
class FragmentBuilderBase{
 public:
  explicit FragmentBuilderBase(ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config):config_(std::move(config)){}
  virtual ~FragmentBuilderBase() = default;

  // Returns a fragment if the fragment rule applies, a nullptr indicates no fragment could be generated from the headers.
  virtual std::unique_ptr<ScopeKeyFragmentBase> computeFragment(const Http::HeaderMap& headers) const PURE;

 protected:
  const ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config_;
};

class HeaderValueExtractorImpl: public FragmentBuilderBase {
 public:
  explicit HeaderValueExtractorImpl(ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config):FragmentBuilderBase(config),header_value_extractor_config_(config.header_value_extractor()) {
    ASSERT(config.type_case()== ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHeaderValueExtractor, "header_value_extractor is not set.");
  }

  std::unique_ptr<ScopeKeyFragmentBase> computeFragment(const Http::HeaderMap& headers) const override;
 private:
  const ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor& header_value_extractor_config_;
};


/**
 * Base class for ScopeKeyBuilder impls.
 */
class ScopeKeyBuilderBase{
 public:
  explicit ScopeKeyBuilderBase(ScopedRoutes::ScopeKeyBuilder config):config_(std::move(config)) {  }
  virtual ~ScopeKeyBuilderBase() = default;

  virtual const ScopeKey computeScopeKey(const Http::HeaderMap& headers) const PURE;

 protected:
  ScopedRoutes::ScopeKeyBuilder config_;
};

class ScopeKeyBuilderImpl: public ScopeKeyBuilderBase {
 public:
  explicit ScopeKeyBuilderImpl(ScopedRoutes::ScopeKeyBuilder config);

  const ScopeKey computeScopeKey(const Http::HeaderMap& headers) const override;
 private:
    std::vector<std::unique_ptr<FragmentBuilderBase>> fragment_builders_;
};

class ScopedConfigImpl : public ScopedConfig{
 public:
  explicit ScopedConfigImpl(ScopedRoutes::ScopeKeyBuilder config):scope_key_builder_(config){}

  // Envoy::Router::ScopedConfig
  Router::ConfigConstSharedPtr getRouteConfig(const Http::HeaderMap& headers) const override;

  void addOrUpdateRoutingScope(const ScopedRouteInfoConstSharedPtr& scoped_route_info);
  void removeRoutingScope(const std::string& scope_name);

 private:
  ScopeKeyBuilderImpl scope_key_builder_;
  absl::flat_hash_map<std::string, RouteConfiguration> scoped_route_config;
};

typedef const std::shared_ptr<ScopedConfigImpl> ScopedConfigImplConstSharedPtr;

/**
 * TODO(AndresGuedez): implement scoped routing logic.
 *
 * Each Envoy worker is assigned an instance of this type. When config updates are received,
 * addOrUpdateRoutingScope() and removeRoutingScope() are called to update the set of scoped routes.
 *
 * ConnectionManagerImpl::refreshCachedRoute() will call getRouteConfig() to obtain the
 * Router::ConfigConstSharedPtr to use for route selection.
 */
class ThreadLocalScopedConfigImpl : public ScopedConfig, public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalScopedConfigImpl(ScopedConfigImplConstSharedPtr config):config_(std::move(config)){}

// Envoy::Router::ScopedConfig
  Router::ConfigConstSharedPtr getRouteConfig(const Http::HeaderMap& headers) const override{
    return config_->getRouteConfig(headers);
  }

private:
  ScopedConfigImplConstSharedPtr config_;
};

/**
 * A NULL implementation of the scoped routing configuration.
 */
class NullScopedConfigImpl : public ScopedConfig {
public:
  Router::ConfigConstSharedPtr getRouteConfig(const Http::HeaderMap&) const override {
    return std::make_shared<const NullConfigImpl>();
  }
};

} // namespace Router
} // namespace Envoy
