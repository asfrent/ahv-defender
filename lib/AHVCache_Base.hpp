#ifndef AHV_DEFENDER_AHV_CACHE_BASE_H_
#define AHV_DEFENDER_AHV_CACHE_BASE_H_

#include <string>

class AHVCache_Base {
 public:
  virtual void Add(const std::string& hash, int64_t record_index, bool quick = false) = 0;
  virtual void Remove(const std::string& hash, int64_t record_index) = 0;
  virtual void Find(const std::string& hash, int64_t** possible_indexes, int* count) = 0;
};

#endif  // AHV_DEFENDER_AHV_CACHE_BASE_H_