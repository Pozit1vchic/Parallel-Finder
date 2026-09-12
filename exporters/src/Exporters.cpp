#include <pfexporters/ExportOptions.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace pfexporters {
namespace {

std::string jsonEscape(const std::string& value)
{
    std::string result;
    result.reserve(value.size() + 8);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20U) result += '?';
            else result += static_cast<char>(character);
            break;
        }
    }
    return result;
}

std::string csvEscape(const std::string& value)
{
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string result = "\"";
    for (const char character : value) {
        if (character == '"') result += "\"\"";
        else result += character;
    }
    result += '"';
    return result;
}

std::string timecode(double seconds, double fps)
{
    const double safeFps = fps > 0.0 ? fps : 30.0;
    const auto totalFrames = static_cast<long long>(std::llround(std::max(0.0, seconds) * safeFps));
    const auto frame = totalFrames % static_cast<long long>(safeFps);
    const auto totalSeconds = totalFrames / static_cast<long long>(safeFps);
    const auto second = totalSeconds % 60;
    const auto minute = (totalSeconds / 60) % 60;
    const auto hour = totalSeconds / 3600;
    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << hour << ':'
           << std::setw(2) << minute << ':' << std::setw(2) << second << ':'
           << std::setw(2) << frame;
    return output.str();
}

std::string jsonResults(const std::vector<pfcore::MotionMatch>& matches)
{
    std::ostringstream output;
    output << "{\n  \"version\": 1,\n  \"matches\": [\n";
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& item = matches[i];
        output << "    {\n"
               << "      \"index\": " << (i + 1) << ",\n"
               << "      \"similarity\": " << std::setprecision(8) << item.similarity << ",\n"
               << "      \"left\": {\"source\": \"" << jsonEscape(item.leftSourceId)
               << "\", \"start\": " << item.leftStartSeconds << ", \"end\": " << item.leftEndSeconds << "},\n"
               << "      \"right\": {\"source\": \"" << jsonEscape(item.rightSourceId)
               << "\", \"start\": " << item.rightStartSeconds << ", \"end\": " << item.rightEndSeconds << "}\n"
               << "    }" << (i + 1 == matches.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    return output.str();
}

std::string csvResults(const std::vector<pfcore::MotionMatch>& matches)
{
    std::ostringstream output;
    output << "index,similarity,left_source,left_start,left_end,right_source,right_start,right_end\n";
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& item = matches[i];
        output << (i + 1) << ',' << std::setprecision(8) << item.similarity << ','
               << csvEscape(item.leftSourceId) << ',' << item.leftStartSeconds << ',' << item.leftEndSeconds << ','
               << csvEscape(item.rightSourceId) << ',' << item.rightStartSeconds << ',' << item.rightEndSeconds << '\n';
    }
    return output.str();
}

std::string textResults(const std::vector<pfcore::MotionMatch>& matches)
{
    std::ostringstream output;
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& item = matches[i];
        output << "#" << (i + 1) << "  " << std::fixed << std::setprecision(1)
               << item.similarity * 100.0 << "%\n"
               << "  A: " << item.leftSourceId << "  " << item.leftStartSeconds << "–" << item.leftEndSeconds << " s\n"
               << "  B: " << item.rightSourceId << "  " << item.rightStartSeconds << "–" << item.rightEndSeconds << " s\n\n";
    }
    return output.str();
}

std::string edlResults(const std::vector<pfcore::MotionMatch>& matches, double fps)
{
    std::ostringstream output;
    output << "TITLE: Parallel Finder\nFCM: NON-DROP FRAME\n\n";
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& item = matches[i];
        output << std::setfill('0') << std::setw(3) << (i + 1) << "  PF      V     C        "
               << timecode(item.leftStartSeconds, fps) << ' ' << timecode(item.leftEndSeconds, fps)
               << ' ' << timecode(item.leftStartSeconds, fps) << ' ' << timecode(item.leftEndSeconds, fps) << "\n"
               << "* FROM CLIP NAME: " << item.leftSourceId << "\n";
    }
    return output.str();
}

std::string fcpxmlResults(const std::vector<pfcore::MotionMatch>& matches, double fps)
{
    std::ostringstream output;
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<!DOCTYPE fcpxml>\n<fcpxml version=\"1.10\"><resources>"
           << "<format id=\"r1\" name=\"Parallel Finder\" frameDuration=\"1/" << std::max(1, static_cast<int>(std::lround(fps))) << "s\"/></resources>\n"
           << "<library><event name=\"Parallel Finder\"><project name=\"Parallel Results\"><sequence format=\"r1\"><spine>\n";
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& item = matches[i];
        output << "<asset-clip name=\"Pair " << (i + 1) << "\" duration=\""
               << std::max(0.001, item.durationSeconds) << "s\" start=\""
               << item.leftStartSeconds << "s\"/>\n";
    }
    output << "</spine></sequence></project></event></library></fcpxml>\n";
    return output.str();
}

} // namespace

std::string formatResults(const std::vector<pfcore::MotionMatch>& matches,
                          const ExportOptions& options)
{
    switch (options.format) {
    case ExportFormat::Json: return jsonResults(matches);
    case ExportFormat::Csv: return csvResults(matches);
    case ExportFormat::Txt: return textResults(matches);
    case ExportFormat::Edl: return edlResults(matches, options.framesPerSecond);
    case ExportFormat::FcpXml: return fcpxmlResults(matches, options.framesPerSecond);
    case ExportFormat::Aep: {
        std::ostringstream jsx;
        jsx << "// Parallel Finder import helper\n"
            << "// Load parallel_data.json next to this script.\n"
            << "app.beginUndoGroup('Parallel Finder import');\n"
            << "// Generated pair count: " << matches.size() << "\n"
            << "app.endUndoGroup();\n";
        return jsx.str();
    }
    }
    return {};
}

bool writeResults(const std::vector<pfcore::MotionMatch>& matches,
                 const ExportOptions& options,
                 std::string& error)
{
    if (options.outputFolder.empty()) {
        error = "export output folder is empty";
        return false;
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(options.outputFolder, filesystemError);
    if (filesystemError) {
        error = "create export folder: " + filesystemError.message();
        return false;
    }
    const std::filesystem::path root = std::filesystem::path(options.outputFolder) / options.filePrefix;
    const char* extension = ".json";
    switch (options.format) {
    case ExportFormat::Json: extension = ".json"; break;
    case ExportFormat::Csv: extension = ".csv"; break;
    case ExportFormat::Txt: extension = ".txt"; break;
    case ExportFormat::Edl: extension = ".edl"; break;
    case ExportFormat::FcpXml: extension = ".fcpxml"; break;
    case ExportFormat::Aep: extension = ".jsx"; break;
    }
    std::ofstream output(root.string() + extension, std::ios::binary);
    if (!output) {
        error = "open export file failed";
        return false;
    }
    output << formatResults(matches, options);
    if (!output) {
        error = "write export file failed";
        return false;
    }
    if (options.format == ExportFormat::Aep) {
        std::ofstream data(root.parent_path() / "parallel_data.json", std::ios::binary);
        if (!data) {
            error = "open AEP data file failed";
            return false;
        }
        ExportOptions jsonOptions = options;
        jsonOptions.format = ExportFormat::Json;
        data << formatResults(matches, jsonOptions);
    }
    return true;
}

} // namespace pfexporters
