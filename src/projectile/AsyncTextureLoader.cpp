#include "AsyncTextureLoader.h"
#include "../log.h"
#include <cstdint>
#include <cstring>
#include <sstream>

namespace Projectile {

// Placeholder texture path - transparent texture for visual consistency while loading
static constexpr const char* PLACEHOLDER_TEXTURE_PATH = "textures\\VRDressup\\transparent.dds";

namespace
{
    
AsyncTextureLoader::AsyncTextureLoader() = default;

AsyncTextureLoader::~AsyncTextureLoader() {
    Shutdown();
}

AsyncTextureLoader& AsyncTextureLoader::GetInstance() {
    static AsyncTextureLoader instance;
    return instance;
}

// =============================================================================
// Lifecycle
// =============================================================================

void AsyncTextureLoader::Start() {
    if (m_running.load()) {
        spdlog::warn("AsyncTextureLoader::Start - Already running");
        return;
    }

    m_shutdown.store(false);
    m_running.store(true);

    m_workerThread = std::thread(&AsyncTextureLoader::WorkerThreadFunc, this);

    spdlog::info("AsyncTextureLoader: Started worker thread");
    spdlog::trace("AsyncTextureLoader: State after start running={} pending={} completed={} cache={}",
        m_running.load(), GetPendingCount(), GetCompletedCount(), GetCacheSize());
}

void AsyncTextureLoader::Shutdown() {
    if (!m_running.load()) {
        return;
    }

    spdlog::info("AsyncTextureLoader: Shutting down...");

    // Signal shutdown
    m_shutdown.store(true);
    m_running.store(false);

    // Wake up worker thread so it can exit
    m_workAvailable.notify_all();

    // Wait for worker to finish
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // Clear queues
    {
        std::lock_guard<std::mutex> lock(m_loadQueueMutex);
        while (!m_loadQueue.empty()) m_loadQueue.pop();
        m_pendingSet.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_completedQueueMutex);
        while (!m_completedQueue.empty()) m_completedQueue.pop();
    }
    {
        std::lock_guard<std::mutex> lock(m_callbacksMutex);
        m_callbacks.clear();
    }

    spdlog::info("AsyncTextureLoader: Shutdown complete");
}

// =============================================================================
// Worker Thread
// =============================================================================

void AsyncTextureLoader::WorkerThreadFunc() {
    // Log thread start with ID
    {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        spdlog::info("AsyncTextureLoader: Worker thread STARTED (thread ID: {})", oss.str());
    }

    while (!m_shutdown.load()) {
        std::string texturePath;

        // Wait for work or shutdown signal
        {
            std::unique_lock<std::mutex> lock(m_loadQueueMutex);
            m_workAvailable.wait(lock, [this] {
                return m_shutdown.load() || !m_loadQueue.empty();
            });

            if (m_shutdown.load()) {
                break;
            }

            if (m_loadQueue.empty()) {
                continue;
            }

            texturePath = m_loadQueue.front();
            m_loadQueue.pop();
            // Note: Keep in m_pendingSet until load completes

            spdlog::trace("AsyncTextureLoader: [Worker] Dequeued '{}' (queue={} pending={})",
                texturePath, m_loadQueue.size(), m_pendingSet.size());
        }

        // Load texture (this is the slow part - disk I/O)
        LoadResult result;
        result.path = texturePath;
        result.success = false;

        spdlog::debug("AsyncTextureLoader: [Worker] Loading texture '{}'...", texturePath);

        try {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
       

                RE::NiPointer<RE::NiTexture> texture;
                RE::BSShaderManager::GetTexture(texturePath.c_str(), true, texture, false);

                if (texture) {
                    result.texture = texture;
                    result.success = true;
                    spdlog::debug("AsyncTextureLoader: [Worker] SUCCESS loading '{}' (tex={})",
                        texturePath, static_cast<const void*>(texture.get()));
                } else {
                    spdlog::warn("AsyncTextureLoader: [Worker] FAILED to load '{}' (GetTexture returned null)",
                        texturePath);
                }
          
        } catch (const std::exception& e) {
            spdlog::error("AsyncTextureLoader: [Worker] EXCEPTION loading '{}': {}",
                texturePath, e.what());
        } catch (...) {
            spdlog::error("AsyncTextureLoader: [Worker] UNKNOWN EXCEPTION loading '{}'",
                texturePath);
        }

        // Queue result for main thread
        {
            std::lock_guard<std::mutex> lock(m_completedQueueMutex);
            m_completedQueue.push(std::move(result));
        }

        // Remove from pending set
        {
            std::lock_guard<std::mutex> lock(m_loadQueueMutex);
            m_pendingSet.erase(texturePath);
            spdlog::trace("AsyncTextureLoader: [Worker] Completed '{}' (pending now={})",
                texturePath, m_pendingSet.size());
        }
    }

    // Log thread end
    {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        spdlog::info("AsyncTextureLoader: Worker thread ENDED (thread ID: {})", oss.str());
    }
}

// =============================================================================
// Texture Requesting
// =============================================================================

RE::NiPointer<RE::NiTexture> AsyncTextureLoader::RequestTexture(
    const std::string& texturePath,
    TextureReadyCallback onReady)
{
    spdlog::trace("AsyncTextureLoader::RequestTexture - '{}' (callback={})",
        texturePath, onReady ? "yes" : "no");

    // 1. Check cache first (fast path)
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto cacheIt = m_cache.find(texturePath);
        if (cacheIt != m_cache.end()) {
            spdlog::trace("AsyncTextureLoader::RequestTexture - Cache HIT '{}' (tex={})",
                texturePath, static_cast<const void*>(cacheIt->second.get()));
            // Already loaded - return immediately
            if (onReady) {
                spdlog::trace("AsyncTextureLoader::RequestTexture - Firing callback inline for cached '{}'",
                    texturePath);
                onReady(cacheIt->second);
            }
            return cacheIt->second;
        }
    }

