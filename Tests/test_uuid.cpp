#include <catch2/catch_all.hpp>
#include "../src/util/UUID.h"
#include <unordered_set>
#include <thread>
#include <vector>

using namespace Util;

TEST_CASE("UUID Generation", "[uuid]") {
    SECTION("Generated UUIDs are valid") {
        auto uuid = UUID::Generate();
        REQUIRE(uuid.IsValid());
        REQUIRE(uuid.Value() != 0);
    }

    SECTION("Invalid UUID has zero value") {
        auto invalid = UUID::Invalid();
        REQUIRE_FALSE(invalid.IsValid());
        REQUIRE(invalid.Value() == 0);
    }

    SECTION("Default constructed UUID is invalid") {
        UUID uuid;
        REQUIRE_FALSE(uuid.IsValid());
        REQUIRE(uuid.Value() == 0);
    }

    SECTION("Generated UUIDs are unique") {
        std::unordered_set<uint64_t> seen;
        constexpr int NUM_UUIDS = 1000;

        for (int i = 0; i < NUM_UUIDS; ++i) {
            auto uuid = UUID::Generate();
            REQUIRE(seen.find(uuid.Value()) == seen.end());
            seen.insert(uuid.Value());
        }

        REQUIRE(seen.size() == NUM_UUIDS);
    }
}

TEST_CASE("UUID Comparison", "[uuid]") {
    SECTION("Equal UUIDs compare equal") {
        UUID a(12345);
        UUID b(12345);
        REQUIRE(a == b);
        REQUIRE_FALSE(a != b);
    }

    SECTION("Different UUIDs compare not equal") {
        auto a = UUID::Generate();
        auto b = UUID::Generate();
        REQUIRE(a != b);
        REQUIRE_FALSE(a == b);
    }

    SECTION("Less-than comparison works") {
        UUID a(100);
        UUID b(200);
        REQUIRE(a < b);
        REQUIRE_FALSE(b < a);
    }
}

TEST_CASE("UUID ToString", "[uuid]") {
    SECTION("ToString produces 16-character hex string") {
        auto uuid = UUID::Generate();
        auto str = uuid.ToString();
        REQUIRE(str.length() == 16);
    }

    SECTION("ToString produces consistent output") {
        UUID uuid(0x123456789ABCDEF0);
        REQUIRE(uuid.ToString() == "123456789abcdef0");
    }

    SECTION("Zero UUID produces all zeros") {
        UUID uuid(0);
        REQUIRE(uuid.ToString() == "0000000000000000");
    }
}

TEST_CASE("UUID Hash", "[uuid]") {
    SECTION("Same UUIDs have same hash") {
        UUID a(12345);
        UUID b(12345);
        UUID::Hash hasher;
        REQUIRE(hasher(a) == hasher(b));
    }

    SECTION("UUID can be used in unordered_map") {
        std::unordered_map<UUID, int, UUID::Hash> map;
        auto uuid1 = UUID::Generate();
        auto uuid2 = UUID::Generate();

        map[uuid1] = 1;
        map[uuid2] = 2;

        REQUIRE(map[uuid1] == 1);
        REQUIRE(map[uuid2] == 2);
        REQUIRE(map.size() == 2);
    }
}

TEST_CASE("UUID Thread Safety", "[uuid][threading]") {
    SECTION("Concurrent generation produces unique UUIDs") {
        constexpr int NUM_THREADS = 4;
        constexpr int UUIDS_PER_THREAD = 250;

        std::vector<std::thread> threads;
        std::vector<std::vector<UUID>> results(NUM_THREADS);

        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&results, t]() {
                for (int i = 0; i < UUIDS_PER_THREAD; ++i) {
                    results[t].push_back(UUID::Generate());
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        // Collect all UUIDs and verify uniqueness
        std::unordered_set<uint64_t> allUUIDs;
        for (const auto& threadResults : results) {
            for (const auto& uuid : threadResults) {
                REQUIRE(allUUIDs.find(uuid.Value()) == allUUIDs.end());
                allUUIDs.insert(uuid.Value());
            }
        }

        REQUIRE(allUUIDs.size() == NUM_THREADS * UUIDS_PER_THREAD);
    }
}
