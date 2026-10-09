#pragma once
namespace wemo_bridge {
struct MotionSensorState { bool reachable; bool occupied; };
// Occupancy is nonnullable. Before any valid observation, false is only an
// unavailable placeholder. Invalid observations never update last occupancy.
inline MotionSensorState ApplyMotionSensorSnapshot(int packed, bool online, bool lastOccupied)
{
    const bool valid = packed == 2 || packed == 5;
    return {online && valid, online && valid ? (packed & 1) != 0 : lastOccupied};
}
}