    // 2. Check if already queued/loading
    // NOTE: GetPlaceholder() must be called OUTSIDE the lock to avoid deadlock.
    // GetPlaceholder() may call BSShaderManager::GetTexture() which has internal locks.
    // The worker thread also calls BSShaderManager::GetTexture() then acquires m_loadQueueMutex,
    // so holding m_loadQueueMutex while calling game functions creates a deadlock risk.
    bool alreadyPending = false;
    {
        std::lock_guard<std::mutex> lock(m_loadQueueMutex);
        if (m_pendingSet.find(texturePath) != m_pendingSet.end()) {
            spdlog::trace("AsyncTextureLoader::RequestTexture - Already pending '{}'", texturePath);
            alreadyPending = true;
            // Already pending - just add callback
            if (onReady) {
                std::lock_guard<std::mutex> cbLock(m_callbacksMutex);
                m_callbacks[texturePath].push_back(onReady);
                spdlog::trace("AsyncTextureLoader::RequestTexture - Added callback (pending callbacks={} for '{}')",
                    m_callbacks[texturePath].size(), texturePath);
            }
        } else {
            // 3. Queue for loading
            m_loadQueue.push(texturePath);
            m_pendingSet.insert(texturePath);
            spdlog::trace("AsyncTextureLoader::RequestTexture - Queued '{}' (queue={} pending={})",
                texturePath, m_loadQueue.size(), m_pendingSet.size());
        }
    }

    // Return placeholder for already-pending textures (outside lock to avoid deadlock)
    if (alreadyPending) {
        return GetPlaceholder();
    }

    // Add callback if provided
    if (onReady) {
        std::lock_guard<std::mutex> cbLock(m_callbacksMutex);
        m_callbacks[texturePath].push_back(onReady);
        spdlog::trace("AsyncTextureLoader::RequestTexture - Added callback (callbacks={} for '{}')",
            m_callbacks[texturePath].size(), texturePath);
    }

    // Wake up worker thread
    m_workAvailable.notify_one();

    spdlog::debug("AsyncTextureLoader: Queued '{}' for async loading", texturePath);

    return GetPlaceholder();
}

bool AsyncTextureLoader::IsTextureReady(const std::string& texturePath) const {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_cache.find(texturePath) != m_cache.end();
}

bool AsyncTextureLoader::IsTextureLoading(const std::string& texturePath) const {
    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
    return m_pendingSet.find(texturePath) != m_pendingSet.end();
}

// =============================================================================
// Frame Processing (Main Thread)
// =============================================================================

size_t AsyncTextureLoader::ProcessCompletedLoads() {
    size_t processed = 0;

    // Process all completed loads this frame
    while (true) {
        LoadResult result;

        // Pop from completed queue
        {
            std::lock_guard<std::mutex> lock(m_completedQueueMutex);
            if (m_completedQueue.empty()) {
                break;
            }
            result = std::move(m_completedQueue.front());
            m_completedQueue.pop();
        }

        spdlog::trace("AsyncTextureLoader: [Main] Processing '{}' (success={}, tex={})",
            result.path, result.success,
            static_cast<const void*>(result.texture.get()));

        // Add to cache
        if (result.success && result.texture) {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            m_cache[result.path] = result.texture;
            spdlog::debug("AsyncTextureLoader: [Main] Cached '{}' ({} total cached)",
                result.path, m_cache.size());
        }

        // Fire callbacks (always, even on failure - callback receives nullptr)
        FireCallbacks(result.path, result.texture);

        ++processed;
    }

    if (processed > 0) {
        spdlog::trace("AsyncTextureLoader: [Main] Processed {} completed loads", processed);
    }

    return processed;
}

