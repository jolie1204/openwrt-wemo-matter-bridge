#pragma once

#include "Device.h"

#include <functional>

class DeviceDimmable : public DeviceOnOff
{
public:
    enum Changed_t
    {
        kChanged_Level = kChanged_OnOff << 1,
    } Changed;

    DeviceDimmable(const char * szDeviceName, std::string szLocation);

    uint8_t GetLevel();
    void SetLevel(uint8_t aLevel);

    using DeviceCallback_fn = std::function<void(DeviceDimmable *, DeviceDimmable::Changed_t)>;
    void SetChangeCallback(DeviceCallback_fn aChanged_CB);

private:
    void HandleDeviceChange(Device * device, Device::Changed_t changeMask);

    uint8_t mLevel = 0;
    DeviceCallback_fn mChanged_CB;
};
