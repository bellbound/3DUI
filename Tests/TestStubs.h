#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace std::literals;

// =============================================================================
// spdlog stubs - logging implementation for tests
// =============================================================================
#include <iostream>
#include <format>

namespace spdlog {
    namespace detail {
        enum class level { trace, debug, info, warn, error, critical };

        inline const char* level_name(level lvl) {
            switch (lvl) {
                case level::trace: return "TRACE";
                case level::debug: return "DEBUG";
                case level::info: return "INFO";
                case level::warn: return "WARN";
                case level::error: return "ERROR";
                case level::critical: return "CRITICAL";
                default: return "???";
            }
        }

        template<typename... Args>
        inline void log(level lvl, std::format_string<Args...> fmt, Args&&... args) {
            std::cerr << "[" << level_name(lvl) << "] "
                      << std::format(fmt, std::forward<Args>(args)...) << std::endl;
        }

        // Overload for simple string messages (no format args)
        inline void log(level lvl, const char* msg) {
            std::cerr << "[" << level_name(lvl) << "] " << msg << std::endl;
        }

        inline void log(level lvl, const std::string& msg) {
            std::cerr << "[" << level_name(lvl) << "] " << msg << std::endl;
        }
    }

    template<typename... Args>
    inline void trace(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::trace, fmt, std::forward<Args>(args)...);
    }
    inline void trace(const char* msg) { detail::log(detail::level::trace, msg); }
    inline void trace(const std::string& msg) { detail::log(detail::level::trace, msg); }

    template<typename... Args>
    inline void debug(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::debug, fmt, std::forward<Args>(args)...);
    }
    inline void debug(const char* msg) { detail::log(detail::level::debug, msg); }
    inline void debug(const std::string& msg) { detail::log(detail::level::debug, msg); }

    template<typename... Args>
    inline void info(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::info, fmt, std::forward<Args>(args)...);
    }
    inline void info(const char* msg) { detail::log(detail::level::info, msg); }
    inline void info(const std::string& msg) { detail::log(detail::level::info, msg); }

    template<typename... Args>
    inline void warn(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::warn, fmt, std::forward<Args>(args)...);
    }
    inline void warn(const char* msg) { detail::log(detail::level::warn, msg); }
    inline void warn(const std::string& msg) { detail::log(detail::level::warn, msg); }

    template<typename... Args>
    inline void error(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::error, fmt, std::forward<Args>(args)...);
    }
    inline void error(const char* msg) { detail::log(detail::level::error, msg); }
    inline void error(const std::string& msg) { detail::log(detail::level::error, msg); }

    template<typename... Args>
    inline void critical(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::critical, fmt, std::forward<Args>(args)...);
    }
    inline void critical(const char* msg) { detail::log(detail::level::critical, msg); }
    inline void critical(const std::string& msg) { detail::log(detail::level::critical, msg); }
}

// =============================================================================
// SKSE stubs
// =============================================================================
namespace SKSE {
    class TaskInterface {
    public:
        template<typename F>
        void AddTask(F&& task) {
            // In tests, just execute immediately
            task();
        }
    };

    inline TaskInterface* GetTaskInterface() {
        static TaskInterface instance;
        return &instance;
    }
}

// =============================================================================
// REL stubs (for vtable hooking)
// =============================================================================
namespace REL {
    template<typename T>
    struct Offset {
        constexpr Offset(std::uintptr_t offset) : m_offset(offset) {}
        std::uintptr_t offset() const { return m_offset; }
        std::uintptr_t address() const { return m_offset; }  // Stub: just return offset
        std::uintptr_t m_offset;
    };

    template<typename T>
    struct Relocation {
        Relocation(std::uintptr_t addr) : m_address(addr) {}
        std::uintptr_t address() const { return m_address; }
        T operator*() const { return T{}; }
        std::uintptr_t m_address;
    };

    template<typename T>
    inline void safe_write(std::uintptr_t, T) {
        // No-op in tests
    }
}

namespace RE {
    // Forward declarations for stub types
    class Actor;
    class PlayerCharacter;
    class TESNPC;
    class TESRace;
    class TESWorldSpace;
    class TESLocation;
    class TESCell;
    class BGSVoiceType;
    class TESForm;
    class TESObjectREFR;
    class ActorRuntimeData;
    class TESSceneForm;
    class ProcessLists;
    class SubtitleManager; // Forward declaration
    class SubtitleInfo;    // Forward declaration
    class BSFaceGenAnimationData; // Forward declaration
    class BSSpinLockGuard; // Forward declaration
    class BSFixedString; // Forward declaration for BSFixedString
    class Calendar; // Forward declaration for Calendar
    class TESQuest; // Forward declaration for TESQuest
    class TESDataHandler; // Forward declaration for TESDataHandler
    // === NiPoint3 Stub ===
    struct NiPoint3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        NiPoint3() = default;
        NiPoint3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

        bool operator==(const NiPoint3& other) const {
            return x == other.x && y == other.y && z == other.z;
        }

        NiPoint3 operator+(const NiPoint3& other) const {
            return NiPoint3(x + other.x, y + other.y, z + other.z);
        }

