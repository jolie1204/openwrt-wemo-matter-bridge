#pragma once
namespace wemo_bridge {
// Scheduled Matter work already owns the POSIX nonrecursive stack mutex.
// Startup/foreign-thread mutations must acquire it, but owner-thread mutations
// must not acquire it twice. Manager must provide real lock-owner tracking.
template<class Manager> class StackLockIfNeeded
{
public:
    explicit StackLockIfNeeded(Manager &manager) : mManager(manager), mAcquired(!manager.IsChipStackLockedByCurrentThread())
    {
        if (mAcquired) mManager.LockChipStack();
    }
    ~StackLockIfNeeded() { if (mAcquired) mManager.UnlockChipStack(); }
    StackLockIfNeeded(const StackLockIfNeeded &) = delete;
    StackLockIfNeeded &operator=(const StackLockIfNeeded &) = delete;
private:
    Manager &mManager;
    bool mAcquired;
};
}
