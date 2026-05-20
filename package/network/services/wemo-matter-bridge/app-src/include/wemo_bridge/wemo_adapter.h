#pragma once

#include <functional>
#include <string>
#include <vector>

#include "wemo_bridge/wemo_device.h"

namespace wemo_bridge {

struct WemoStateEvent
{
    int wemo_id;
    bool is_online;
    int state;  // 0=off, 1=on
    int level;  // 0-100 or -1
};

using StateEventCallback = std::function<void(const WemoStateEvent &)>;

class WemoAdapter
{
public:
    virtual ~WemoAdapter() = default;

    virtual std::vector<WemoDevice> Discover() = 0;
    virtual void Refresh() {}
    virtual bool SetOnOff(const std::string & udn, bool on) = 0;
    virtual bool SetLevelPercent(const std::string & udn, uint8_t percent) = 0;
    virtual void RegisterStateCallback(StateEventCallback cb) = 0;
};

} // namespace wemo_bridge