size_t AsyncTextureLoader::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
    return m_pendingSet.size();
}

size_t AsyncTextureLoader::GetCompletedCount() const {
    std::lock_guard<std::mutex> lock(m_completedQueueMutex);
    return m_completedQueue.size();
}

// =============================================================================
// Preloading
// =============================================================================

void AsyncTextureLoader::PreloadTextures(const std::vector<std::string>& texturePaths) {
    for (const auto& path : texturePaths) {
        // RequestTexture without callback just queues the load
        RequestTexture(path, nullptr);
    }
    spdlog::info("AsyncTextureLoader: Queued {} textures for preload", texturePaths.size());
}

// =============================================================================
// Cache Management
// =============================================================================

void AsyncTextureLoader::ClearCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    size_t count = m_cache.size();
    m_cache.clear();
    spdlog::info("AsyncTextureLoader: Cleared cache ({} textures)", count);
}

size_t AsyncTextureLoader::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_cache.size();
}

RE::NiPointer<RE::NiTexture> AsyncTextureLoader::GetCachedTexture(const std::string& texturePath) const {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_cache.find(texturePath);
    if (it != m_cache.end()) {
        return it->second;
    }
    return nullptr;
}

// =============================================================================
// Internal Helpers
// =============================================================================

RE::NiPointer<RE::NiTexture> AsyncTextureLoader::GetPlaceholder() {
    // Double-checked locking for thread-safe lazy initialization
    if (!m_placeholderInitialized.load()) {
        std::lock_guard<std::mutex> lock(m_placeholderMutex);
        if (!m_placeholderInitialized.load()) {
            spdlog::debug("AsyncTextureLoader: Loading placeholder texture '{}'...", PLACEHOLDER_TEXTURE_PATH);

            try {
                RE::BSShaderManager::GetTexture(PLACEHOLDER_TEXTURE_PATH, true, m_placeholder, false);

                if (m_placeholder) {
                    spdlog::info("AsyncTextureLoader: Placeholder texture loaded successfully");
                } else {
                    spdlog::error("AsyncTextureLoader: FAILED to load placeholder texture '{}'",
                        PLACEHOLDER_TEXTURE_PATH);
                }
            } catch (const std::exception& e) {
                spdlog::error("AsyncTextureLoader: EXCEPTION loading placeholder: {}", e.what());
            } catch (...) {
                spdlog::error("AsyncTextureLoader: UNKNOWN EXCEPTION loading placeholder");
            }

            m_placeholderInitialized.store(true);
        }
    }

    if (!m_placeholder) {
        spdlog::trace("AsyncTextureLoader: Placeholder texture is null");
    }

    return m_placeholder;
}

void AsyncTextureLoader::FireCallbacks(const std::string& texturePath, RE::NiPointer<RE::NiTexture> texture) {
    std::vector<TextureReadyCallback> callbacks;

    // Extract callbacks (thread-safe)
    {
        std::lock_guard<std::mutex> lock(m_callbacksMutex);
        auto it = m_callbacks.find(texturePath);
        if (it != m_callbacks.end()) {
            callbacks = std::move(it->second);
            m_callbacks.erase(it);
        }
    }

    if (!callbacks.empty()) {
        spdlog::trace("AsyncTextureLoader: Firing {} callbacks for '{}' (tex={})",
            callbacks.size(), texturePath, static_cast<const void*>(texture.get()));
    }

    // Fire callbacks (outside lock)
    for (auto& callback : callbacks) {
        if (callback) {
            try {
                callback(texture);
            } catch (const std::exception& e) {
                spdlog::error("AsyncTextureLoader: Callback exception for '{}': {}",
                    texturePath, e.what());
            } catch (...) {
                spdlog::error("AsyncTextureLoader: Unknown callback exception for '{}'",
                    texturePath);
            }
        }
    }

    if (!callbacks.empty()) {
        spdlog::trace("AsyncTextureLoader: Fired {} callbacks for '{}'",
            callbacks.size(), texturePath);
    }
}

} // namespace Projectile
