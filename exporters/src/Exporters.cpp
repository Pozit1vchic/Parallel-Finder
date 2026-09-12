#include <pfexporters/ExportOptions.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
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

std::string xmlEscape(const std::string& value)
{
    std::string result;
    result.reserve(value.size() + 8);
    for (const char character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default: result += character; break;
        }
    }
    return result;
}

std::string fileUri(const std::string& path)
{
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::string escaped;
    escaped.reserve(normalized.size() + 8);
    for (const char character : normalized) {
        if (character == '%') escaped += "%25";
        else if (character == ' ') escaped += "%20";
        else if (character == '#') escaped += "%23";
        else if (character == '?') escaped += "%3F";
        else escaped += character;
    }
    if (escaped.size() >= 2 && escaped[1] == ':') return "file:///" + escaped;
    return "file:///" + escaped;
}

std::vector<pfcore::MotionMatch> orderedMatches(const std::vector<pfcore::MotionMatch>& matches,
                                                NumberingMode mode)
{
    std::vector<pfcore::MotionMatch> ordered = matches;
    if (mode == NumberingMode::AsInVideo) {
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            if (left.leftSourceId != right.leftSourceId) return left.leftSourceId < right.leftSourceId;
            if (std::abs(left.leftStartSeconds - right.leftStartSeconds) > 1e-9)
                return left.leftStartSeconds < right.leftStartSeconds;
            if (left.rightSourceId != right.rightSourceId) return left.rightSourceId < right.rightSourceId;
            return left.rightStartSeconds < right.rightStartSeconds;
        });
    } else {
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            if (std::abs(left.rankScore - right.rankScore) > 1e-9)
                return left.rankScore > right.rankScore;
            if (std::abs(left.similarity - right.similarity) > 1e-9)
                return left.similarity > right.similarity;
            return left.leftStartSeconds < right.leftStartSeconds;
        });
    }
    return ordered;
}

std::string timecode(double seconds, double fps, bool dropFrame)
{
    const double safeFps = std::max(1.0, fps);
    const long long nominalFps = std::max(1LL, std::llround(safeFps));
    long long totalFrames = static_cast<long long>(std::llround(std::max(0.0, seconds) * safeFps));
    if (dropFrame && (nominalFps == 30 || nominalFps == 60)) {
        const long long drop = nominalFps == 60 ? 4 : 2;
        const long long framesPerMinute = nominalFps * 60 - drop;
        const long long framesPerTenMinutes = nominalFps * 600 - drop * 9;
        const long long blocks = totalFrames / framesPerTenMinutes;
        const long long remainder = totalFrames % framesPerTenMinutes;
        totalFrames += drop * 9 * blocks + drop * std::max(0LL, (remainder - drop) / framesPerMinute);
    }
    const auto frame = totalFrames % nominalFps;
    const auto totalSeconds = totalFrames / nominalFps;
    const auto second = totalSeconds % 60;
    const auto minute = (totalSeconds / 60) % 60;
    const auto hour = totalSeconds / 3600;
    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << hour << ':'
           << std::setw(2) << minute << ':' << std::setw(2) << second << ':'
           << std::setw(2) << frame;
    return output.str();
}

std::string frameDuration(double fps)
{
    if (!(fps > 0.0) || !std::isfinite(fps)) return "1/1s";
    const auto closeTo = [fps](double value) { return std::abs(fps - value) < 0.01; };
    if (closeTo(23.976)) return "1001/24000s";
    if (closeTo(29.97)) return "1001/30000s";
    if (closeTo(59.94)) return "1001/60000s";
    const long long fpsScale = 1'000'000;
    const long long fpsNumerator = std::max(1LL, std::llround(fps * fpsScale));
    const long long divisor = std::gcd(fpsScale, fpsNumerator);
    return std::to_string(fpsScale / divisor) + "/" + std::to_string(fpsNumerator / divisor) + "s";
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
               << "      \"dtwDistance\": " << item.dtwDistance << ",\n"
               << "      \"durationSeconds\": " << item.durationSeconds << ",\n"
               << "      \"direction\": \"" << jsonEscape(item.directionLabel) << "\",\n"
               << "      \"gesture\": \"" << jsonEscape(item.gestureLabel) << "\",\n"
               << "      \"rankScore\": " << item.rankScore << ",\n"
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
    output << "index,similarity,rank_score,direction,gesture,dtw_distance,duration_seconds,left_source,left_start,left_end,right_source,right_start,right_end\n";
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& item = matches[i];
        output << (i + 1) << ',' << std::setprecision(8) << item.similarity << ','
               << item.rankScore << ',' << csvEscape(item.directionLabel) << ','
               << csvEscape(item.gestureLabel) << ',' << item.dtwDistance << ',' << item.durationSeconds << ','
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

std::string edlResults(const std::vector<pfcore::MotionMatch>& matches, const ExportOptions& options)
{
    std::ostringstream output;
    output << "TITLE: Parallel Finder\nFCM: "
           << (options.dropFrame ? "DROP FRAME" : "NON-DROP FRAME") << "\n\n";
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& item = matches[i];
        output << std::setfill('0') << std::setw(3) << (i + 1) << "  PF      V     C        "
               << timecode(item.leftStartSeconds, options.framesPerSecond, options.dropFrame) << ' '
               << timecode(item.leftEndSeconds, options.framesPerSecond, options.dropFrame) << ' '
               << timecode(item.leftStartSeconds, options.framesPerSecond, options.dropFrame) << ' '
               << timecode(item.leftEndSeconds, options.framesPerSecond, options.dropFrame) << "\n"
               << "* FROM CLIP NAME: " << item.leftSourceId << "\n";
    }
    return output.str();
}

