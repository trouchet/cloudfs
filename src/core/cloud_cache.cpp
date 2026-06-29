#include "core/cloud_cache.hpp"

namespace duckdb {

namespace {
template <typename Map> void StripPrefix(Map& map, const std::string& pfx) {
    for (auto it = map.begin(); it != map.end();)
        if (it->first.substr(0, pfx.size()) == pfx)
            it = map.erase(it);
        else
            ++it;
}
} // namespace

using Clock = std::chrono::system_clock;

// ── Item cache ────────────────────────────────────────────────────────────────
bool CloudCache::GetItem(const std::string& scheme, const std::string& path, CloudItem& out) {
    std::lock_guard<std::mutex> lk(mu_);
    auto k = MakeKey(scheme, path);
    auto it = items_.find(k);
    if (it == items_.end())
        return false;
    if (Clock::now() > it->second.expires_at) {
        items_.erase(it);
        return false;
    }
    out = it->second.value;
    return true;
}

void CloudCache::PutItem(const std::string& scheme, const std::string& path,
                         const CloudItem& item) {
    std::lock_guard<std::mutex> lk(mu_);
    if (items_.size() >= kMaxItems)
        EvictOldest(items_, kMaxItems * 3 / 4);
    items_[MakeKey(scheme, path)] = {item, Clock::now() + std::chrono::seconds(kItemTtl)};
}

void CloudCache::InvalidateItem(const std::string& scheme, const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    items_.erase(MakeKey(scheme, path));
}

void CloudCache::InvalidatePrefix(const std::string& scheme, const std::string& prefix) {
    std::lock_guard<std::mutex> lk(mu_);
    std::string pfx = MakeKey(scheme, prefix);
    for (auto it = items_.begin(); it != items_.end();) {
        if (it->first.substr(0, pfx.size()) == pfx)
            it = items_.erase(it);
        else
            ++it;
    }
}

// ── Download URL cache ────────────────────────────────────────────────────────
bool CloudCache::GetDownloadUrl(const std::string& scheme, const std::string& item_id,
                                std::string& out) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = urls_.find(MakeKey(scheme, item_id));
    if (it == urls_.end())
        return false;
    if (Clock::now() > it->second.expires_at) {
        urls_.erase(it);
        return false;
    }
    out = it->second.value;
    return true;
}

void CloudCache::PutDownloadUrl(const std::string& scheme, const std::string& item_id,
                                const std::string& url, int ttl) {
    std::lock_guard<std::mutex> lk(mu_);
    if (urls_.size() >= kMaxItems * 2)
        EvictOldest(urls_, kMaxItems);
    urls_[MakeKey(scheme, item_id)] = {url, Clock::now() + std::chrono::seconds(ttl)};
}

// ── Root ID cache ─────────────────────────────────────────────────────────────
bool CloudCache::GetRootId(const std::string& scheme, const std::string& key, std::string& out) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = roots_.find(MakeKey(scheme, key));
    if (it == roots_.end())
        return false;
    if (Clock::now() > it->second.expires_at) {
        roots_.erase(it);
        return false;
    }
    out = it->second.value;
    return true;
}

void CloudCache::PutRootId(const std::string& scheme, const std::string& key,
                           const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    roots_[MakeKey(scheme, key)] = {id, Clock::now() + std::chrono::seconds(kRootTtl)};
}

// ── Bulk ops ──────────────────────────────────────────────────────────────────
void CloudCache::ClearScheme(const std::string& scheme) {
    std::lock_guard<std::mutex> lk(mu_);
    std::string pfx = scheme + ":";
    StripPrefix(items_, pfx);
    StripPrefix(urls_, pfx);
    StripPrefix(roots_, pfx);
}

void CloudCache::ClearAll() {
    std::lock_guard<std::mutex> lk(mu_);
    items_.clear();
    urls_.clear();
    roots_.clear();
}

CloudCache::Stats CloudCache::GetStats() const {
    std::lock_guard<std::mutex> lk(mu_);
    return {items_.size(), urls_.size(), roots_.size()};
}

// ── Eviction helpers ──────────────────────────────────────────────────────────
template <typename Map> void CloudCache::EvictExpired(Map& map) {
    auto now = Clock::now();
    for (auto it = map.begin(); it != map.end();)
        if (now > it->second.expires_at)
            it = map.erase(it);
        else
            ++it;
}

template <typename Map> void CloudCache::EvictOldest(Map& map, size_t target) {
    EvictExpired(map);
    // Note: O(n) eviction is acceptable for small caches (kMaxItems=512).
    // For larger caches, consider a priority queue or LRU list.
    while (map.size() > target) {
        auto oldest = map.begin();
        for (auto it = map.begin(); it != map.end(); ++it)
            if (it->second.expires_at < oldest->second.expires_at)
                oldest = it;
        map.erase(oldest);
    }
}

} // namespace duckdb
