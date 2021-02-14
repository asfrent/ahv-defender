#include <iostream>
#include <memory>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <fstream>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <thread>
#include <functional>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include "ahvdefender.grpc.pb.h"

#include "BCryptHasher.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using ahvdefender::AHVDatabase;
using ahvdefender::AHVLookupRequest;
using ahvdefender::AHVLookupResponse;
using ahvdefender::AHVAddRequest;
using ahvdefender::AHVAddResponse;
using ahvdefender::AHVRemoveRequest;
using ahvdefender::AHVRemoveResponse;

std::mutex shutdown_mutex;

void SigIntHandler(int s){
  std::cout << "Caught SIGINT." << std::endl;
  shutdown_mutex.unlock();
}

void SetUpSigIntHandler() {
  struct sigaction sig_int_handler;
  sig_int_handler.sa_handler = SigIntHandler;
  sigemptyset(&sig_int_handler.sa_mask);
  sig_int_handler.sa_flags = 0;
  sigaction(SIGINT, &sig_int_handler, nullptr);
}

struct DiskRecord {
  void set_used(bool used) {
    data[0] = (unsigned char) (used ? 0x01 : 0x00);
  }

  static const DiskRecord& empty_record() {
    static bool initialized = false;
    static DiskRecord empty;
    if (!initialized) {
      empty.set_used(false);
      memset((unsigned char*) empty.data + 1, 0, 31);
      initialized = true;
    }
    return empty;
  }

  // Record layout:
  //   * byte 0: T if index is used, F if index is free.
  //   * bytes 1-32: bcrypt binary hash.
  unsigned char data[32];
};

class AHVCache_RadixBucket {
 public:
  AHVCache_RadixBucket() {
    delta_add_prefixes = new int32_t[MAX_DELTA_SIZE];
    delta_add_rindexes = new int32_t[MAX_DELTA_SIZE];
    delta_remove_prefixes = new int32_t[MAX_DELTA_SIZE];
    serving_prefixes = new int32_t[MAX_SERVING_SIZE];
    serving_rindexes = new int32_t[MAX_SERVING_SIZE];
    delta_remove_size = 0;
    delta_add_size = 0;
    serving_size = 0;
  }

  void Add(int32_t prefix, int32_t reduced_index) {
    delta_add_prefixes[delta_add_size] = prefix;
    delta_add_rindexes[delta_add_size] = reduced_index;
    ++delta_add_size;
  }

  void Remove(int32_t prefix) {
    delta_remove_prefixes[delta_remove_size] = prefix;
    ++delta_remove_size;
  }

  void Find(int32_t prefix, int64_t** possible_indexes, int* count) {
    std::cout << "[bucket] Find(" << prefix << ", ..., ...)" << std::endl;
    int32_t* start = serving_prefixes;
    std::cout << "[bucket] start = " << start << std::endl;
    int32_t* end = serving_prefixes + serving_size;
    std::cout << "[bucket] end = " << end << std::endl;
    int32_t* ptr = std::lower_bound(start, end, prefix);
    std::cout << "[bucket] ptr = " << ptr << std::endl;
    int32_t* it = ptr;
    while (it != end && *it == prefix) {
      ++it;
    }
    *count = it - ptr;
    std::cout << "[bucket] *count = " << *count << std::endl;
    *possible_indexes = new int64_t[*count];
    it = ptr;
    int idx = ptr - start;
    std::cout << "[bucket] idx = " << idx << std::endl;
    for (int i = 0; i < *count; ++i) {
      (*possible_indexes)[i] = (int64_t) 32 * (int64_t) serving_rindexes[idx + i];
    }
  }

  void Rebuild() {
    // First pass, apply remove.
    for (int i = 0; i < delta_remove_size; ++i) {
      // Binay search for prefix.
      int32_t* ptr = std::lower_bound(serving_prefixes, serving_prefixes + serving_size, delta_remove_prefixes[i]);
      // Skip if not found.
      if (*ptr != delta_remove_prefixes[i]) continue;
      // Where did we find it?
      int index = ptr - serving_prefixes;
      // Replace with last element.
      serving_prefixes[index] = serving_prefixes[serving_size - 1];
      serving_rindexes[index] = serving_rindexes[serving_size - 1];
      // Decrement size.
      --serving_size;
    }
    // Applied all removes, reset remove delta size.
    delta_remove_size = 0;

    // Second pass, apply add.
    for (int i = 0; i < delta_add_size; ++i) {
      // Append at end.
      serving_prefixes[serving_size] = delta_add_prefixes[i];
      serving_rindexes[serving_size] = delta_add_rindexes[i];
      // Increment size.
      ++serving_size;
    }
    // Applied all adds, reset add delta size.
    delta_add_size = 0;

    MergeSort(0, serving_size);
  }

