#ifndef AHV_DEFENDER_AHV_CACHE_HASH_MAP_H_
#define AHV_DEFENDER_AHV_CACHE_HASH_MAP_H_


#include <string>
#include <unordered_map>

#include "AHVCache_Base.hpp"

class AHVCache_HashMap : public AHVCache_Base {
 public:
  void Add(const std::string& hash, int64_t record_index, bool quick = false) override {
    m_[hash] = record_index;
  }

  void Remove(const std::string& hash, int64_t record_index) override {
    if (m_[hash] != record_index) {
      exit(1);
    }
    m_.erase(hash);
  }

  void Find(const std::string& hash, int64_t** possible_indexes, int* count) override {
    auto it = m_.find(hash);
    if (it == m_.end()) {
      *count = 0;
      *possible_indexes = new int64_t[0];
    } else {
      *count = 1;
      *possible_indexes = new int64_t[1];
      (*possible_indexes)[0] = it->second;
    }
  }

 private:
  std::unordered_map<std::string, int64_t> m_;
};

  #endif  // AHV_DEFENDER_AHV_CACHE_HASH_MAP_H_