        NiPoint3 operator-(const NiPoint3& other) const {
            return NiPoint3(x - other.x, y - other.y, z - other.z);
        }

        NiPoint3 operator*(float scalar) const {
            return NiPoint3(x * scalar, y * scalar, z * scalar);
        }

        float Length() const {
            return std::sqrt(x * x + y * y + z * z);
        }
    };

    // === NiMatrix3 Stub ===
    struct NiMatrix3 {
        float entry[3][3] = {{1,0,0}, {0,1,0}, {0,0,1}};
    };

    // === NiTransform Stub ===
    struct NiTransform {
        NiMatrix3 rotate;
        NiPoint3 translate;
        float scale = 1.0f;
    };

    // === NiBound Stub ===
    struct NiBound {
        NiPoint3 center;
        float radius = 0.0f;
    };

    // === NiAVObject Stub ===
    class NiAVObject {
    public:
        NiTransform local;
        NiTransform world;
        NiBound worldBound;  // Bounding sphere for the 3D object
        NiAVObject* parent = nullptr;  // For anchor validity checks
        virtual ~NiAVObject() = default;

        NiAVObject* GetObjectByName(std::string_view) { return this; }
    };

    // === NiNode Stub ===
    class NiNode : public NiAVObject {
    public:
        struct Flags {
            bool IsHidden() const { return hidden; }
            void SetHidden(bool h) { hidden = h; }
            bool hidden = false;
        } flags;

        NiAVObject* GetObjectByName(std::string_view) { return this; }
    };

    // === FormID Stub ===
    using FormID = std::uint32_t;

    // === RefHandle Stub (what LookupByHandle accepts) ===
    using RefHandle = uint32_t;

    // === NiPointer Stub (smart pointer returned by LookupByHandle) ===
    template<typename T>
    class NiPointer {
    public:
        NiPointer() : m_ptr(nullptr) {}
        NiPointer(T* p) : m_ptr(p) {}
        NiPointer(const NiPointer&) = default;
        NiPointer& operator=(const NiPointer&) = default;

        T* get() const { return m_ptr; }
        T* operator->() const { return m_ptr; }
        T& operator*() const { return *m_ptr; }
        explicit operator bool() const { return m_ptr != nullptr; }

    private:
        T* m_ptr;
    };

    // === ObjectRefHandle Stub ===
    struct ObjectRefHandle {
        uint32_t value = 0;

        ObjectRefHandle() = default;
        explicit ObjectRefHandle(uint32_t v) : value(v) {}

        bool operator!() const { return value == 0; }
        explicit operator bool() const { return value != 0; }
        uint32_t native_handle() const { return value; }
        bool IsValid() const { return value != 0; }
    };

    // === TESForm Stub ===
    class TESForm {
    public:
        FormID formID = 0;
        virtual ~TESForm() = default;

        FormID GetFormID() const { return formID; }
        TESForm* GetBaseObject() { return this; }

        template<typename T>
        T* As() { return dynamic_cast<T*>(this); }
    };

    // === NiNPShortPoint3 Stub ===
    struct NiNPShortPoint3 {
        int16_t x = 0;
        int16_t y = 0;
        int16_t z = 0;
    };

    // === BOUND_DATA Stub ===
    struct BOUND_DATA {
        NiNPShortPoint3 boundMin;
        NiNPShortPoint3 boundMax;
    };

    // === TESBoundObject Stub ===
    class TESBoundObject : public TESForm {
    public:
        BOUND_DATA boundData;
    };

    // === BGSProjectile Stub ===
    class BGSProjectile : public TESBoundObject {
    public:
        // Stub for projectile type flags (mirrors CommonLibSSE)
        struct TypeFlags {
            uint32_t value = 0;
            uint32_t get() const { return value; }
        };

        struct Data {
            std::string model;
            float speed = 1000.0f;          // Projectile speed
            float range = 10000.0f;         // Maximum travel distance before destruction
            float gravity = 0.0f;           // Gravity effect on trajectory
            float lifetime = 10.0f;         // Maximum time in seconds before destruction
            float collisionRadius = 10.0f;  // Collision detection radius
            TypeFlags types;                // Projectile type flags
            TypeFlags flags;                // Additional flags
        } data;

        // boundData is inherited from TESBoundObject

        void SetModel(const char* path) { data.model = path ? path : ""; }
    };

    // === TESAmmo Stub ===
    class TESAmmo : public TESBoundObject {
    public:
        std::string& GetModel() { return model; }
        void SetModel(const char* path) { model = path ? path : ""; }
    private:
        std::string model;
    };

    // === TESObjectWEAP Stub ===
    class TESObjectWEAP : public TESForm {
    public:
    };

    // === BGSTextureSet Stub ===
    class BGSTextureSet : public TESForm {
    public:
        struct TexturePath {
            std::string str;
        };
        std::array<TexturePath, 8> texturePaths;  // Skyrim texture sets have up to 8 slots
    };

    // === Projectile Stub ===
    class Projectile : public TESForm {
    public:
        // Data structure matching CommonLibSSE
        struct Data {
            NiPoint3 location;
            NiPoint3 angle;
        } data;

        // Runtime flags enum (mirrors CommonLibSSE)
        enum class Flags : uint32_t {
            kNone = 0,
            kDestroyAfterHit = 1 << 15,
            kDestroyed = 1 << 25,
        };

        // Stub for EnumSet (needs to be assignable from Flags enum)
        struct FlagsSet {
            uint32_t value = 0;
            uint32_t get() const { return value; }
            FlagsSet& operator=(Flags f) { value = static_cast<uint32_t>(f); return *this; }
        };

        // Runtime data structure (matches CommonLibSSE-NG pattern)
        struct ProjectileRuntimeData {
            NiPoint3 velocity;            // Primary velocity vector
            NiPoint3 linearVelocity;      // Linear velocity component
            float range = 0.0f;           // Range limit (copied from form)
            float livingTime = 0.0f;      // Time alive in seconds
            float distanceMoved = 0.0f;   // Actual distance traveled (this is what gets checked!)
            FlagsSet flags;               // Runtime flags
        };

        ProjectileRuntimeData runtimeData;

        NiNode* Get3D() { return node; }
        void Set3D(NiNode* n) { node = n; }

        // Accessors matching CommonLibSSE patterns
        NiPoint3& GetPosition() { return data.location; }
        NiPoint3& GetVelocity() { return runtimeData.linearVelocity; }
        NiPoint3& GetRotation() { return data.angle; }

        ProjectileRuntimeData& GetProjectileRuntimeData() { return runtimeData; }

        ObjectRefHandle GetHandle() const { return handle; }
        void SetHandle(ObjectRefHandle h) { handle = h; }

    private:
        NiNode* node = nullptr;
        ObjectRefHandle handle;
    };

    // === TESObjectREFR Stub ===
    class TESObjectREFR : public TESForm {
    public:
        NiPoint3 GetPosition() const { return position; }
        void SetPosition(const NiPoint3& pos) { position = pos; }

        NiNode* Get3D() { return node; }
        void Set3D(NiNode* n) { node = n; }

        ObjectRefHandle GetHandle() const { return handle; }
        void SetHandle(ObjectRefHandle h) { handle = h; }

        // Static lookup by handle - for tests, we use a simple map
        // Matches CommonLib API: NiPointer<TESObjectREFR> LookupByHandle(RefHandle)
        static NiPointer<TESObjectREFR> LookupByHandle(RefHandle h) {
            auto it = s_handleMap.find(h);
            return it != s_handleMap.end() ? NiPointer<TESObjectREFR>(it->second) : NiPointer<TESObjectREFR>();
        }

        // Overload for ObjectRefHandle (convenience for tests)
        static NiPointer<TESObjectREFR> LookupByHandle(const ObjectRefHandle& h) {
            return LookupByHandle(h.value);
        }

        // Test helper to register a ref with a handle
        static void RegisterHandle(RefHandle h, TESObjectREFR* ref) {
            s_handleMap[h] = ref;
        }

        // Overload for ObjectRefHandle (convenience for tests)
        static void RegisterHandle(const ObjectRefHandle& h, TESObjectREFR* ref) {
            RegisterHandle(h.value, ref);
        }

        static void ClearHandles() {
            s_handleMap.clear();
        }

    private:
        NiPoint3 position;
        NiNode* node = nullptr;
        ObjectRefHandle handle;
        static inline std::map<uint32_t, TESObjectREFR*> s_handleMap;
    };

    // === PlayerCharacter Stub ===
    class PlayerCharacter : public TESObjectREFR {
    public:
        static PlayerCharacter* GetSingleton() {
            static PlayerCharacter instance;
            return &instance;
        }

        NiNode* Get3D() { return &dummyNode; }

    private:
        NiNode dummyNode;
    };

    // === TESDataHandler Stub ===
    class TESDataHandler {
    public:
        static TESDataHandler* GetSingleton() {
            static TESDataHandler instance;
            return &instance;
        }

        TESForm* LookupForm(FormID formID, const std::string& pluginName) {
            auto key = pluginName + ":" + std::to_string(formID);
            auto it = forms.find(key);
            return it != forms.end() ? it->second : nullptr;
        }

        // Test helper to register forms
        void RegisterForm(const std::string& pluginName, FormID formID, TESForm* form) {
            auto key = pluginName + ":" + std::to_string(formID);
            forms[key] = form;
        }

        void ClearForms() { forms.clear(); }

    private:
        std::map<std::string, TESForm*> forms;
    };

    // === Papyrus Stubs ===
    namespace BSScript {
        // Forward declarations for Papyrus related stubs
        class IVirtualMachine;
        class IStackCallbackFunctor;
        class Variable;
        class Object; // Forward declare BSScript::Object
        namespace Internal {
            class VirtualMachine;
        }
    }
    using VMStackID = std::uint32_t; // Stub VMStackID to match CommonLibSSE-NG
    struct StaticFunctionTag {}; // Stub StaticFunctionTag as an empty struct
}