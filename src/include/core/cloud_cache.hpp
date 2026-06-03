#pragma once
#include "cloud_item.hpp"

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// CloudCache
// Provider-agnostic LRU-TTL cache shared by all backends.
// Keys are namespaced by scheme so multiple providers can coexist.
// ─────────────────────────────────────────────────────────────────────────────
class CloudCache {
  public:
    // Tunable TTLs (seconds)
    static constexpr int kItemTtl = 900;         // 15 min: path → CloudItem
    static constexpr int kDownloadUrlTtl = 3000; // 50 min: pre-auth URL
    static constexpr int kRootTtl = 86400;       // 24 h:  root → internal ID
    static constexpr size_t kMaxItems = 512;

    // ── Item (path → CloudItem) ───────────────────────────────────────────────
    bool GetItem(const std::string& scheme, const std::string& full_path, CloudItem& out);
    void PutItem(const std::string& scheme, const std::string& full_path, const CloudItem& item);
    void InvalidateItem(const std::string& scheme, const std::string& full_path);
    void InvalidatePrefix(const std::string& scheme, const std::string& prefix);

    // ── Pre-authenticated download URL ────────────────────────────────────────
    bool GetDownloadUrl(const std::string& scheme, const std::string& item_id, std::string& out);
    void PutDownloadUrl(const std::string& scheme, const std::string& item_id,
                        const std::string& url, int ttl = kDownloadUrlTtl);

    // ── Root ID (bucket/site → drive_id / team_drive_id) ──────────────────────
    bool GetRootId(const std::string& scheme, const std::string& root_key, std::string& out);
    void PutRootId(const std::string& scheme, const std::string& root_key, const std::string& id);

    // ── Bulk operations ───────────────────────────────────────────────────────
    void ClearScheme(const std::string& scheme); // evict everything for one provider
    void ClearAll();

    // Stats for diagnostics
    struct Stats {
        size_t item_entries;
        size_t url_entries;
        size_t root_entries;
    };
    Stats GetStats() const;

  private:
    template <typename V> struct Entry {
        V value;
        std::chrono::system_clock::time_point expires_at;
    };

    std::string MakeKey(const std::string& scheme, const std::string& sub) const {
        return scheme + ":" + sub;
    }

    template <typename Map> void EvictExpired(Map& map);

    template <typename Map> void EvictOldest(Map& map, size_t target_size);

    std::unordered_map<std::string, Entry<CloudItem>> items_;
    std::unordered_map<std::string, Entry<std::string>> urls_;
    std::unordered_map<std::string, Entry<std::string>> roots_;
    mutable std::mutex mu_;
};

} // namespace duckdb
