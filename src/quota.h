#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cpamp {
using Json = nlohmann::json;
struct Quota {
    double remaining = 0;
    int64_t reset = 0;
    int accounts = 1;
};
struct Status {
    std::string name;
    std::optional<Quota> quota;
    std::string error;
};
std::optional<double> Number(const Json& value);
std::optional<int64_t> Timestamp(const Json& value);
std::optional<Quota> ExtractQuota(const Json& data);
std::optional<Quota> HealthQuota(const Json& data);
Json SnapshotAccounts(const Json& data);
std::optional<Quota> SnapshotQuota(const Json& data);
std::optional<Quota> Average(const std::vector<Status>& statuses);
std::string NormalizeUrl(std::string url);
}