 private:
  void MergeSort(int li, int ls) {
    if (li >= ls) return;
    int mid = li + (ls - li) / 2;
    MergeSort(li, mid);
    MergeSort(mid + 1, ls);
    Merge(li, mid, ls);
  }

  void Merge(int li, int mid, int ls) {
    int size1 = mid - li + 1;
    int32_t* v1_prefixes = new int32_t[size1];
    int32_t* v1_rindexes = new int32_t[size1];
    memcpy(v1_prefixes, serving_prefixes + li, size1 * sizeof(int32_t));
    memcpy(v1_rindexes, serving_rindexes + li, size1 * sizeof(int32_t));

    int size2 = ls - mid;
    int32_t* v2_prefixes = new int32_t[size2];
    int32_t* v2_rindexes = new int32_t[size2];
    memcpy(v2_prefixes, serving_prefixes + mid + 1, size2 * sizeof(int32_t));
    memcpy(v2_rindexes, serving_rindexes + mid + 1, size2 * sizeof(int32_t));

    int it1 = 0, it2 = 0, it = li;

    while (it1 < size1 && it2 < size2) {
      if (v1_prefixes[it1] <= v2_prefixes[it2]) {
        serving_prefixes[it] = v1_prefixes[it1];
        serving_rindexes[it] = v1_rindexes[it1];
        ++it1;
      } else {
        serving_prefixes[it] = v2_prefixes[it2];
        serving_prefixes[it] = v2_prefixes[it2];
        ++it2;
      }
      ++it;
    }

    while (it1 < size1) {
      serving_prefixes[it] = v1_prefixes[it1];
      serving_rindexes[it] = v1_rindexes[it1];
      ++it1;
      ++it;
    }

    while (it2 < size2) {
      serving_prefixes[it] = v1_prefixes[it2];
      serving_rindexes[it] = v1_rindexes[it2];
      ++it2;
      ++it;
    }

    delete[] v1_prefixes;
    delete[] v1_rindexes;
    delete[] v2_prefixes;
    delete[] v2_rindexes;
  }

  const int MAX_DELTA_SIZE = 4096; // total 1M
  const int MAX_SERVING_SIZE = 4194304; // total 1G

  int32_t* delta_add_prefixes;
  int32_t* delta_add_rindexes;
  int32_t* delta_remove_prefixes;
  int32_t* serving_prefixes;
  int32_t* serving_rindexes;

  int32_t delta_add_size, delta_remove_size, serving_size;
};

class AHVCache_Base {
 public:
  virtual void Add(const std::string& hash, int64_t record_index) = 0;
  virtual void Remove(const std::string& hash) = 0;
  virtual void Find(const std::string& hash, int64_t** possible_indexes, int* count) = 0;
};

class AHVCache_Radix : public AHVCache_Base {
 public:
  void Add(const std::string& hash, int64_t record_index) override {
    int32_t prefix, bucket, reduced_index;
    EncodePrefix(hash, &prefix, &bucket);
    EncodeReducedIndex(record_index, &reduced_index);
    buckets[bucket].Add(prefix, reduced_index);
  }

  void Remove(const std::string& hash) override {
    int32_t prefix, bucket;
    EncodePrefix(hash, &prefix, &bucket);
    buckets[bucket].Remove(prefix);
  }

  void Find(const std::string& hash, int64_t** possible_indexes, int* count) override {
    int32_t prefix, bucket;
    EncodePrefix(hash, &prefix, &bucket);
    buckets[bucket].Rebuild();
    buckets[bucket].Find(prefix, possible_indexes, count);
  }

 private:
  void EncodePrefix(const std::string& hash, int32_t* prefix, int* bucket) {
    *bucket = (int) (*hash.c_str());
    memcpy((char*) prefix, hash.c_str(), 4);
  }

  void EncodeReducedIndex(int64_t record_index, int32_t* reduced_index) {
    *reduced_index = record_index / 32;
  }

  AHVCache_RadixBucket buckets[256];
};

class AHVCache_HashMap : public AHVCache_Base {
 public:
  void Remove(const std::string& hash) override {
    m_.erase(hash);
  }

