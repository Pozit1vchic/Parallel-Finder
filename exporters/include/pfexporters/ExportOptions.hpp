#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <pfcore/MotionMatcher.hpp>

namespace pfexporters {

// Export formats for v1 (spec section 7).
enum class ExportFormat {
    Json,   // full structured results
    Csv,    // flat table
    Txt,    // human-readable list
    Edl,    // CMX3600; FPS taken from probe, never hardcoded
    FcpXml, // FCPXML 1.10
    Aep,    // After Effects: generated .jsx + parallel_data.json (not binary .aep)
};

// Export numbering modes (spec section 7).
enum class NumberingMode {
    AsInVideo,      // «как в видео»
    RenumberSorted, // «перенумеровать по сортировке»
};

// Cut modes (spec section 7).
enum class CutMode {
    Exact, // re-encode; encoder priority NVENC → AMF → QSV → CPU
    Fast,  // -c copy; accuracy ±0.5s around keyframes
};

struct ExportOptions {
    ExportFormat format = ExportFormat::Json;
    NumberingMode numbering = NumberingMode::AsInVideo;
    CutMode cutMode = CutMode::Exact;
    // Output folder + prefix (default "frame_", auto-numbering #0001).
    std::string outputFolder;
    std::string filePrefix = "frame_";
    bool downscaleAboveSource = true; // разрешение не выше исходника
    // Must be supplied by the decoder/probe for EDL/FCPXML. Zero means
    // unknown and is rejected by writeResults for frame-based formats.
    double framesPerSecond = 0.0;
    bool dropFrame = false;
};

std::string formatResults(const std::vector<pfcore::MotionMatch>& matches,
                          const ExportOptions& options);

// Writes the selected textual export. For AEP, `outputFolder/filePrefix.jsx`
// and `outputFolder/parallel_data.json` are written together.
bool writeResults(const std::vector<pfcore::MotionMatch>& matches,
                  const ExportOptions& options,
                  std::string& error);

} // namespace pfexporters
