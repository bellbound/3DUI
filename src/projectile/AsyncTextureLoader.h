#pragma once

#include "RE/Skyrim.h"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

namespace Projectile {

// Callback type for texture ready notification
// Called on main thread when texture load completes
using TextureReadyCallback = std::function<void(RE::NiPointer<RE::NiTexture>)>;

// =============================================================================
// AsyncTextureLoader
// Provides non-blocking texture loading using a dedicated worker thread.
//
// Design:
// - RequestTexture() returns immediately with placeholder or cached texture
// - Worker thread calls BSShaderManager::GetTexture() asynchronously
// - ProcessCompletedLoads() (main thread) swaps in loaded textures and fires callbacks
//
// Thread Safety:
// - RequestTexture() can be called from any thread
// - ProcessCompletedLoads() MUST be called from main thread only
// - All callback functions are invoked on main thread
// =============================================================================
class AsyncTextureLoader {
public:
    // Singleton access
    static AsyncTextureLoader& GetInstance();

    // =========================================================================
    // Lifecycle
    // =========================================================================

    // Start the worker thread (call once during plugin init)
    void Start();

    // Stop the worker thread and cleanup (call during shutdown)
    void Shutdown();

    // Check if the loader is running
    bool IsRunning() const { return m_running.load(); }

    // =========================================================================
    // Texture Requesting
    // =========================================================================

    // Request a texture asynchronously
    // Returns immediately with:
    //   - Cached texture if available (instant)
    //   - Placeholder texture if load is pending/queued
    // Optional callback fires on main thread when real texture is ready
    RE::NiPointer<RE::NiTexture> RequestTexture(
        const std::string& texturePath,
        TextureReadyCallback onReady = nullptr);

    // Check if a texture is fully loaded (in cache)
    bool IsTextureReady(const std::string& texturePath) const;

    // Check if a texture is currently loading or queued
    bool IsTextureLoading(const std::string& texturePath) const;

    // =========================================================================
    // Frame Processing (call from main Update loop)
    // =========================================================================

    // Process completed texture loads - call once per frame from main thread
    // Moves loaded textures to cache and fires ready callbacks
    // Returns number of textures processed this frame
    size_t ProcessCompletedLoads();

    // Get number of textures waiting to load
    size_t GetPendingCount() const;

    // Get number of completed loads waiting to be processed
    size_t GetCompletedCount() const;

    // =========================================================================
    // Preloading (for known textures - non-blocking)
    // =========================================================================

    // Queue multiple textures for preloading
    void PreloadTextures(const std::vector<std::string>& texturePaths);

    // =========================================================================
    // Cache Management
    // =========================================================================

    // Clear all cached textures (frees memory, does NOT stop pending loads)
    void ClearCache();

    // Get cache statistics
    size_t GetCacheSize() const;

    // Direct cache access (for texture that's already loaded)
    RE::NiPointer<RE::NiTexture> GetCachedTexture(const std::string& texturePath) const;

private:
    AsyncTextureLoader();
    ~AsyncTextureLoader();
    AsyncTextureLoader(const AsyncTextureLoader&) = delete;
    AsyncTextureLoader& operator=(const AsyncTextureLoader&) = delete;

    // Result of an async texture load
    struct LoadResult {
        std::string path;
        RE::NiPointer<RE::NiTexture> texture;  // nullptr on failure
        bool success;
    };

    // Worker thread function
    void WorkerThreadFunc();

    // Internal: Get or create placeholder texture
    RE::NiPointer<RE::NiTexture> GetPlaceholder();

    // Internal: Fire callbacks for a loaded texture (must be on main thread)
    void FireCallbacks(const std::string& texturePath, RE::NiPointer<RE::NiTexture> texture);

    // =========================================================================
    // State
    // =========================================================================

    // Loaded texture cache (accessed from main thread after load completes)
    std::unordered_map<std::string, RE::NiPointer<RE::NiTexture>> m_cache;
    mutable std::mutex m_cacheMutex;

    // Request queue: main thread -> worker thread
    std::queue<std::string> m_loadQueue;
    std::unordered_set<std::string> m_pendingSet;  // Fast lookup for queued/loading
    mutable std::mutex m_loadQueueMutex;
    std::condition_variable m_workAvailable;

    // Completed queue: worker thread -> main thread
    std::queue<LoadResult> m_completedQueue;
    mutable std::mutex m_completedQueueMutex;

    // Callbacks waiting for texture load completion
    std::unordered_map<std::string, std::vector<TextureReadyCallback>> m_callbacks;
    std::mutex m_callbacksMutex;

    // Placeholder texture (lazy-initialized, thread-safe)
    RE::NiPointer<RE::NiTexture> m_placeholder;
    std::mutex m_placeholderMutex;
    std::atomic<bool> m_placeholderInitialized{false};

    // Worker thread
    std::thread m_workerThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shutdown{false};
};

} // namespace Projectile
