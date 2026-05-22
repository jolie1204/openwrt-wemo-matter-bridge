#include "wemo_bridge/wemo_adapter_openwemo.h"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if HAVE_OPENWEMO_ENGINE
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
extern "C" {
#define export we_export_param
#include "wemo_engine.h"
#undef export
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

namespace wemo_bridge {

namespace {

// Values from wemo_engine.h dev_id_t enum.
constexpr int kTypeSwitch  = 1;
constexpr int kTypeMini    = 3;
constexpr int kTypeDimmer  = 4;
constexpr int kTypeInsight = 5;

#if HAVE_OPENWEMO_ENGINE
void MaybeConfigureIpcTarget(const std::string & engine_socket)
{
    const auto pos = engine_socket.rfind(':');
    if (pos == std::string::npos || pos == 0 || pos + 1 >= engine_socket.size())
    {
        return;
    }

    const std::string host = engine_socket.substr(0, pos);
    const std::string port_str = engine_socket.substr(pos + 1);
    const int port = std::atoi(port_str.c_str());
    if (port > 0)
    {
        we_set_ipc_target(host.c_str(), port);
    }
}

bool EnsureEngineInitialized(const std::string & engine_socket)
{
    static std::mutex init_lock;
    static bool target_configured = false;
    static bool initialized = false;

    std::lock_guard<std::mutex> lock(init_lock);
    if (initialized)
    {
        return true;
    }

    if (!target_configured)
    {
        MaybeConfigureIpcTarget(engine_socket);
        target_configured = true;
    }

    initialized = (we_init() != 0);
    if (!initialized)
    {
        std::fprintf(stderr, "wemo_adapter: we_init failed (socket=%s)\n", engine_socket.c_str());
    }

    return initialized;
}

bool SendState(int wemo_id, int state, int level)
{
    struct we_state target {};
    target.state     = state;
    target.level     = level;
    target.is_online = 1;

    const int rc_set = we_set_action(wemo_id, &target);
    std::fprintf(stderr, "wemo_adapter: set_action wemo_id=%d state=%d level=%d rc=%d\n", wemo_id, state, level, rc_set);
    return rc_set != 0;
}

StateEventCallback gStateCallback;

void EmitStateEvent(int wemo_id, const struct we_state & data)
{
    if (gStateCallback)
    {
        WemoStateEvent ev;
        ev.wemo_id  = wemo_id;
        ev.is_online = (data.is_online != 0);
        ev.state     = data.state;
        ev.level     = data.level;
        gStateCallback(ev);
    }
}

void OnWeStateEvent(int wemo_id, struct we_state * data)
{
    if (data != nullptr)
    {
        EmitStateEvent(wemo_id, *data);
    }
}
#endif

} // namespace

WemoAdapterOpenWemo::WemoAdapterOpenWemo(std::string engine_socket) : mEngineSocket(std::move(engine_socket)) {}

std::vector<WemoDevice> WemoAdapterOpenWemo::Discover()
{
#if HAVE_OPENWEMO_ENGINE
    if (!EnsureEngineInitialized(mEngineSocket))
    {
        return {};
    }

    (void) we_discover(0);
#endif

    return ListCachedDevices();
}

std::vector<WemoDevice> WemoAdapterOpenWemo::ListCachedDevices()
{
    std::vector<WemoDevice> devices;

#if HAVE_OPENWEMO_ENGINE
    if (!EnsureEngineInitialized(mEngineSocket))
    {
        return devices;
    }

    struct we_device_list list {};
    const int rc = we_list_devices(&list);
    if (rc != WE_STATUS_OK)
    {
        std::fprintf(stderr, "wemo_adapter: we_list_devices failed rc=%d\n", rc);
        return devices;
    }

    std::lock_guard<std::mutex> lock(mCacheMutex);
    mUdnToWemoId.clear();

    const int count = std::clamp(list.count, 0, WE_DEVICE_LIST_MAX_ITEMS);
    for (int i = 0; i < count; i++)
    {
        const struct we_device_info & info = list.items[i];

        WemoDevice device;
        device.wemo_id          = info.wemo_id;
        device.device_type      = info.device_type;
        device.udn              = info.udn;
        device.friendly_name    = info.friendly_name;
        device.manufacturer     = info.manufacturer;
        device.model_name       = info.model_name;
        device.serial_number    = info.serial_number;
        device.firmware_version = info.firmware_version;
        device.is_online        = (info.is_online != 0);
        device.onoff            = static_cast<uint8_t>(info.state ? 1 : 0);
        // Some firmware reports a generic device_type even for dimmers but
        // still provides a valid level (0-100).
        device.supports_level = (info.device_type == kTypeDimmer) || (info.level >= 0);
        device.is_plug        = (info.device_type == kTypeSwitch) || (info.device_type == kTypeMini) ||
            (info.device_type == kTypeInsight);
        if (info.level >= 0)
        {
            device.level_percent = static_cast<uint8_t>(std::clamp(info.level, 0, 100));
        }

        if (!device.udn.empty())
        {
            mUdnToWemoId[device.udn] = device.wemo_id;
        }

        devices.push_back(std::move(device));
    }
#endif

    return devices;
}

void WemoAdapterOpenWemo::Refresh()
{
#if HAVE_OPENWEMO_ENGINE
    if (EnsureEngineInitialized(mEngineSocket))
    {
        (void) we_discover(0);
    }
#endif
}

void WemoAdapterOpenWemo::PollStates()
{
#if HAVE_OPENWEMO_ENGINE
    if (!EnsureEngineInitialized(mEngineSocket))
    {
        return;
    }

    struct we_device_list list {};
    const int rc = we_list_devices(&list);
    if (rc != WE_STATUS_OK)
    {
        std::fprintf(stderr, "wemo_adapter: state poll list_devices failed rc=%d\n", rc);
        return;
    }

    struct PolledState
    {
        int wemo_id;
        struct we_state state;
    };
    std::vector<PolledState> polled;

    const int count = std::clamp(list.count, 0, WE_DEVICE_LIST_MAX_ITEMS);
    polled.reserve(static_cast<size_t>(count));
    {
        std::lock_guard<std::mutex> lock(mCacheMutex);
        for (int i = 0; i < count; i++)
        {
            const struct we_device_info & info = list.items[i];
            if (info.wemo_id <= 0)
            {
                continue;
            }
            if (info.udn[0] != '\0')
            {
                mUdnToWemoId[info.udn] = info.wemo_id;
            }

            struct we_state state {};
            state.is_online = info.is_online;
            state.state     = info.state;
            state.level     = info.level;
            polled.push_back({ info.wemo_id, state });
        }
    }

    for (const auto & entry : polled)
    {
        EmitStateEvent(entry.wemo_id, entry.state);
    }
#endif
}

void WemoAdapterOpenWemo::RegisterStateCallback(StateEventCallback cb)
{
#if HAVE_OPENWEMO_ENGINE
    if (EnsureEngineInitialized(mEngineSocket))
    {
        gStateCallback = std::move(cb);
        we_register_event_callback(OnWeStateEvent);
    }
#else
    (void) cb;
#endif
}

bool WemoAdapterOpenWemo::SetOnOff(const std::string & udn, bool on)
{
#if !HAVE_OPENWEMO_ENGINE
    (void) udn;
    (void) on;
    return false;
#else
    if (!EnsureEngineInitialized(mEngineSocket))
    {
        return false;
    }

    // Fast path: cache lookup
    {
        std::lock_guard<std::mutex> lock(mCacheMutex);
        auto it = mUdnToWemoId.find(udn);
        if (it != mUdnToWemoId.end())
        {
            return SendState(it->second, on ? 1 : 0, -1);
        }
    }

    // Cache miss: refresh device list and retry
    (void) we_discover(0);
    struct we_device_list list {};
    if (we_list_devices(&list) == WE_STATUS_OK)
    {
        std::lock_guard<std::mutex> lock(mCacheMutex);
        const int count = std::clamp(list.count, 0, WE_DEVICE_LIST_MAX_ITEMS);
        for (int i = 0; i < count; i++)
        {
            if (list.items[i].udn[0] != '\0')
            {
                mUdnToWemoId[list.items[i].udn] = list.items[i].wemo_id;
            }
        }
        auto it = mUdnToWemoId.find(udn);
        if (it != mUdnToWemoId.end())
        {
            return SendState(it->second, on ? 1 : 0, -1);
        }
    }

    return false;
#endif
}

bool WemoAdapterOpenWemo::SetLevelPercent(const std::string & udn, uint8_t percent)
{
#if !HAVE_OPENWEMO_ENGINE
    (void) udn;
    (void) percent;
    return false;
#else
    if (!EnsureEngineInitialized(mEngineSocket))
    {
        return false;
    }

    const int clamped = std::clamp(static_cast<int>(percent), 0, 100);
    const int state   = (clamped > 0) ? 1 : 0;

    // Fast path: cache lookup
    {
        std::lock_guard<std::mutex> lock(mCacheMutex);
        auto it = mUdnToWemoId.find(udn);
        if (it != mUdnToWemoId.end())
        {
            return SendState(it->second, state, clamped);
        }
    }

    // Cache miss: refresh device list and retry
    (void) we_discover(0);
    struct we_device_list list {};
    if (we_list_devices(&list) == WE_STATUS_OK)
    {
        std::lock_guard<std::mutex> lock(mCacheMutex);
        const int count = std::clamp(list.count, 0, WE_DEVICE_LIST_MAX_ITEMS);
        for (int i = 0; i < count; i++)
        {
            if (list.items[i].udn[0] != '\0')
            {
                mUdnToWemoId[list.items[i].udn] = list.items[i].wemo_id;
            }
        }
        auto it = mUdnToWemoId.find(udn);
        if (it != mUdnToWemoId.end())
        {
            return SendState(it->second, state, clamped);
        }
    }

    return false;
#endif
}

bool WemoAdapterOpenWemo::SetFriendlyName(const std::string & udn, const std::string & name)
{
#if !HAVE_OPENWEMO_ENGINE
    (void) udn;
    (void) name;
    return false;
#else
    if (!EnsureEngineInitialized(mEngineSocket))
    {
        return false;
    }

    auto resolveWemoId = [this](const std::string & key) -> int {
        std::lock_guard<std::mutex> lock(mCacheMutex);
        auto it = mUdnToWemoId.find(key);
        return (it != mUdnToWemoId.end()) ? it->second : 0;
    };

    int wemoId = resolveWemoId(udn);
    if (wemoId <= 0)
    {
        (void) we_discover(0);
        (void) ListCachedDevices();
        wemoId = resolveWemoId(udn);
    }
    if (wemoId <= 0)
    {
        return false;
    }

    struct we_name_change data {};
    std::snprintf(data.name, sizeof(data.name), "%s", name.c_str());
    const int rc = we_change_name(wemoId, &data);
    std::fprintf(stderr, "wemo_adapter: change_name wemo_id=%d name=%s rc=%d\n", wemoId, data.name, rc);
    return rc != 0;
#endif
}

} // namespace wemo_bridge
