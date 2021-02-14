#ifndef AHV_DEFENDER_AHV_DISK_DATABASE_H_
#define AHV_DEFENDER_AHV_DISK_DATABASE_H_

#include <chrono>
#include <iostream>
#include <string>

#include "AHVCache_Radix.hpp"
#include "AHVStore_File.hpp"
#include "BCryptHasher.hpp"

class AHVDiskDatabase {
 public:
  AHVDiskDatabase(const std::string& filename)
      : store_(filename) { }

  void Init() {
    int64_t total_hashes = 0, total_free = 0;
    auto start = std::chrono::high_resolution_clock::now();
    store_.ForEach(
        [&] (const char* data, int64_t index) -> void {
          cache_.Add(std::string(data + 1, 31), index, true);
          ++total_hashes;
        },
        [&] (int64_t index) -> void {
          ++total_free;
        });
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);

    std::cout << "Loaded " << total_hashes << " hashes." << std::endl;
    std::cout << "There's " << total_free << " free records in the DB." << std::endl;
    std::cout << "Took " << duration.count() << " seconds." << std::endl;
  }

  bool Add(const std::string& ahv) {
    std::string hash = hasher_.ComputeHash(ahv);
    int64_t* possible_record_indexes;
    int count;
    cache_.Find(hash, &possible_record_indexes, &count);
    bool found = false;
    for (int i = 0; i < count; ++i) {
      if (store_.HashAtEquals(possible_record_indexes[i], hash)) {
        found = true;
        break;
      }
    }
    delete[] possible_record_indexes;
    if (found) return false;
    int64_t record_index = store_.Add(hash);
    cache_.Add(hash, record_index);
    return true;
  }

  bool Remove(const std::string& ahv) {
    std::string hash = hasher_.ComputeHash(ahv);
    int64_t* possible_record_indexes;
    int count;
    cache_.Find(hash, &possible_record_indexes, &count);
    bool found = false;
    for (int i = 0; i < count; ++i) {
      if (store_.HashAtEquals(possible_record_indexes[i], hash)) {
        found = true;
        store_.Remove(possible_record_indexes[i]);
        cache_.Remove(hash, possible_record_indexes[i]);
        break;
      }
    }
    delete[] possible_record_indexes;
    return found;
  }

  bool Lookup(const std::string& ahv) {
    std::string hash = hasher_.ComputeHash(ahv);
    int64_t* possible_record_indexes;
    int count;
    cache_.Find(hash, &possible_record_indexes, &count);
    bool found = false;
    for (int i = 0; i < count; ++i) {
      if (store_.HashAtEquals(possible_record_indexes[i], hash)) {
        found = true;
        break;
      }
    }
    delete[] possible_record_indexes;
    return found;
  }

 private:
  BCryptHasher hasher_;
  AHVCache_Radix cache_;
  AHVStore_File store_;
};

#endif  // AHV_DEFENDER_AHV_DISK_DATABASE_H_