#include "server/config_validation/admin.h"

namespace Envoy {
namespace Server {

bool ValidationAdmin::addHandler(const std::string&, const std::string&, HandlerCb, bool, bool) {
  return false;
};

bool ValidationAdmin::removeHandler(const std::string&) { return false; };

const Network::Socket& ValidationAdmin::socket() { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; };

ConfigTracker& ValidationAdmin::getConfigTracker() { return config_tracker_; };

Http::Code ValidationAdmin::request(absl::string_view, absl::string_view, Http::HeaderMap&,
                                    std::string&) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

} // namespace Server
} // namespace Envoy
