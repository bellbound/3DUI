#include <catch2/catch_all.hpp>

#include "projectile/MeshPreflight.h"

#include <cstring>
#include <string>
#include <vector>

using Projectile::MeshPreflight;
using Verdict = MeshPreflight::Verdict;

// ============================================================================
// MeshPreflight Tests
// ============================================================================
//
// The fixtures below build NIF headers byte by byte, the same way the real files
// are laid out: header line, versions, export info, block type table, per-block
// type indices and sizes, string table, group count. Only the header is ever
// parsed, so a "file" here is a header plus however many block bytes we claim.

namespace {

struct NifBuilder {
    std::vector<std::uint8_t> bytes;

    void raw(const void* data, std::size_t count) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + count);
    }
    void u8(std::uint8_t v) { bytes.push_back(v); }
    void u16(std::uint16_t v) { raw(&v, 2); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void line(const std::string& text) {
        raw(text.data(), text.size());
        u8('\n');
    }
    void shortString(const std::string& text) {
        u8(static_cast<std::uint8_t>(text.size()));
        raw(text.data(), text.size());
    }
    void sizedString(const std::string& text) {
        u32(static_cast<std::uint32_t>(text.size()));
        raw(text.data(), text.size());
    }
};

struct Block {
    std::string   type;
    std::uint32_t size;
};

// A structurally valid Skyrim SE header describing the given blocks.
std::vector<std::uint8_t> BuildHeader(const std::vector<Block>& blocks,
                                      std::uint32_t bsVersion = 100,
                                      std::uint32_t nifVersion = 0x14020007) {
    NifBuilder nif;
    nif.line("Gamebryo File Format, Version 20.2.0.7");
    nif.u32(nifVersion);
    nif.u8(1);   // little endian
    nif.u32(12);  // user version
    nif.u32(static_cast<std::uint32_t>(blocks.size()));
    nif.u32(bsVersion);
    nif.shortString("tests");
    nif.shortString("");
    nif.shortString("");

    std::vector<std::string> types;
    std::vector<std::uint16_t> indices;
    for (const auto& block : blocks) {
        std::uint16_t index = 0;
        bool found = false;
        for (std::uint16_t i = 0; i < types.size(); ++i) {
            if (types[i] == block.type) {
                index = i;
                found = true;
                break;
            }
        }
        if (!found) {
            index = static_cast<std::uint16_t>(types.size());
            types.push_back(block.type);
        }
        indices.push_back(index);
    }

    nif.u16(static_cast<std::uint16_t>(types.size()));
    for (const auto& type : types) nif.sizedString(type);
    for (auto index : indices) nif.u16(index);
    for (const auto& block : blocks) nif.u32(block.size);
    nif.u32(0);  // string table count
    nif.u32(0);  // longest string
    nif.u32(0);  // group count
    return nif.bytes;
}

// Header plus enough trailing bytes to satisfy every block and the footer.
MeshPreflight::Result InspectComplete(const std::vector<Block>& blocks,
                                      std::uint32_t bsVersion = 100) {
    const auto header = BuildHeader(blocks, bsVersion);
    std::uint64_t total = header.size() + 4;
    for (const auto& block : blocks) total += block.size;
    return MeshPreflight::Inspect(header.data(), header.size(), total);
}

const std::vector<Block> kStaticMesh = {
    {"BSFadeNode", 92},
    {"BSTriShape", 4096},
    {"BSLightingShaderProperty", 112},
};

const std::vector<Block> kSkinnedDynamicMesh = {
    {"NiNode", 100},
    {"BSDynamicTriShape", 4096},
    {"BSDismemberSkinInstance", 32},
    {"NiSkinData", 512},
    {"NiSkinPartition", 2048},
};

// The shape of the mesh that crashed the game: dynamic tri shapes, no skin anywhere.
const std::vector<Block> kUnskinnedDynamicMesh = {
    {"BSFadeNode", 92},
    {"BSXFlags", 8},
    {"BSDynamicTriShape", 458070},
    {"BSLightingShaderProperty", 112},
    {"NiAlphaProperty", 15},
};

}  // namespace

TEST_CASE("MeshPreflight accepts healthy meshes", "[meshpreflight]") {
    SECTION("Plain static mesh") {
        REQUIRE(InspectComplete(kStaticMesh).verdict == Verdict::Ok);
    }

    SECTION("Dynamic tri shape backed by skin data") {
        REQUIRE(InspectComplete(kSkinnedDynamicMesh).verdict == Verdict::Ok);
    }

    SECTION("Skyrim LE format is let through") {
        // Thousands of BS version 83 meshes load fine, so they must not be rejected.
        REQUIRE(InspectComplete(kStaticMesh, 83).verdict == Verdict::Ok);
    }

    SECTION("Trailing bytes after the last block are fine") {
        const auto header = BuildHeader(kStaticMesh);
        std::uint64_t total = header.size() + 4 + 92 + 4096 + 112 + 50000;
        REQUIRE(MeshPreflight::Inspect(header.data(), header.size(), total).verdict == Verdict::Ok);
    }

    SECTION("Unknown file size skips the size rule") {
        const auto header = BuildHeader(kStaticMesh);
        REQUIRE(MeshPreflight::Inspect(header.data(), header.size(), 0).verdict == Verdict::Ok);
    }
}

