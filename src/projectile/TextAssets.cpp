#include "TextAssets.h"
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

namespace Projectile {
namespace TextAssets {

// =============================================================================
// Runtime-loaded metrics from CSV
// =============================================================================

static std::unordered_map<wchar_t, UVCoord> s_uvCoords;      // UV coordinates from CSV
static std::unordered_map<wchar_t, float> s_widthRatios;     // Width ratios from CSV
static bool s_metricsLoaded = false;
static int s_atlasCols = 0;  // Derived from max col in CSV
static int s_atlasRows = 0;  // Derived from max row in CSV

const UVCoord* GetCharUV(wchar_t ch) {
    // Try loading CSV if not loaded yet
    if (!s_metricsLoaded) {
        if (!LoadMetricsFromCSV()) {
            spdlog::error("TextAssets: Failed to load character metrics from CSV! Text rendering will not work.");
            return nullptr;
        }
    }

    // Look up in CSV-loaded coordinates
    auto it = s_uvCoords.find(ch);
    if (it != s_uvCoords.end()) {
        return &it->second;
    }

    // Character not found in CSV
    return nullptr;
}

bool IsRenderableChar(wchar_t ch) {
    // Whitespace is handled specially (creates gaps)
    if (ch == L' ' || ch == L'\t' || ch == L'\u00A0') {
        return true;
    }
    return GetCharUV(ch) != nullptr;
}

// =============================================================================
// Runtime CSV Loading
// =============================================================================

bool LoadMetricsFromCSV() {
    if (s_metricsLoaded) {
        return true;
    }

    std::ifstream file(TEXT_METRICS_CSV);
    if (!file.is_open()) {
        spdlog::warn("TextAssets: Could not open metrics CSV at {}", TEXT_METRICS_CSV);
        return false;
    }

    std::string line;
    // Skip header line
    if (!std::getline(file, line)) {
        spdlog::warn("TextAssets: CSV file is empty");
        return false;
    }

    int loadedCount = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // Parse CSV: Character,Unicode,Index,Row,Col,X,Y,Width,WidthRatio
        std::stringstream ss(line);
        std::string charStr, unicode, index, row, col, x, y, width, widthRatio;

        std::getline(ss, charStr, ',');
        std::getline(ss, unicode, ',');
        std::getline(ss, index, ',');
        std::getline(ss, row, ',');
        std::getline(ss, col, ',');
        std::getline(ss, x, ',');
        std::getline(ss, y, ',');
        std::getline(ss, width, ',');
        std::getline(ss, widthRatio, ',');

        if (charStr.empty() || widthRatio.empty()) continue;

        // Handle the character (could be multi-byte UTF-8)
        wchar_t ch = 0;
        if (charStr.size() == 1) {
            ch = static_cast<wchar_t>(charStr[0]);
        } else {
            // Parse Unicode value (U+XXXX format)
            if (unicode.size() >= 6 && unicode[0] == 'U' && unicode[1] == '+') {
                unsigned int codepoint = 0;
                std::stringstream hexss;
                hexss << std::hex << unicode.substr(2);
                hexss >> codepoint;
                ch = static_cast<wchar_t>(codepoint);
            }
        }

        if (ch != 0) {
            try {
                // Load UV coordinates (Row, Col)
                int rowVal = std::stoi(row);
                int colVal = std::stoi(col);
                s_uvCoords[ch] = UVCoord(colVal, rowVal);

                // Track max row/col for atlas dimensions
                if (colVal > s_atlasCols) s_atlasCols = colVal;
                if (rowVal > s_atlasRows) s_atlasRows = rowVal;

                // Load width ratio
                float ratio = std::stof(widthRatio);
                s_widthRatios[ch] = ratio;

                loadedCount++;
            } catch (...) {
                // Skip invalid lines
            }
        }
    }

    // Convert from max index to count (0-based to 1-based)
    s_atlasCols += 1;
    s_atlasRows += 1;

    s_metricsLoaded = true;
    spdlog::info("TextAssets: Loaded {} characters from CSV (atlas {}x{})", loadedCount, s_atlasCols, s_atlasRows);
    return true;
}

float GetWidthRatio(wchar_t ch) {
    // Try loading if not loaded yet
    if (!s_metricsLoaded) {
        LoadMetricsFromCSV();
    }

    auto it = s_widthRatios.find(ch);
    if (it != s_widthRatios.end()) {
        return it->second;
    }

    spdlog::error("TextAssets::GetWidthRatio - Character not found in CSV: '{}' (U+{:04X})",
        static_cast<char>(ch <= 127 ? ch : '?'), static_cast<unsigned int>(ch));
    return 0.0f;
}

bool IsMetricsLoaded() {
    return s_metricsLoaded;
}

int GetAtlasCols() {
    if (!s_metricsLoaded) {
        LoadMetricsFromCSV();
    }
    return s_atlasCols;
}

int GetAtlasRows() {
    if (!s_metricsLoaded) {
        LoadMetricsFromCSV();
    }
    return s_atlasRows;
}

} // namespace TextAssets
} // namespace Projectile
