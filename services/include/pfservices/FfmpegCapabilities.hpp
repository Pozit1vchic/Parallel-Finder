#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <stop_token>

namespace pfservices {
struct FfmpegCapabilities {
    std::vector<std::string> videoEncoders;
    std::vector<std::string> audioEncoders;
    std::string error;
    static FfmpegCapabilities parse(std::string_view output);
    static FfmpegCapabilities probe(const std::string& executable, std::stop_token stop = {});
    std::vector<std::string> h264Candidates() const;
};
}
