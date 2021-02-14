#ifndef AHV_DEFENDER_AHV_CACHE_RADIX_BUCKET_H_
#define AHV_DEFENDER_AHV_CACHE_RADIX_BUCKET_H_

#include <string>
#include <chrono>
#include <set>


class AHVCache_RadixBucket {
 public:
  AHVCache_RadixBucket() {
    serving_prefixes = new int32_t[MAX_SERVING_SIZE];
    serving_rindexes = new int32_t[MAX_SERVING_SIZE];
    serving_size = 0;
    last_rebuild_time = std::chrono::high_resolution_clock::now();
  }

  ~AHVCache_RadixBucket() {
    delete[] serving_prefixes;
    delete[] serving_rindexes;
  }

  int DeltaCount(const std::set<std::pair<int32_t, int32_t>> delta, int32_t prefix) {
    auto lb = std::lower_bound(delta.begin(), delta.end(), std::make_pair(prefix, 0));
    int c = 0;
    for (auto it = lb; it != delta.end() && it->first == prefix; ++it) {
      ++c;
    }
    return c;
  }

  void DeltaFind(int32_t prefix, int64_t** possible_indexes, int* count) {
    *count = DeltaCount(delta_add, prefix);
    *possible_indexes = new int64_t[*count];
    *count = 0;
    auto lb = std::lower_bound(delta_add.begin(), delta_add.end(), std::make_pair(prefix, 0));
    for (auto it = lb; it != delta_add.end() && it->first == prefix; ++it) {
      (*possible_indexes)[*count] = (int64_t) 32 * (int64_t) it->second;
      ++(*count);
    }
  }

  void CombineResults(int64_t* v1, int c1, int64_t* v2, int c2, int64_t** r, int* c) {
    *r = new int64_t[c1 + c2];
    *c = 0;
    for (int i = 0; i < c1; ++i) {
      (*r)[(*c)++] = v1[i];
    }
    for (int i = 0; i < c2; ++i) {
      (*r)[(*c)++] = v2[i];
    }
    delete[] v1;
    delete[] v2;
  }

  void Add(int32_t prefix, int32_t reduced_index, bool quick) {
    auto p = std::make_pair(prefix, reduced_index);
    if (!quick && delta_remove.count(p) > 0) {
      delta_remove.erase(p);
    }
    delta_add.insert(p);
  }

  void Remove(int32_t prefix, int32_t reduced_index) {
    auto p = std::make_pair(prefix, reduced_index);
    if (delta_add.count(p) > 0) {
      delta_add.erase(p);
    }
    delta_remove.insert(p);
  }

  void ServingFind(int32_t prefix, int64_t** possible_indexes, int* count) {
    int32_t* start = serving_prefixes;
    int32_t* end = serving_prefixes + serving_size;
    int32_t* ptr = std::lower_bound(start, end, prefix);
    int32_t* it = ptr;
    while (it != end && *it == prefix) {
      ++it;
    }
    int max_count = it - ptr;
    *possible_indexes = new int64_t[max_count];
    it = ptr;
    int idx = ptr - start;
    *count = 0;
    for (int i = 0; i < max_count; ++i) {
      if (delta_remove.count(std::make_pair(prefix, serving_rindexes[idx + i])) > 0) continue;
      (*possible_indexes)[*count] = (int64_t) 32 * (int64_t) serving_rindexes[idx + i];
      ++(*count);
    }
  }

  void Find(int32_t prefix, int64_t** possible_indexes, int* count) {
    int64_t *v1, *v2;
    int c1, c2;
    ServingFind(prefix, &v1, &c1);
    DeltaFind(prefix, &v2, &c2);
    CombineResults(v1, c1, v2, c2, possible_indexes, count);
  }

  void MaybeRebuild(bool quick = false) {
    if (delta_add.size() > MAX_DELTA_SIZE || delta_remove.size() > MAX_DELTA_SIZE) {
      std::cout << "Deltas too large, rebuilding..." << std::endl;
      if (quick) {
        QuickRebuild();
      } else {
        Rebuild();
      }
      return;
    }

    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_rebuild_time);
    if (duration.count() > 10 * 60 && (delta_add.size() > MAX_DELTA_SIZE_WITH_TIME || delta_remove.size() > MAX_DELTA_SIZE_WITH_TIME)) {
      std::cout << "Too much time has passed, deltas not small enough..." << std::endl;
      Rebuild();
      return;
    }
  }

  void Rebuild() {
    auto start_time = std::chrono::high_resolution_clock::now();
    // Accumulate from serving.
    for (int i = 0; i < serving_size; ++i) {
      delta_add.insert(std::make_pair(serving_prefixes[i], serving_rindexes[i]));
    }

    // Apply delta remove.
    for (auto it = delta_remove.begin(); it != delta_remove.end(); ++it) {
      delta_add.erase(*it);
    }

    // Offload to serving.
    serving_size = 0;
    for (auto it = delta_add.begin(); it != delta_add.end(); ++it) {
      serving_prefixes[serving_size] = it->first;
      serving_rindexes[serving_size] = it->second;
      ++serving_size;
    }
    delta_add.clear();
    delta_remove.clear();
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Rebuilt shard. Took " << duration.count() << "ms." << std::endl;
    last_rebuild_time = std::chrono::high_resolution_clock::now();
  }

  void QuickRebuild() {
    auto start_time = std::chrono::high_resolution_clock::now();

    int count = delta_add.size() + serving_size;
    int its = serving_size - 1;
    auto ita = delta_add.rbegin();
    int itns = count - 1;

    while (its >= 0 && ita != delta_add.rend()) {
      if (ita->first >= serving_prefixes[its]) {
        serving_prefixes[itns] = ita->first;
        serving_rindexes[itns] = ita->second;
        ++ita;
      } else {
        serving_prefixes[itns] = serving_prefixes[its];
        serving_rindexes[itns] = serving_rindexes[its];
        --its;
      }
      --itns;
    }
    serving_size = count;
    delta_add.clear();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Rebuilt shard. Took " << duration.count() << "ms." << std::endl;
    last_rebuild_time = std::chrono::high_resolution_clock::now();
  }

 private:
  const int MAX_DELTA_SIZE = 20 * 4096; // total 20M
  const int MAX_DELTA_SIZE_WITH_TIME = 4096; // total 1M
  const int MAX_SERVING_SIZE = 4194304; // total 1G

  int32_t* serving_prefixes;
  int32_t* serving_rindexes;
  int32_t serving_size;

  std::set<std::pair<int32_t, int32_t>> delta_add;
  std::set<std::pair<int32_t, int32_t>> delta_remove;

  std::chrono::time_point<std::chrono::high_resolution_clock> last_rebuild_time;
};

#endif  // AHV_DEFENDER_AHV_CACHE_RADIX_BUCKET_H_