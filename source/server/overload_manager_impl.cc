#include "server/overload_manager_impl.h"

#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "server/resource_monitor_config_impl.h"

namespace Envoy {
namespace Server {

namespace {

class ThresholdTriggerImpl : public OverloadAction::Trigger {
public:
  ThresholdTriggerImpl(const envoy::config::overload::v2alpha::ThresholdTrigger& config)
      : threshold_(config.value()) {}

  bool updateValue(double value) {
    const bool fired = isFired();
    value_ = value;
    return fired != isFired();
  }

  bool isFired() const { return value_.has_value() && value_ >= threshold_; }

private:
  const double threshold_;
  absl::optional<double> value_;
};

} // namespace

OverloadAction::OverloadAction(const envoy::config::overload::v2alpha::OverloadAction& config) {
  for (const auto& trigger_config : config.triggers()) {
    TriggerPtr trigger;

    switch (trigger_config.trigger_oneof_case()) {
    case envoy::config::overload::v2alpha::Trigger::kThreshold:
      trigger = std::make_unique<ThresholdTriggerImpl>(trigger_config.threshold());
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }

    if (!triggers_.insert(std::make_pair(trigger_config.name(), std::move(trigger))).second) {
      throw EnvoyException(
          fmt::format("Duplicate trigger resource for overload action {}", config.name()));
    }
  }
}

bool OverloadAction::updateResourcePressure(const std::string& name, double pressure) {
  const bool active = isActive();

  auto it = triggers_.find(name);
  ASSERT(it != triggers_.end());
  if (it->second->updateValue(pressure)) {
    if (it->second->isFired()) {
      const auto result = fired_triggers_.insert(name);
      ASSERT(result.second);
    } else {
      const auto result = fired_triggers_.erase(name);
      ASSERT(result == 1);
    }
  }

  return active != isActive();
}

bool OverloadAction::isActive() const { return !fired_triggers_.empty(); }

OverloadManagerImpl::OverloadManagerImpl(
    Event::Dispatcher& dispatcher, const envoy::config::overload::v2alpha::OverloadManager& config)
    : started_(false), dispatcher_(dispatcher),
      refresh_interval_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, refresh_interval, 1000))) {
  Configuration::ResourceMonitorFactoryContextImpl context(dispatcher);
  for (const auto& resource : config.resource_monitors()) {
    const auto& name = resource.name();
    ENVOY_LOG(debug, "Adding resource monitor for {}", name);
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::ResourceMonitorFactory>(name);
    auto config = Config::Utility::translateToFactoryConfig(resource, factory);
    auto monitor = factory.createResourceMonitor(*config, context);

    auto result = resources_.emplace(std::piecewise_construct, std::forward_as_tuple(name),
                                     std::forward_as_tuple(name, std::move(monitor), *this));
    if (!result.second) {
      throw EnvoyException(fmt::format("Duplicate resource monitor {}", name));
    }
  }

  for (const auto& action : config.actions()) {
    const auto& name = action.name();
    ENVOY_LOG(debug, "Adding overload action {}", name);
    auto result = actions_.emplace(std::piecewise_construct, std::forward_as_tuple(name),
                                   std::forward_as_tuple(action));
    if (!result.second) {
      throw EnvoyException(fmt::format("Duplicate overload action {}", name));
    }

    for (const auto& trigger : action.triggers()) {
      const std::string resource = trigger.name();

      if (resources_.find(resource) == resources_.end()) {
        throw EnvoyException(
            fmt::format("Unknown trigger resource {} for overload action {}", resource, name));
      }

      resource_to_actions_.insert(std::make_pair(resource, name));
    }
  }
}

void OverloadManagerImpl::start() {
  ASSERT(!started_);
  started_ = true;
  timer_ = dispatcher_.createTimer([this]() -> void {
    for (auto& resource : resources_) {
      resource.second.update();
    }

    timer_->enableTimer(refresh_interval_);
  });
  timer_->enableTimer(refresh_interval_);
}

void OverloadManagerImpl::registerForAction(const std::string& action,
                                            Event::Dispatcher& dispatcher,
                                            OverloadActionCb callback) {
  ASSERT(!started_);

  if (actions_.find(action) == actions_.end()) {
    ENVOY_LOG(debug, "No overload action configured for {}.", action);
    return;
  }

  action_to_callbacks_.emplace(std::piecewise_construct, std::forward_as_tuple(action),
                               std::forward_as_tuple(dispatcher, callback));
}

void OverloadManagerImpl::updateResourcePressure(const std::string& resource, double pressure) {
  auto action_range = resource_to_actions_.equal_range(resource);
  std::for_each(action_range.first, action_range.second,
                [&](ResourceToActionMap::value_type& entry) {
                  const std::string& action = entry.second;
                  auto action_it = actions_.find(action);
                  ASSERT(action_it != actions_.end());
                  if (action_it->second.updateResourcePressure(resource, pressure)) {
                    const bool is_active = action_it->second.isActive();
                    const auto state =
                        is_active ? OverloadActionState::Active : OverloadActionState::Inactive;
                    ENVOY_LOG(info, "Overload action {} has become {}", action,
                              is_active ? "active" : "inactive");
                    auto callback_range = action_to_callbacks_.equal_range(action);
                    std::for_each(callback_range.first, callback_range.second,
                                  [&](ActionToCallbackMap::value_type& cb_entry) {
                                    auto& cb = cb_entry.second;
                                    cb.dispatcher_.post([&, state]() { cb.callback_(state); });
                                  });
                  }
                });
}

void OverloadManagerImpl::Resource::update() {
  if (!pending_update_) {
    pending_update_ = true;
    monitor_->updateResourceUsage(*this);
    return;
  }
  ENVOY_LOG(debug, "Skipping update for resource {} which has pending update", name_);
  // TODO(eziskind) add stat
}

void OverloadManagerImpl::Resource::onSuccess(const ResourceUsage& usage) {
  pending_update_ = false;
  manager_.updateResourcePressure(name_, usage.resource_pressure_);
}

void OverloadManagerImpl::Resource::onFailure(const EnvoyException& error) {
  pending_update_ = false;
  ENVOY_LOG(info, "Failed to update resource {}: {}", name_, error.what());

  // TODO(eziskind): add stat
}

} // namespace Server
} // namespace Envoy
