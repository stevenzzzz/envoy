#include "extensions/filters/network/thrift_proxy/protocol_impl.h"

#include <algorithm>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/byte_order.h"
#include "common/common/macros.h"

#include "extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/buffer_helper.h"
#include "extensions/filters/network/thrift_proxy/compact_protocol_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

bool AutoProtocolImpl::readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) {
  if (protocol_ == nullptr) {
    if (buffer.length() < 2) {
      return false;
    }

    uint16_t version = BufferHelper::peekU16(buffer);
    if (BinaryProtocolImpl::isMagic(version)) {
      setProtocol(std::make_unique<BinaryProtocolImpl>());
    } else if (CompactProtocolImpl::isMagic(version)) {
      setProtocol(std::make_unique<CompactProtocolImpl>());
    }

    if (!protocol_) {
      throw EnvoyException(
          fmt::format("unknown thrift auto protocol message start {:04x}", version));
    }
  }

  return protocol_->readMessageBegin(buffer, metadata);
}

bool AutoProtocolImpl::readMessageEnd(Buffer::Instance& buffer) {
  RELEASE_ASSERT(protocol_ != nullptr, "");
  return protocol_->readMessageEnd(buffer);
}

class AutoProtocolConfigFactory : public ProtocolFactoryBase<AutoProtocolImpl> {
public:
  AutoProtocolConfigFactory() : ProtocolFactoryBase(ProtocolNames::get().AUTO) {}
};

/**
 * Static registration for the auto protocol. @see RegisterFactory.
 */
static Registry::RegisterFactory<AutoProtocolConfigFactory, NamedProtocolConfigFactory> register_;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