  void Add(const std::string& hash, int64_t record_index) override {
    m_[hash] = record_index;
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

class AHVStore_File {
 public:
  AHVStore_File(const std::string& filename) {
    if (!FileExists(filename)) {
      CreateEmptyFile(filename);
    }
    fs_.open(filename, std::ios::binary | std::ios::in | std::ios::out);
  }

  ~AHVStore_File() {
    fs_.close();
    std::cout << "File store cleanly destructed." << std::endl;
  }

  void ForEach(std::function<void(const char*, int64_t)> tell_record,
               std::function<void(int64_t)> tell_free) {
    fs_mutex_.lock();
    fs_.seekg(0, std::ios::end);
    int64_t remaining = fs_.tellg();
    fs_.seekg(0, std::ios::beg);
    char buffer[4096];
    int record_index = 0;
    while(remaining > 0) {
      int count = remaining < 4096 ? (int) remaining : 4096;
      fs_.read(buffer, count);
      int buffer_index = 0;
      while (buffer_index < count) {
        if (*(buffer + buffer_index) == 0x01) {
          tell_record(buffer + buffer_index, record_index);
        } else {
          tell_free(record_index);
        }
        record_index += 32;
        buffer_index += 32;
      }
      remaining -= count;
    }
    fs_mutex_.unlock();
  }

  int64_t Add(const std::string& hash) {
    // Prepare disk record.
    DiskRecord disk_record;
    disk_record.set_used(true);
    memcpy(disk_record.data + 1, hash.c_str(), 31);

    // Append.
    fs_mutex_.lock();
    fs_.seekp(0, std::ios::end);
    int64_t record_index = fs_.tellp();
    fs_.write((const char*) disk_record.data, 32);
    fs_mutex_.unlock();
    return record_index;
  }

  void Remove(int64_t record_index) {
    fs_mutex_.lock();
    fs_.seekp(record_index);
    fs_.write((const char*) DiskRecord::empty_record().data, 32);
    fs_mutex_.unlock();
  }

  bool HashAtEquals(int64_t record_index, const std::string& hash) {
    char buffer[31];
    fs_mutex_.lock();
    fs_.seekg(record_index + 1);
    fs_.read(buffer, 31);
    fs_mutex_.unlock();
    return strncmp(hash.c_str(), buffer, 31) == 0;
  }

 private:
  static bool FileExists(const std::string& filename) {
    struct stat buffer;
    return stat(filename.c_str(), &buffer) == 0;
  }

  static void CreateEmptyFile(const std::string& filename) {
    std::ofstream output(filename);
    output.close();
  }

  std::fstream fs_;
  std::mutex fs_mutex_;
};

class AHVDiskDatabase {
 public:
  AHVDiskDatabase(const std::string& filename)
      : store_(filename) { }

  void Init() {
    int64_t total_hashes = 0, total_free = 0;
    store_.ForEach(
        [&] (const char* data, int64_t index) -> void {
          cache_.Add(std::string(data + 1, 31), index);
          ++total_hashes;
        },
        [&] (int64_t index) -> void {
          ++total_free;
        });
    std::cout << "Loaded " << total_hashes << " hashes." << std::endl;
    std::cout << "There's " << total_free << " free records in the DB." << std::endl;
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
        cache_.Remove(hash);
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

class AHVDatabaseServiceImpl final : public AHVDatabase::Service {
 public:
  AHVDatabaseServiceImpl(std::unique_ptr<AHVDiskDatabase> ahv_disk_database)
      : ahv_disk_database_(std::move(ahv_disk_database)) {
  }

  Status Lookup(ServerContext* context, const AHVLookupRequest* request, AHVLookupResponse* response) override {
    cout_mutex.lock();
    std::cout << "Lookup " << request->ahv() << std::endl;
    cout_mutex.unlock();
    response->set_found(ahv_disk_database_->Lookup(request->ahv()));
    return Status::OK;
  }

  Status Add(ServerContext* context, const AHVAddRequest* request, AHVAddResponse* response) override {
    cout_mutex.lock();
    std::cout << "Add " << request->ahv() << std::endl;
    cout_mutex.unlock();
    response->set_added(ahv_disk_database_->Add(request->ahv()));
    return Status::OK;
  }

  Status Remove(ServerContext* context, const AHVRemoveRequest* request, AHVRemoveResponse* response) {
    cout_mutex.lock();
    std::cout << "Remove " << request->ahv() << std::endl;
    cout_mutex.unlock();
    response->set_removed(ahv_disk_database_->Remove(request->ahv()));
    return Status::OK;
  }

 private:
  std::mutex cout_mutex;
  std::unique_ptr<AHVDiskDatabase> ahv_disk_database_;
};

void RunServer() {
  std::unique_ptr<AHVDiskDatabase> ahv_disk_database =
      std::make_unique<AHVDiskDatabase>("hashes");
  ahv_disk_database->Init();
  std::string server_address("0.0.0.0:12000");
  AHVDatabaseServiceImpl service(std::move(ahv_disk_database));
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  std::thread t([&] () -> void { server->Wait(); });
  shutdown_mutex.lock();
  shutdown_mutex.lock();
  server->Shutdown();
  t.join();
}

int main(int argc, char** argv) {
  SetUpSigIntHandler();
  RunServer();
  std::cout << "Bye." << std::endl;
  return 0;
}
