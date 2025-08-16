#pragma once

#include <vector>

/** A sorted flat map based on std::vector<> field naming is kept consistent with STL containers.
 *  This container provides no guarantee about key and value having stable addresses.
 */
template<typename Key, typename Value>
class sorted_flat_map {
public:
  using key_type = Key;
  using value_type = Value;
  using entry = std::pair<key_type, value_type>;
  using container = std::vector<entry>;
  using iterator = typename container::iterator;
  using const_iterator = typename container::const_iterator;
  using size_type = typename container::size_type;

  iterator find(const key_type& key) {
    iterator pos = std::lower_bound(map.begin(), map.end(), key, [](const entry& entry, const key_type& key) { return entry.first < key; });
    if (pos != map.end() && pos->first == key) {
      return pos;
    } else {
      return map.end();
    }
  }

  iterator begin() {
    return map.begin();
  }

  iterator end() {
    return map.end();
  }

  const_iterator begin() const {
    return map.begin();
  }

  const_iterator end() const {
    return map.end();
  }

  iterator erase(iterator pos) {
    return map.erase(pos);
  }

  size_type erase(const key_type& key) {
    auto pos = find(key);
    if (pos != map.end()) {
      map.erase(pos);
      return 1;
    }
    return 0;
  }

  size_type size() const { 
    return map.size(); 
  }

  value_type& operator[](const key_type& key) {
    // Find entry or insert position
    iterator pos = std::lower_bound(map.begin(), map.end(), key, [](const entry& entry, const key_type& key) { return entry.first < key; });
    if (pos != map.end() && pos->first == key) {
      return pos->second;
    }

    // Perform sorted insertion
    return map.insert(pos, entry{ key, value_type{} })->second;
  }
  

private:
  container map;
};

