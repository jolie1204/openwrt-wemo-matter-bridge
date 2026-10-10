// Keep regression assertions and their lock checks active in release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wemo_bridge/stack_lock_if_needed.h"
#include <cassert>
#include <mutex>
#include <thread>
#include <iostream>
struct Manager {
    std::mutex mutex;
    std::thread::id owner;
    int locks=0, unlocks=0;
    bool IsChipStackLockedByCurrentThread() const { return owner == std::this_thread::get_id(); }
    void LockChipStack() {
        // try_lock detects the regression without hanging the test process.
        assert(!IsChipStackLockedByCurrentThread()); assert(mutex.try_lock());
        owner=std::this_thread::get_id(); ++locks;
    }
    void UnlockChipStack() { assert(IsChipStackLockedByCurrentThread());owner={};++unlocks;mutex.unlock(); }
};
bool mutate(Manager &manager,bool registrationSucceeds) {
    wemo_bridge::StackLockIfNeeded guard(manager);
    assert(manager.IsChipStackLockedByCurrentThread());
    if (!registrationSucceeds) return false;
    return true;
}
int main() {
    Manager manager;
    assert(mutate(manager,true));assert(!mutate(manager,false)); // startup add/failure
    assert(manager.locks==2 && manager.unlocks==2);
    manager.LockChipStack(); // PlatformMgr ScheduleWork already holds this mutex.
    const auto locks=manager.locks;
    assert(mutate(manager,true));assert(!mutate(manager,false)); // add/remove/error in scheduled work
    assert(manager.IsChipStackLockedByCurrentThread());
    assert(manager.locks==locks && manager.unlocks==2);
    manager.UnlockChipStack();assert(manager.locks==manager.unlocks);
    std::cout << "PASS: nonrecursive Matter stack lock startup + already-held mutation/error paths\n";
}
