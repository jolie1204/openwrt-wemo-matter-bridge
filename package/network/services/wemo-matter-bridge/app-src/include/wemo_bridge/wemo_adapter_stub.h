#pragma once

#include "wemo_bridge/wemo_adapter.h"

namespace wemo_bridge {

class WemoAdapterStub final : public WemoAdapter
{
public:
    std::vector<WemoDevice> Discover() override;
    bool SetOnOff(const std::string & udn, bool on) override;
    bool SetLevelPercent(const std::string & udn, uint8_t percent) override;
};

} // namespace wemo_bridge