std::string fcpxmlResults(const std::vector<pfcore::MotionMatch>& matches, const ExportOptions& options)
{
    std::vector<std::string> sources;
    for (const auto& item : matches) {
        if (std::find(sources.begin(), sources.end(), item.leftSourceId) == sources.end())
            sources.push_back(item.leftSourceId);
        if (std::find(sources.begin(), sources.end(), item.rightSourceId) == sources.end())
            sources.push_back(item.rightSourceId);
    }
    std::ostringstream output;
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<!DOCTYPE fcpxml>\n<fcpxml version=\"1.10\"><resources>"
           << "<format id=\"r1\" name=\"Parallel Finder\" frameDuration=\""
           << frameDuration(options.framesPerSecond) << "\"/>\n";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        double duration = 0.001;
        for (const auto& item : matches) {
            if (item.leftSourceId == sources[i]) duration = std::max(duration, item.leftEndSeconds);
            if (item.rightSourceId == sources[i]) duration = std::max(duration, item.rightEndSeconds);
        }
        output << "<asset id=\"a" << (i + 1) << "\" name=\"" << xmlEscape(sources[i])
               << "\" src=\"" << xmlEscape(fileUri(sources[i])) << "\" start=\"0s\" duration=\""
               << duration << "s\"/>\n";
    }
    output << "</resources>\n"
           << "<library><event name=\"Parallel Finder\"><project name=\"Parallel Results\"><sequence format=\"r1\"><spine>\n";
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& item = matches[i];
        const auto leftAsset = static_cast<std::size_t>(std::distance(sources.begin(),
            std::find(sources.begin(), sources.end(), item.leftSourceId))) + 1;
        const auto rightAsset = static_cast<std::size_t>(std::distance(sources.begin(),
            std::find(sources.begin(), sources.end(), item.rightSourceId))) + 1;
        const double pairDuration = std::max(0.001, item.durationSeconds);
        output << "<asset-clip ref=\"a" << leftAsset << "\" name=\"Pair " << (i + 1)
               << " A\" duration=\"" << pairDuration << "s\" start=\""
               << item.leftStartSeconds << "s\"/>\n"
               << "<asset-clip ref=\"a" << rightAsset << "\" name=\"Pair " << (i + 1)
               << " B\" duration=\"" << pairDuration << "s\" start=\""
               << item.rightStartSeconds << "s\"/>\n";
    }
    output << "</spine></sequence></project></event></library></fcpxml>\n";
    return output.str();
}

std::string jsEscape(const std::string& value)
{
    std::string result;
    for (const char character : value) {
        if (character == '\\' || character == '\'') result += '\\';
        if (character == '\n') result += "\\n";
        else if (character == '\r') result += "\\r";
        else result += character;
    }
    return result;
}

