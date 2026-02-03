#pragma once

#include <unordered_map>
#include <string>

namespace Projectile {
namespace TextAssets {

// =============================================================================
// Mesh Paths
// =============================================================================

// Text mesh with character geometry nodes as children
constexpr const char* TEXT_MESH_PATH = "meshes\\3DUI\\character_template.nif";

// Texture atlas path
constexpr const char* TEXT_ATLAS_PATH = "textures\\3DUI\\text_atlas_main.dds";

// Character metrics CSV (relative to SKSE plugins folder)
constexpr const char* TEXT_METRICS_CSV = "Data\\SKSE\\Plugins\\3DUI\\text_atlas_main_mapping.csv";

// =============================================================================
// Character Spacing
// =============================================================================

constexpr float BASE_LETTER_DISTANCE = 325.0f;

// Inter-character gap added to each character's width ratio
constexpr float CHAR_GAP = 0.1f;

// =============================================================================
// UV Coordinate Lookup
// =============================================================================

struct UVCoord {
    int col;  // Column in atlas (0-15)
    int row;  // Row in atlas (0-13)

    UVCoord() : col(0), row(0) {}
    UVCoord(int c, int r) : col(c), row(r) {}
};

// Get UV coordinates for a character (returns nullptr if not found)
// Loaded from CSV at runtime
const UVCoord* GetCharUV(wchar_t ch);

// Check if character is renderable
bool IsRenderableChar(wchar_t ch);

// =============================================================================
// Runtime CSV Loading
// =============================================================================

// Load character metrics from CSV file (call once at startup)
bool LoadMetricsFromCSV();

// Get width ratio for a character (0.0-1.0+ relative to cell size)
// Returns 0 and logs error if character not found
float GetWidthRatio(wchar_t ch);

// Check if metrics were loaded from CSV
bool IsMetricsLoaded();

// Get atlas dimensions (derived from max row/col in CSV)
int GetAtlasCols();
int GetAtlasRows();

} // namespace TextAssets
} // namespace Projectile
