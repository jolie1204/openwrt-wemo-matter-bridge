#include "RelayManager.h"
#include <fcntl.h>
#include <unistd.h>

static bool writeStr(const std::string &path, const char *s) {
    int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = write(fd, s, strlen(s));
    close(fd);
    return n > 0;
}
static bool readBool(const std::string &path, bool &out) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char c; bool ok = (read(fd, &c, 1) == 1);
    close(fd);
    if (!ok) return false;
    out = (c != '0');
    return true;
}

RelayManager & RelayManager::Instance() {
    static RelayManager s; return s;
}

bool RelayManager::Init(const std::string &relay, const std::string &white) {
    mRelayPath = relay;
    mWhiteLedPath = white;
    // read current relay state if possible, default OFF
    readBool(mRelayPath, mState);
    SyncLed();
    return true;
}

void RelayManager::SyncLed() {
    if (mWhiteLedPath.empty()) return;
    // gpio-leds expects 0 or nonzero brightness
    writeStr(mWhiteLedPath, mState ? "255\n" : "0\n");
}

void RelayManager::Set(bool on) {
    mState = on;
    writeStr(mRelayPath, mState ? "1\n" : "0\n");
    SyncLed();
}

void RelayManager::Toggle() { Set(!mState); }
