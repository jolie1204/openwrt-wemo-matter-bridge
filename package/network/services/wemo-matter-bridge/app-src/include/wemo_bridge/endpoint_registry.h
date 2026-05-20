#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace wemo_bridge {

class EndpointRegistry
{
public:
    explicit EndpointRegistry(std::string path);

    std::optional<uint16_t> Lookup(const std::string & udn) const;
    std::optional<uint16_t> GetOrAssign(const std::string & udn);

private:
    std::string mPath;
};

} // namespace wemo_bridge
