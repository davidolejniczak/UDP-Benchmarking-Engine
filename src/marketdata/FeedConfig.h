#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace marketdata {

struct FeedConfig {
    std::uint32_t exchange_id = 0;
    std::string group;
    std::uint16_t port = 0;
};

inline std::vector<FeedConfig> default_feeds() {
    return {
        FeedConfig{0, "239.10.0.1", 9001},
        FeedConfig{1, "239.10.0.2", 9002},
        FeedConfig{2, "239.10.0.3", 9003},
        FeedConfig{3, "239.10.0.4", 9004},
    };
}

inline std::vector<FeedConfig> parse_groups(const std::string& groups) {
    std::vector<FeedConfig> feeds;
    std::size_t start = 0;
    std::uint32_t exchange_id = 0;

    while (start < groups.size()) {
        const std::size_t comma = groups.find(',', start);
        const std::string token = groups.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        const std::size_t colon = token.rfind(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= token.size()) {
            throw std::invalid_argument("groups must be group:port pairs separated by commas");
        }

        const int port = std::stoi(token.substr(colon + 1));
        if (port <= 0 || port > 65535) {
            throw std::invalid_argument("multicast port out of range");
        }

        feeds.push_back(FeedConfig{exchange_id++, token.substr(0, colon), static_cast<std::uint16_t>(port)});
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }

    if (feeds.empty()) {
        throw std::invalid_argument("at least one multicast group is required");
    }

    return feeds;
}

}  // namespace marketdata
