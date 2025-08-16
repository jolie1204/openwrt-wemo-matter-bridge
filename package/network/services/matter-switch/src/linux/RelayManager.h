#pragma once
#include <string>

class RelayManager {
public:
    static RelayManager & Instance();

    // paths exported by your DTS (gpio-export and gpio-leds)
    bool Init(const std::string & relay="/sys/class/gpio/relay/value",
              const std::string & white="/sys/class/leds/white/brightness");

    void Set(bool on);
    void Toggle();
    bool IsOn() const { return mState; }

private:
    RelayManager() = default;
    void SyncLed();

    std::string mRelayPath, mWhiteLedPath;
    bool mState = false;
};
