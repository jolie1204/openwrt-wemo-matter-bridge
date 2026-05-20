#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "wemo_bridge/wemo_adapter.h"

namespace wemo_bridge {

class WemoAdapterOpenWemo final : public WemoAdapter
{
public:
    explicit WemoAdapterOpenWemo(std::string engine_socket);

    std::vector<WemoDevice> Discover() override;
    void Refresh() override;
    bool SetOnOff(const std::string & udn, bool on) override;
    bool SetLevelPercent(const std::string & udn, uint8_t percent) override;
    void RegisterStateCallback(StateEventCallback cb) override;

private:
    std::string mEngineSocket;
    mutable std::mutex mCacheMutex;
    std::unordered_map<std::string, int> mUdnToWemoId;
};

} // namespace wemo_bridge
