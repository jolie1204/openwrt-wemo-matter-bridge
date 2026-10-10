#pragma once

#include <optional>
#include <string>

namespace wemo_bridge {

// Only the UUID scheme is case-insensitive. The authoritative UDN body is kept
// exactly as supplied by discovery, including vendor-specific letter case.
std::optional<std::string> CanonicalIdentityUdn(const std::string & udn);

class EndpointIdentityRegistry
{
public:
    explicit EndpointIdentityRegistry(std::string path);

    // Explicit first provisioning only. Never overwrites a file; runtime does
    // not call this, and missing/damaged databases must not regenerate IDs.
    static bool InitializeEmpty(const std::string & path);

    // Existing values win. A new candidate escapes only after durable COMMIT.
    // Invalid input, collision, corrupt record, lock or disk errors fail closed.
    std::optional<std::string> GetOrAssign(const std::string & udn, const std::string & candidate);

private:
    std::string mPath;
};

} // namespace wemo_bridge
