#pragma once

#if !defined(TEST_ENVIRONMENT)
#include "RE/Skyrim.h"
#else
#include "TestStubs.h"
#endif

#include <cstdint>
#include <string>

namespace Projectile {

// Structural screen for a .nif before we hand its path to the game's model loader.
//
// 3DUI stamps arbitrary mod-authored meshes onto projectile forms and fires them, so
// it loads meshes the game would otherwise never touch: an armour ground object that
// only appears when the item is dropped, a static that never spawns in a cell. A
// malformed one takes the whole process down inside the loader, on a background
// thread where no exception handler of ours can reach it - a hair ground mesh out of
// a BSA did exactly that, dying in BSDynamicTriShape::LoadBinary.
//
// So we read the NIF header ourselves first and refuse the meshes that carry a known
// crash signature. Only the header is read (a few KB), and the verdict is cached per
// path, so a mesh is inspected once per session no matter how often it is shown.
//
// The rules are deliberately conservative. Measured over 86,767 loose meshes in a
// large load order they reject 5 files, all genuinely unloadable: one hair ground
// mesh with the crash signature below, and four Fallout 4 / Starfield skeletons that
// Skyrim cannot read at all. Meshes that merely look unusual - Skyrim LE format
// (BS version 83), junk bytes appended after the last block - are let through,
// because thousands of them load fine every day.
class MeshPreflight {
public:
    enum class Verdict {
        Ok,
        // Resource system could not open it, or it is empty. NOT a crash risk: a
        // model the loader cannot find just draws nothing, exactly as before.
        Unreadable,
        // Not a Gamebryo NIF, or the header does not parse within its own bounds.
        BadHeader,
        // A NIF, but not one this engine reads: Fallout 3/NV/4, Starfield, big-endian.
        UnsupportedFormat,
        // The block table promises more bytes than the file actually holds, so the
        // loader would read off the end of the buffer.
        Truncated,
        // A *DynamicTriShape with no skin data anywhere in the file. Dynamic shapes
        // carry a second, CPU-side vertex buffer that is only ever set up through a
        // skin instance; standalone the loader walks it through a null pointer. This
        // is the signature of the crash that prompted this check - typically a hair
        // mesh converted with the wrong NIF Optimizer setting.
        UnskinnedDynamicShape,
        // The header did not fit in the bytes we read. Internal to Check(); callers
        // see BadHeader once the retry with a larger read has also come up short.
        Incomplete,
    };

    struct Result {
        Verdict     verdict = Verdict::Ok;
        std::string detail;  // specifics for the log: block index, byte counts, ...
    };

    // True if this path may be handed to the game's model loader. Cached per path;
    // logs a warning the first time a path is rejected. An empty or non-.nif path
    // passes untouched - those never reach the model loader in the first place.
    static bool IsSafeToLoad(const std::string& modelPath);

    // Uncached, silent. Opens the mesh through the resource system, so it sees loose
    // files and BSA entries exactly as the loader will.
    static Result Check(const std::string& modelPath);

    // Pure header parse over bytes already in memory. `fileSize` is the size of the
    // whole file (0 if unknown, which skips the truncation rule); `available` is how
    // much of it `data` holds.
    static Result Inspect(const std::uint8_t* data, std::size_t available, std::uint64_t fileSize);

    // TESModel paths are relative to Data\Meshes\, but 3DUI's own callers pass paths
    // that already carry the "meshes\" prefix. The resource system wants the full
    // form, so both are normalised to it.
    static std::string NormalizePath(const std::string& modelPath);

    static const char* Describe(Verdict verdict);

    // Test/diagnostic helpers.
    static void        ClearCache();
    static std::size_t CacheSize();
};

}  // namespace Projectile
