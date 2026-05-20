#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "wemo_bridge/endpoint_registry.h"
#include "wemo_bridge/wemo_adapter_openwemo.h"

namespace {

void PrintUsage(const char * bin)
{
    std::cout << "Usage:\n"
              << "  " << bin << " list\n"
              << "  " << bin << " set-on <udn>\n"
              << "  " << bin << " set-off <udn>\n"
              << "  " << bin << " set-level <udn> <0-100>\n";
}

std::optional<int> ParsePercent(const std::string & value)
{
    char * end = nullptr;
    long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed < 0 || parsed > 100)
    {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

} // namespace

int main(int argc, char ** argv)
{
    wemo_bridge::EndpointRegistry registry("./var/endpoint-map.sqlite3");
    wemo_bridge::WemoAdapterOpenWemo adapter("127.0.0.1:49153");

    if (argc <= 1 || std::string(argv[1]) == "list")
    {
        const auto devices = adapter.Discover();
        std::cout << "discovered_devices=" << devices.size() << std::endl;
        for (const auto & device : devices)
        {
            const auto endpoint_id = registry.GetOrAssign(device.udn);
            std::cout << "udn=" << device.udn << " wemo_id=" << device.wemo_id
                      << " endpoint=" << (endpoint_id.has_value() ? std::to_string(endpoint_id.value()) : "n/a")
                      << " online=" << (device.is_online ? "1" : "0")
                      << " onoff=" << static_cast<int>(device.onoff)
                      << " level=" << static_cast<int>(device.level_percent)
                      << " supports_level=" << (device.supports_level ? "1" : "0")
                      << " name=\"" << device.friendly_name << "\""
                      << std::endl;
        }
        return 0;
    }

    const std::string cmd = argv[1];
    if ((cmd == "set-on" || cmd == "set-off") && argc == 3)
    {
        const std::string udn = argv[2];
        const bool on = (cmd == "set-on");
        if (!adapter.SetOnOff(udn, on))
        {
            std::cerr << "failed: " << cmd << " udn=" << udn << std::endl;
            return 1;
        }
        std::cout << "ok: " << cmd << " udn=" << udn << std::endl;
        return 0;
    }

    if (cmd == "set-level" && argc == 4)
    {
        const std::string udn = argv[2];
        const auto percent = ParsePercent(argv[3]);
        if (!percent.has_value())
        {
            std::cerr << "invalid level percent: " << argv[3] << std::endl;
            return 1;
        }
        if (!adapter.SetLevelPercent(udn, static_cast<uint8_t>(percent.value())))
        {
            std::cerr << "failed: set-level udn=" << udn << " percent=" << percent.value() << std::endl;
            return 1;
        }
        std::cout << "ok: set-level udn=" << udn << " percent=" << percent.value() << std::endl;
        return 0;
    }

    PrintUsage(argv[0]);
    return 1;
}
