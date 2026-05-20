#include "wemo_bridge/wemo_adapter_stub.h"

namespace wemo_bridge {

std::vector<WemoDevice> WemoAdapterStub::Discover()
{
    return {};
}

bool WemoAdapterStub::SetOnOff(const std::string &, bool)
{
    return false;
}

bool WemoAdapterStub::SetLevelPercent(const std::string &, uint8_t)
{
    return false;
}

} // namespace wemo_bridge
