#include "MeshPreflight.h"
#include "../log.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(TEST_ENVIRONMENT)
#include <fstream>
#endif

namespace Projectile {

namespace {

// A NIF header runs a few hundred bytes for a simple mesh and grows with the block
// count. 16 KB covers every mesh we have measured; the retry cap is there so a
// hostile file cannot talk us into reading a gigabyte.
constexpr std::size_t kFirstReadBytes = 16u * 1024u;
constexpr std::size_t kMaxHeaderBytes = 1024u * 1024u;

constexpr std::uint32_t kNifVersion2020007 = 0x14020007u;  // "20.2.0.7" - Skyrim LE, SSE and FO4
constexpr std::uint32_t kBSVersionSkyrimLE = 83u;          // oldest this engine still reads
constexpr std::uint32_t kBSVersionFallout4 = 130u;         // this and up: FO4, FO76, Starfield

// Sanity caps. Real meshes sit orders of magnitude below these, so a value above one
// is a corrupt table - and we must never size a buffer or a loop from it.
constexpr std::uint32_t kMaxBlocks = 200000u;
constexpr std::uint32_t kMaxBlockTypes = 20000u;
constexpr std::uint32_t kMaxStrings = 200000u;
constexpr std::uint32_t kMaxStringLength = 4096u;

// Block types that mean "this shape's vertices are driven by a skin". A dynamic tri
// shape is only ever set up through one of these; without any of them in the file
// the loader walks the dynamic vertex buffer through a null pointer.
bool IsSkinBlockType(const std::string& type) {
    return type == "NiSkinInstance" ||
           type == "BSDismemberSkinInstance" ||
           type == "NiSkinPartition" ||
           type == "NiSkinData" ||
           type == "BSSkin::Instance" ||
           type == "BSSkin::BoneData";
}

bool IsDynamicShapeType(const std::string& type) {
    return type.find("DynamicTriShape") != std::string::npos;
}

// The skin instance is the block that carries the bone pointer array - every bone the
// loader resolves goes through one of these.
bool IsSkinInstanceType(const std::string& type) {
    return type.find("SkinInstance") != std::string::npos || type == "BSSkin::Instance";
}

// A bone is a node block in the same file: the skin instance points at it, and at
// attach time the engine matches it to the actor's skeleton by name. Scene-graph roots
// do not count - a BSFadeNode is never a bone, and the mesh that prompted this rule is
// a BSFadeNode with nothing else nodal in it. These are the types the measurement over
// the load order was run with; add to the list rather than trimming it, since a node
// type missing from here can only cost a healthy mesh its verdict.
bool IsBoneNodeType(const std::string& type) {
    return type == "NiNode" ||
           type == "BSValueNode" ||
           type == "NiBillboardNode" ||
           type == "BSOrderedNode" ||
           type == "BSLeafAnimNode" ||
           type == "BSTreeNode";
}

// Header lines and block type names go into log messages, so keep them printable.
std::string Sanitize(const std::string& text, std::size_t maxLength = 64) {
    std::string out;
    out.reserve((std::min)(text.size(), maxLength));
    for (char ch : text) {
        if (out.size() >= maxLength) {
            out += "...";
            break;
        }
        out.push_back((ch >= 0x20 && ch < 0x7F) ? ch : '?');
    }
    return out;
}

std::string FormatNifVersion(std::uint32_t version) {
    return std::format("{}.{}.{}.{}",
        (version >> 24) & 0xFF, (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);
}

// Bounds-checked walk over the header bytes we managed to read. Two failure modes are
// kept apart on purpose: `overran` means the header is longer than the prefix we read
// and the caller should read more, `invalid` means the file itself is nonsense.
class Cursor {
public:
    Cursor(const std::uint8_t* data, std::size_t size) : m_data(data), m_size(size) {}

    bool        overran() const { return m_overran; }
    bool        invalid() const { return m_invalid; }
    bool        bad() const { return m_overran || m_invalid; }
    std::size_t pos() const { return m_pos; }

    std::uint8_t u8() {
        std::uint8_t value = 0;
        Take(&value, 1);
        return value;
    }

    std::uint16_t u16() {
        std::uint8_t bytes[2] = {};
        Take(bytes, 2);
        return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
    }

    std::uint32_t u32() {
        std::uint8_t bytes[4] = {};
        Take(bytes, 4);
        return static_cast<std::uint32_t>(bytes[0]) |
               (static_cast<std::uint32_t>(bytes[1]) << 8) |
               (static_cast<std::uint32_t>(bytes[2]) << 16) |
               (static_cast<std::uint32_t>(bytes[3]) << 24);
    }

    void skip(std::uint64_t count) {
        if (bad()) return;
        if (count > static_cast<std::uint64_t>(m_size - m_pos)) {
            m_overran = true;
            m_pos = m_size;
            return;
        }
        m_pos += static_cast<std::size_t>(count);
    }

    // The file's first line, terminated by '\n'. No terminator inside the window we
    // are allowed to scan means this is not a NIF at all, not that we read too little.
    std::string line(std::size_t maxLength) {
        if (bad()) return {};
        const std::size_t limit = (std::min)(m_size, m_pos + maxLength);
        for (std::size_t i = m_pos; i < limit; ++i) {
            if (m_data[i] == '\n') {
                std::string out(reinterpret_cast<const char*>(m_data + m_pos), i - m_pos);
                m_pos = i + 1;
                return out;
            }
        }
        if (m_pos + maxLength <= m_size) {
            m_invalid = true;
        } else {
            m_overran = true;
            m_pos = m_size;
        }
        return {};
    }

    // Export-info strings: one length byte, then that many chars.
    std::string shortString() {
        const std::uint8_t length = u8();
        return Take(length);
    }

    // Block type names and the string table: uint32 length, then that many chars.
    std::string sizedString() {
        const std::uint32_t length = u32();
        if (length > kMaxStringLength) {
            m_invalid = true;
            return {};
        }
        return Take(length);
    }

    // Same, but we only care that it is well formed - used for the string table.
    void skipSizedString() {
        const std::uint32_t length = u32();
        if (length > kMaxStringLength) {
            m_invalid = true;
            return;
        }
        skip(length);
    }

private:
    void Take(std::uint8_t* out, std::size_t count) {
        if (bad()) return;
        if (count > m_size - m_pos) {
            m_overran = true;
            m_pos = m_size;
            return;
        }
        std::memcpy(out, m_data + m_pos, count);
        m_pos += count;
    }

    std::string Take(std::size_t count) {
        if (bad()) return {};
        if (count > m_size - m_pos) {
            m_overran = true;
            m_pos = m_size;
            return {};
        }
        std::string out(reinterpret_cast<const char*>(m_data + m_pos), count);
        m_pos += count;
        // Strings in the header are stored with their trailing NUL for some writers.
        while (!out.empty() && out.back() == '\0') out.pop_back();
        return out;
    }

    const std::uint8_t* m_data = nullptr;
    std::size_t         m_size = 0;
    std::size_t         m_pos = 0;
    bool                m_overran = false;
    bool                m_invalid = false;
};

// Reads the first `want` bytes of a mesh and reports the size of the whole file.
// Returns false only when the mesh cannot be opened at all.
#if !defined(TEST_ENVIRONMENT)
bool ReadHeaderBytes(const std::string& path, std::size_t want,
                     std::vector<std::uint8_t>& out, std::uint64_t& fileSize) {
    out.clear();
    fileSize = 0;

    RE::BSResourceNiBinaryStream stream(path.c_str());
    if (!stream.good()) return false;

    // BSResourceNiBinaryStream::get_info is a no-op in CommonLibSSE-NG, so take the
    // size off the underlying resource stream. BSA entries report their uncompressed
    // size there, which is the size the parse needs.
    if (stream.stream) {
        fileSize = stream.stream->totalSize;
    }

    const std::size_t toRead = fileSize > 0
        ? static_cast<std::size_t>((std::min)(static_cast<std::uint64_t>(want), fileSize))
        : want;
    if (toRead == 0) return true;

    out.resize(toRead);
    if (stream.read(reinterpret_cast<char*>(out.data()), static_cast<std::uint32_t>(toRead))) {
        return true;
    }

    // read() is all-or-nothing, so a stream that misreports its size leaves us with
    // nothing. Fall back to byte-at-a-time: slower, but it yields an exact prefix and,
    // when it stops early, an exact file size.
    out.clear();
    RE::BSResourceNiBinaryStream retry(path.c_str());
    if (!retry.good()) return false;

    out.reserve((std::min)(want, kFirstReadBytes));
    char ch = 0;
    while (out.size() < want && retry.get(ch)) {
        out.push_back(static_cast<std::uint8_t>(ch));
    }
    fileSize = out.size() < want ? out.size() : 0;  // short read means we saw EOF
    return !out.empty();
}
#else
// Tests run without the game's resource system, so read straight off disk.
bool ReadHeaderBytes(const std::string& path, std::size_t want,
                     std::vector<std::uint8_t>& out, std::uint64_t& fileSize) {
    out.clear();
    fileSize = 0;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;

    const auto size = file.tellg();
    if (size < 0) return false;
    fileSize = static_cast<std::uint64_t>(size);

    const std::size_t toRead = static_cast<std::size_t>((std::min)(static_cast<std::uint64_t>(want), fileSize));
    out.resize(toRead);
    file.seekg(0);
    if (toRead > 0) file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(toRead));
    return true;
}
#endif

std::mutex& CacheMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, bool>& Cache() {
    static std::unordered_map<std::string, bool> cache;
    return cache;
}

}  // namespace

MeshPreflight::Result MeshPreflight::Inspect(const std::uint8_t* data, std::size_t available,
                                             std::uint64_t fileSize) {
    if (!data || available == 0) {
        return {Verdict::Unreadable, "file is empty"};
    }

    Cursor cursor(data, available);

    const std::string headerLine = cursor.line(128);
    if (cursor.invalid()) {
        return {Verdict::BadHeader, "no NIF header line in the first 128 bytes"};
    }
    if (cursor.overran()) {
        return {Verdict::Incomplete, "header line is longer than the bytes read"};
    }
    if (headerLine.find("NetImmerse File Format") != std::string::npos) {
        return {Verdict::UnsupportedFormat, "NetImmerse-era NIF (Morrowind/Oblivion)"};
    }
    if (headerLine.find("Gamebryo File Format") == std::string::npos) {
        return {Verdict::BadHeader, std::format("not a NIF (header line: '{}')", Sanitize(headerLine))};
    }

    const std::uint32_t version = cursor.u32();
    if (!cursor.bad() && version != kNifVersion2020007) {
        return {Verdict::UnsupportedFormat,
                std::format("NIF version {} - this engine reads 20.2.0.7", FormatNifVersion(version))};
    }

    const std::uint8_t endian = cursor.u8();
    if (!cursor.bad() && endian != 1) {
        return {Verdict::UnsupportedFormat, "big-endian NIF (console build)"};
    }

    const std::uint32_t userVersion = cursor.u32();
    if (!cursor.bad() && userVersion != 11 && userVersion != 12) {
        return {Verdict::UnsupportedFormat, std::format("user version {}", userVersion)};
    }

    const std::uint32_t blockCount = cursor.u32();
    if (!cursor.bad() && (blockCount == 0 || blockCount > kMaxBlocks)) {
        return {Verdict::BadHeader, std::format("block count {}", blockCount)};
    }

    const std::uint32_t bsVersion = cursor.u32();
    if (!cursor.bad() && bsVersion >= kBSVersionFallout4) {
        return {Verdict::UnsupportedFormat,
                std::format("BS version {} - Fallout 4 / Starfield era mesh", bsVersion)};
    }
    if (!cursor.bad() && bsVersion < kBSVersionSkyrimLE) {
        return {Verdict::UnsupportedFormat,
                std::format("BS version {} - Fallout 3 / New Vegas era mesh", bsVersion)};
    }

    // Export info: author, process script, export script.
    for (int i = 0; i < 3; ++i) {
        cursor.shortString();
    }

    const std::uint32_t blockTypeCount = cursor.u16();
    if (!cursor.bad() && (blockTypeCount == 0 || blockTypeCount > kMaxBlockTypes)) {
        return {Verdict::BadHeader, std::format("block type count {}", blockTypeCount)};
    }

    std::vector<std::string> blockTypes;
    if (!cursor.bad()) {
        blockTypes.reserve(blockTypeCount);
        for (std::uint32_t i = 0; i < blockTypeCount && !cursor.bad(); ++i) {
            blockTypes.push_back(cursor.sizedString());
        }
    }

    // Which type each block is, and how many bytes each one claims.
    std::vector<std::uint16_t> blockTypeIndices;
    if (!cursor.bad()) {
        blockTypeIndices.reserve(blockCount);
        for (std::uint32_t i = 0; i < blockCount && !cursor.bad(); ++i) {
            const std::uint16_t index = cursor.u16();
            if (!cursor.bad() && index >= blockTypes.size()) {
                return {Verdict::BadHeader,
                        std::format("block {} has type index {} of {}", i, index, blockTypes.size())};
            }
            blockTypeIndices.push_back(index);
        }
    }

    std::uint64_t blockBytes = 0;
    for (std::uint32_t i = 0; i < blockCount && !cursor.bad(); ++i) {
        blockBytes += cursor.u32();
    }

    const std::uint32_t stringCount = cursor.u32();
    cursor.u32();  // longest string in the table - not something we need
    if (!cursor.bad() && stringCount > kMaxStrings) {
        return {Verdict::BadHeader, std::format("string table count {}", stringCount)};
    }
    for (std::uint32_t i = 0; i < stringCount && !cursor.bad(); ++i) {
        cursor.skipSizedString();
    }

    const std::uint32_t groupCount = cursor.u32();
    if (!cursor.bad() && groupCount > kMaxBlocks) {
        return {Verdict::BadHeader, std::format("group count {}", groupCount)};
    }
    cursor.skip(static_cast<std::uint64_t>(groupCount) * 4u);

    if (cursor.invalid()) {
        return {Verdict::BadHeader, "header does not parse within its own bounds"};
    }
    if (cursor.overran()) {
        return {Verdict::Incomplete, "header is longer than the bytes read"};
    }

    // Everything past the header is block data plus the footer, and the footer is at
    // least the root count. If the blocks alone do not fit, the loader reads off the
    // end of the file. Extra bytes at the end are fine and common, so only the
    // overrun direction counts.
    const std::uint64_t headerEnd = cursor.pos();
    const std::uint64_t needed = headerEnd + blockBytes + 4u;
    if (fileSize > 0 && needed > fileSize) {
        return {Verdict::Truncated,
                std::format("header + {} blocks need {} bytes, file holds {}",
                    blockCount, needed, fileSize)};
    }

    // Both crash signatures come out of the block table alone, so they share one walk.
    bool        sawSkin = false;
    bool        sawBoneNode = false;
    int         dynamicBlock = -1;
    std::string dynamicType;
    int         skinInstanceBlock = -1;
    std::string skinInstanceType;
    for (std::size_t i = 0; i < blockTypeIndices.size(); ++i) {
        const std::string& type = blockTypes[blockTypeIndices[i]];
        if (IsSkinBlockType(type)) {
            sawSkin = true;
        }
        if (skinInstanceBlock < 0 && IsSkinInstanceType(type)) {
            skinInstanceBlock = static_cast<int>(i);
            skinInstanceType = type;
        }
        if (!sawBoneNode && IsBoneNodeType(type)) {
            sawBoneNode = true;
        }
        if (dynamicBlock < 0 && IsDynamicShapeType(type)) {
            dynamicBlock = static_cast<int>(i);
            dynamicType = type;
        }
    }

    // A dynamic tri shape with no skin anywhere in the file.
    if (!sawSkin && dynamicBlock >= 0) {
        return {Verdict::UnskinnedDynamicShape,
                std::format("block {} is {} but the file has no skin data", dynamicBlock,
                    Sanitize(dynamicType))};
    }

    // A skin instance with no node anywhere for its bones to point at.
    if (skinInstanceBlock >= 0 && !sawBoneNode) {
        return {Verdict::SkinnedWithoutBones,
                std::format("block {} is {} but the file has no node to serve as a bone",
                    skinInstanceBlock, Sanitize(skinInstanceType))};
    }

    return {Verdict::Ok, {}};
}

MeshPreflight::Result MeshPreflight::Check(const std::string& modelPath) {
    const std::string path = NormalizePath(modelPath);

    std::vector<std::uint8_t> bytes;
    std::uint64_t             fileSize = 0;
    if (!ReadHeaderBytes(path, kFirstReadBytes, bytes, fileSize)) {
        return {Verdict::Unreadable, "resource system could not open it"};
    }

    Result result = Inspect(bytes.data(), bytes.size(), fileSize);

    // Big meshes can carry a header past our first read. Try once more with the whole
    // thing, up to the cap.
    if (result.verdict == Verdict::Incomplete && fileSize > bytes.size()) {
        const std::size_t want =
            static_cast<std::size_t>((std::min)(fileSize, static_cast<std::uint64_t>(kMaxHeaderBytes)));
        if (want > bytes.size() && ReadHeaderBytes(path, want, bytes, fileSize)) {
            result = Inspect(bytes.data(), bytes.size(), fileSize);
        }
    }
    if (result.verdict == Verdict::Incomplete) {
        result.verdict = Verdict::BadHeader;
    }

    return result;
}

bool MeshPreflight::IsSafeToLoad(const std::string& modelPath) {
    if (modelPath.empty()) return true;

    // Anything that is not a mesh never reaches the model loader.
    if (modelPath.size() < 4 ||
        _stricmp(modelPath.c_str() + modelPath.size() - 4, ".nif") != 0) {
        return true;
    }

    const std::string key = NormalizePath(modelPath);

    {
        std::lock_guard<std::mutex> lock(CacheMutex());
        auto it = Cache().find(key);
        if (it != Cache().end()) {
            return it->second;
        }
    }

    const Result result = Check(modelPath);

    // A mesh the resource system cannot open is not a crash risk - the loader simply
    // draws nothing, exactly as it did before this check existed - so let it through
    // and keep the behaviour consumers already rely on.
    const bool safe = result.verdict == Verdict::Ok || result.verdict == Verdict::Unreadable;

    if (result.verdict == Verdict::Unreadable) {
        spdlog::debug("[MeshPreflight] '{}' could not be opened ({}) - passing it through",
            key, result.detail);
    } else if (!safe) {
        spdlog::error("[MeshPreflight] REJECTED '{}': {} - {}", key, Describe(result.verdict),
            result.detail);
    } else {
        spdlog::trace("[MeshPreflight] '{}' passed inspection", key);
    }

    {
        std::lock_guard<std::mutex> lock(CacheMutex());
        Cache()[key] = safe;
    }
    return safe;
}

std::string MeshPreflight::NormalizePath(const std::string& modelPath) {
    std::string path;
    path.reserve(modelPath.size() + 7);

    for (char ch : modelPath) {
        path.push_back(ch == '/' ? '\\' : ch);
    }
    while (!path.empty() && (path.front() == '\\' || path.front() == ' ')) {
        path.erase(path.begin());
    }

    constexpr std::string_view prefix = "meshes\\";
    const bool hasPrefix = path.size() >= prefix.size() &&
                           _strnicmp(path.c_str(), prefix.data(), prefix.size()) == 0;
    if (!hasPrefix) {
        path.insert(0, prefix);
    }

    // Cache keys are compared as written, so fold case once here.
    std::transform(path.begin(), path.end(), path.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return path;
}

const char* MeshPreflight::Describe(Verdict verdict) {
    switch (verdict) {
        case Verdict::Ok:                    return "ok";
        case Verdict::Unreadable:            return "unreadable";
        case Verdict::BadHeader:             return "bad header";
        case Verdict::UnsupportedFormat:     return "unsupported format";
        case Verdict::Truncated:             return "truncated";
        case Verdict::UnskinnedDynamicShape: return "unskinned dynamic tri shape (known loader crash)";
        case Verdict::SkinnedWithoutBones:   return "skinned shape with no bones (known loader crash)";
        case Verdict::Incomplete:            return "incomplete header";
        default:                             return "unknown";
    }
}

void MeshPreflight::ClearCache() {
    std::lock_guard<std::mutex> lock(CacheMutex());
    Cache().clear();
}

std::size_t MeshPreflight::CacheSize() {
    std::lock_guard<std::mutex> lock(CacheMutex());
    return Cache().size();
}

}  // namespace Projectile
