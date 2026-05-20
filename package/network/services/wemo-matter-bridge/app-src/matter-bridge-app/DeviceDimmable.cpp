#include "DeviceDimmable.h"

#include <platform/CHIPDeviceLayer.h>

DeviceDimmable::DeviceDimmable(const char * szDeviceName, std::string szLocation) : DeviceOnOff(szDeviceName, szLocation)
{
    mLevel = 0;
}

uint8_t DeviceDimmable::GetLevel()
{
    return mLevel;
}

void DeviceDimmable::SetLevel(uint8_t aLevel)
{
    bool changed = aLevel != mLevel;
    mLevel       = aLevel;

    ChipLogProgress(DeviceLayer, "Device[%s]: Level=%u", mName, mLevel);

    if (changed && mChanged_CB)
    {
        mChanged_CB(this, kChanged_Level);
    }
}

void DeviceDimmable::SetChangeCallback(DeviceCallback_fn aChanged_CB)
{
    mChanged_CB = aChanged_CB;

    // DeviceOnOff::SetOnOff() calls its own mChanged_CB directly (not through
    // the virtual HandleDeviceChange).  Install a bridge so OnOff changes
    // propagate through our dimmable callback.
    DeviceOnOff::SetChangeCallback([this](DeviceOnOff *, DeviceOnOff::Changed_t mask) {
        if (mChanged_CB)
        {
            mChanged_CB(this, (DeviceDimmable::Changed_t) mask);
        }
    });
}

void DeviceDimmable::HandleDeviceChange(Device * device, Device::Changed_t changeMask)
{
    if (mChanged_CB)
    {
        mChanged_CB(this, (DeviceDimmable::Changed_t) changeMask);
    }
}
