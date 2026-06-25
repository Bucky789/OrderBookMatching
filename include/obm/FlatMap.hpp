#pragma once

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace obm {

// Cache-friendly sorted map backed by std::vector<pair<K,V>>.
// O(log n) find via binary search; O(n) insert/erase due to shift — faster
// than std::map (red-black tree) at typical book depths (< 200 levels)
// because all data lives in one contiguous allocation.
//
// V must be move-constructible (elements shift on insert/erase).
// Copy constructor is deleted — use move only.
template<typename K, typename V, typename Compare = std::less<K>>
class FlatMap {
public:
    using value_type             = std::pair<K, V>;
    using iterator               = typename std::vector<value_type>::iterator;
    using const_iterator         = typename std::vector<value_type>::const_iterator;
    using reverse_iterator       = typename std::vector<value_type>::reverse_iterator;
    using const_reverse_iterator = typename std::vector<value_type>::const_reverse_iterator;

    FlatMap() = default;

    FlatMap(FlatMap&&) noexcept            = default;
    FlatMap& operator=(FlatMap&&) noexcept = default;
    FlatMap(const FlatMap&)                = delete;
    FlatMap& operator=(const FlatMap&)     = delete;

    iterator       begin()  noexcept { return data_.begin(); }
    iterator       end()    noexcept { return data_.end();   }
    const_iterator begin()  const noexcept { return data_.begin(); }
    const_iterator end()    const noexcept { return data_.end();   }
    reverse_iterator       rbegin() noexcept { return data_.rbegin(); }
    reverse_iterator       rend()   noexcept { return data_.rend();   }
    const_reverse_iterator rbegin() const noexcept { return data_.rbegin(); }
    const_reverse_iterator rend()   const noexcept { return data_.rend();   }

    [[nodiscard]] bool        empty() const noexcept { return data_.empty(); }
    [[nodiscard]] std::size_t size()  const noexcept { return data_.size();  }

    iterator find(const K& key) {
        auto it = lb(key);
        if (it != data_.end() && eq(it->first, key)) return it;
        return data_.end();
    }

    const_iterator find(const K& key) const {
        auto it = lb(key);
        if (it != data_.end() && eq(it->first, key)) return it;
        return data_.end();
    }

    // Insert default-constructed V at sorted position if key absent; return ref.
    V& operator[](const K& key) {
        auto it = lb(key);
        if (it != data_.end() && eq(it->first, key)) return it->second;
        it = data_.emplace(it, key, V{});
        return it->second;
    }

    iterator erase(iterator it) { return data_.erase(it); }

    void clear() noexcept { data_.clear(); }

private:
    std::vector<value_type> data_;
    Compare                 cmp_{};

    iterator lb(const K& key) {
        Compare c = cmp_;
        return std::lower_bound(data_.begin(), data_.end(), key,
            [&c](const value_type& e, const K& k) { return c(e.first, k); });
    }
    const_iterator lb(const K& key) const {
        Compare c = cmp_;
        return std::lower_bound(data_.begin(), data_.end(), key,
            [&c](const value_type& e, const K& k) { return c(e.first, k); });
    }
    bool eq(const K& a, const K& b) const {
        return !cmp_(a, b) && !cmp_(b, a);
    }
};

} // namespace obm