std::string aepResults(const std::vector<pfcore::MotionMatch>& matches, const ExportOptions& options)
{
    std::ostringstream jsx;
    jsx << "// Parallel Finder import helper\n"
        << "// Generated by Parallel Finder. Keep this file next to parallel_data.json.\n"
        << "(function () {\n"
        << "  var dataFile = new File(File($.fileName).parent.fsName + '/parallel_data.json');\n"
        << "  if (!dataFile.exists) { alert('parallel_data.json not found next to the script.'); return; }\n"
        << "  dataFile.open('r'); var data = JSON.parse(dataFile.read()); dataFile.close();\n"
        << "  function footage(path) { var f = new File(path); if (!f.exists) return null;"
        << " var io = new ImportOptions(f); return app.project.importFile(io); }\n"
        << "  app.beginUndoGroup('Parallel Finder import');\n"
        << "  var comp = app.project.items.addComp('Parallel Finder results', 1920, 1080, 1, 3600, "
        << options.framesPerSecond << ");\n"
        << "  var cursor = 0;\n";
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& item = matches[i];
        const double duration = std::max(0.001, item.durationSeconds);
        jsx << "  var a" << i << " = footage('" << jsEscape(item.leftSourceId) << "');\n"
            << "  var b" << i << " = footage('" << jsEscape(item.rightSourceId) << "');\n"
            << "  if (a" << i << ") { var la" << i << " = comp.layers.add(a" << i
            << "); la" << i << ".startTime = cursor - " << item.leftStartSeconds
            << "; la" << i << ".inPoint = cursor; la" << i << ".outPoint = cursor + " << duration
            << "; la" << i << ".name = 'Pair " << (i + 1) << " A'; }\n"
            << "  if (b" << i << ") { var lb" << i << " = comp.layers.add(b" << i
            << "); lb" << i << ".startTime = cursor - " << item.rightStartSeconds
            << "; lb" << i << ".inPoint = cursor; lb" << i << ".outPoint = cursor + " << duration
            << "; lb" << i << ".name = 'Pair " << (i + 1) << " B'; }\n"
            << "  cursor += " << (duration + 0.25) << ";\n";
    }
    jsx << "  comp.duration = Math.max(1, cursor);\n"
        << "  comp.comment = 'Parallel Finder: ' + data.matches.length + ' motion pairs';\n"
        << "  app.endUndoGroup();\n"
        << "})();\n";
    return jsx.str();
}

bool validPrefix(const std::string& prefix)
{
    if (prefix.empty() || prefix == "." || prefix == ".." || prefix.find('/') != std::string::npos
        || prefix.find('\\') != std::string::npos) return false;
    return std::all_of(prefix.begin(), prefix.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '_' || character == '-' || character == '.';
    });
}

} // namespace

std::string formatResults(const std::vector<pfcore::MotionMatch>& matches,
                          const ExportOptions& options)
{
    const auto ordered = orderedMatches(matches, options.numbering);
    switch (options.format) {
    case ExportFormat::Json: return jsonResults(ordered);
    case ExportFormat::Csv: return csvResults(ordered);
    case ExportFormat::Txt: return textResults(ordered);
    case ExportFormat::Edl: return edlResults(ordered, options);
    case ExportFormat::FcpXml: return fcpxmlResults(ordered, options);
    case ExportFormat::Aep: return aepResults(ordered, options);
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
    if (!validPrefix(options.filePrefix)) {
        error = "export file prefix contains unsupported path characters";
        return false;
    }
    if ((options.format == ExportFormat::Edl || options.format == ExportFormat::FcpXml
         || options.format == ExportFormat::Aep)
        && !(options.framesPerSecond > 0.0 && std::isfinite(options.framesPerSecond))) {
        error = "a positive probed FPS is required for this export format";
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
    const std::filesystem::path destination = root.string() + extension;
    const std::filesystem::path temporary = destination.string() + ".part";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "open export file failed";
        return false;
    }
    output << formatResults(matches, options);
    if (!output) {
        error = "write export file failed";
        output.close();
        std::filesystem::remove(temporary, filesystemError);
        return false;
    }
    output.close();
    std::filesystem::remove(destination, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(temporary, destination, filesystemError);
    if (filesystemError) {
        error = "install export file failed: " + filesystemError.message();
        std::filesystem::remove(temporary, filesystemError);
        return false;
    }
    if (options.format == ExportFormat::Aep) {
        const auto dataDestination = root.parent_path() / "parallel_data.json";
        const auto dataTemporary = dataDestination.string() + ".part";
        std::ofstream data(dataTemporary, std::ios::binary | std::ios::trunc);
        if (!data) {
            error = "open AEP data file failed";
            return false;
        }
        ExportOptions jsonOptions = options;
        jsonOptions.format = ExportFormat::Json;
        data << formatResults(matches, jsonOptions);
        data.close();
        std::filesystem::remove(dataDestination, filesystemError);
        filesystemError.clear();
        std::filesystem::rename(dataTemporary, dataDestination, filesystemError);
        if (filesystemError) {
            error = "install AEP data file failed: " + filesystemError.message();
            std::filesystem::remove(dataTemporary, filesystemError);
            return false;
        }
    }
    return true;
}

} // namespace pfexporters
