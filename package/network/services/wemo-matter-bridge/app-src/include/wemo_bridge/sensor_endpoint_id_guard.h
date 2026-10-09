#pragma once
#include <cstdint>
namespace wemo_bridge {
// Sensor reservations must never move the legacy light/plug ID allocator,
// including endpoint registration failure and early returns.
class SensorEndpointIdGuard {
public:
    SensorEndpointIdGuard(uint16_t &current, bool sensor) : mCurrent(current), mSaved(current), mSensor(sensor) {}
    ~SensorEndpointIdGuard() { if (mSensor) mCurrent = mSaved; }
    SensorEndpointIdGuard(const SensorEndpointIdGuard &) = delete;
    SensorEndpointIdGuard &operator=(const SensorEndpointIdGuard &) = delete;
    void Assign(uint16_t reserved) { if (mSensor) mCurrent = reserved; }
private:
    uint16_t &mCurrent;
    uint16_t mSaved;
    bool mSensor;
};
}
