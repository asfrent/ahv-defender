#ifndef AHV_DEFENDER_AHV_CACHE_RADIX_H_
#define AHV_DEFENDER_AHV_CACHE_RADIX_H_

#include <string>
#include <cstring>

#include "AHVCache_Base.hpp"
#include "AHVCache_RadixBucket.hpp"


class AHVCache_Radix : public AHVCache_Base {
 public:
  void Add(const std::string& hash, int64_t record_index, bool quick = false) override {
    int32_t prefix, bucket, reduced_index;
    EncodePrefix(hash, &prefix, &bucket);
    EncodeReducedIndex(record_index, &reduced_index);
    buckets[bucket].Add(prefix, reduced_index, quick);
    buckets[bucket].MaybeRebuild(quick);
  }

  void Remove(const std::string& hash, int64_t record_index) override {
    int32_t prefix, bucket, reduced_index;
    EncodePrefix(hash, &prefix, &bucket);
    EncodeReducedIndex(record_index, &reduced_index);
    buckets[bucket].Remove(prefix, reduced_index);
    buckets[bucket].MaybeRebuild();
  }

  void Find(const std::string& hash, int64_t** possible_indexes, int* count) override {
    int32_t prefix, bucket;
    EncodePrefix(hash, &prefix, &bucket);
    buckets[bucket].Find(prefix, possible_indexes, count);
    buckets[bucket].MaybeRebuild();
  }

 private:
  void EncodePrefix(const std::string& hash, int32_t* prefix, int* bucket) {
    *bucket = (int) ((unsigned char) *hash.c_str());
    memcpy((char*) prefix, hash.c_str(), 4);
  }

  void EncodeReducedIndex(int64_t record_index, int32_t* reduced_index) {
    *reduced_index = record_index / 32;
  }

  AHVCache_RadixBucket buckets[256];
};

#endif  // AHV_DEFENDER_AHV_CACHE_RADIX_H_