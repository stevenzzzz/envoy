#pragma once

#include "source/common/common/thread.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Stats {

/**
 * Lazy-initialization wrapper for StatsStructType, intended for deferred instantiation of a block
 * of stats that might not be needed in a given Envoy process.
 *
 * This class is thread-safe -- instantiations can occur on multiple concurrent threads.
 */
template <typename StatsStructType> class LazyInit {
public:
  // Capture the stat names object and the scope with a ctor, that can be used to instantiate a
  // StatsStructType object later.
  // Caller should make sure scope and stat_names outlive this object.
  LazyInit(Stats::Scope& scope, const typename StatsStructType::StatNameType& stat_names)
      : ctor_([&scope, &stat_names]() -> StatsStructType* {
          return new StatsStructType(stat_names, scope);
        }) {
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_stats_lazyinit")) {
      internal_stats_.get(ctor_);
    }
  }
  // Helper operators to get-or-create and return the StatsStructType object.
  inline StatsStructType* operator->() { return internal_stats_.get(ctor_); }
  inline StatsStructType& operator*() { return *internal_stats_.get(ctor_); }

private:
  // TODO(stevenzzzz, jmarantz): Clean up this ctor_ by moving ownership to AtomicPtr, and drop it
  // when the nested object is created.
  std::function<StatsStructType*()> ctor_;
  Thread::AtomicPtr<StatsStructType, Thread::AtomicPtrAllocMode::DeleteOnDestruct>
      internal_stats_{};
};

} // namespace Stats
} // namespace Envoy