TEST_CASE("MeshPreflight rejects the loader crash signature", "[meshpreflight]") {
    const auto result = InspectComplete(kUnskinnedDynamicMesh);

    SECTION("Verdict names the cause") {
        REQUIRE(result.verdict == Verdict::UnskinnedDynamicShape);
    }

    SECTION("Detail points at the offending block") {
        REQUIRE(result.detail.find("block 2") != std::string::npos);
        REQUIRE(result.detail.find("BSDynamicTriShape") != std::string::npos);
    }

    SECTION("A skin block anywhere in the file clears it") {
        auto blocks = kUnskinnedDynamicMesh;
        blocks.push_back({"NiSkinPartition", 2048});
        REQUIRE(InspectComplete(blocks).verdict == Verdict::Ok);
    }
}

TEST_CASE("MeshPreflight rejects structurally broken files", "[meshpreflight]") {
    SECTION("Truncated: blocks do not fit in the file") {
        const auto header = BuildHeader(kStaticMesh);
        const auto result = MeshPreflight::Inspect(header.data(), header.size(), header.size() + 100);
        REQUIRE(result.verdict == Verdict::Truncated);
    }

    SECTION("Not a NIF at all") {
        const std::string junk = "this is not a mesh, it is a text file\n";
        const auto*       data = reinterpret_cast<const std::uint8_t*>(junk.data());
        REQUIRE(MeshPreflight::Inspect(data, junk.size(), junk.size()).verdict == Verdict::BadHeader);
    }

    SECTION("No header line in the first 128 bytes") {
        std::vector<std::uint8_t> noise(512, 0x41);
        REQUIRE(MeshPreflight::Inspect(noise.data(), noise.size(), noise.size()).verdict ==
                Verdict::BadHeader);
    }

    SECTION("Empty file") {
        REQUIRE(MeshPreflight::Inspect(nullptr, 0, 0).verdict == Verdict::Unreadable);
    }

    SECTION("Header cut off mid-parse asks for more bytes") {
        const auto header = BuildHeader(kStaticMesh);
        const auto result = MeshPreflight::Inspect(header.data(), header.size() / 2, header.size() * 4);
        REQUIRE(result.verdict == Verdict::Incomplete);
    }
}

TEST_CASE("MeshPreflight rejects meshes from other games", "[meshpreflight]") {
    SECTION("Fallout 4 / Starfield era") {
        REQUIRE(InspectComplete(kStaticMesh, 130).verdict == Verdict::UnsupportedFormat);
    }

    SECTION("Fallout 3 / New Vegas era") {
        REQUIRE(InspectComplete(kStaticMesh, 34).verdict == Verdict::UnsupportedFormat);
    }

    SECTION("Wrong NIF version") {
        const auto header = BuildHeader(kStaticMesh, 100, 0x14000005);
        const auto result = MeshPreflight::Inspect(header.data(), header.size(), header.size() + 8192);
        REQUIRE(result.verdict == Verdict::UnsupportedFormat);
    }
}

TEST_CASE("MeshPreflight path handling", "[meshpreflight]") {
    SECTION("Bare model paths get the meshes prefix") {
        REQUIRE(MeshPreflight::NormalizePath("armor\\iron\\gnd.nif") == "meshes\\armor\\iron\\gnd.nif");
    }

    SECTION("Paths that already carry it are left alone") {
        REQUIRE(MeshPreflight::NormalizePath("meshes\\3DUI\\orb.nif") == "meshes\\3dui\\orb.nif");
    }

    SECTION("Forward slashes and leading separators are normalised") {
        REQUIRE(MeshPreflight::NormalizePath("/Meshes/Clutter/Bowl.nif") == "meshes\\clutter\\bowl.nif");
    }

    SECTION("Non-mesh paths are passed through without inspection") {
        REQUIRE(MeshPreflight::IsSafeToLoad("textures\\3DUI\\icon.dds"));
        REQUIRE(MeshPreflight::IsSafeToLoad(""));
    }

    SECTION("A missing mesh is passed through, not rejected") {
        // The loader draws nothing for a model it cannot find - that is not a crash,
        // and consumers have always relied on it.
        MeshPreflight::ClearCache();
        REQUIRE(MeshPreflight::IsSafeToLoad("meshes\\3DUI\\does_not_exist_at_all.nif"));
    }
}
