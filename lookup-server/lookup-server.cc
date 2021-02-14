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
    serving_prefixes = new int32_t[MAX_SERVING_SIZE];
    serving_rindexes = new int32_t[MAX_SERVING_SIZE];
    serving_size = 0;
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

  void Add(int32_t prefix, int32_t reduced_index) {
    auto p = std::make_pair(prefix, reduced_index);
    if (delta_add.count(p) > 0) {
      std::cout << "[bucket] tried to add, but I already have it." << std::endl;
      exit(1);
    }
    if (delta_remove.count(p) > 0) {
      delta_remove.erase(p);
    }
    delta_add.insert(p);
  }

  void Remove(int32_t prefix, int32_t reduced_index) {
    auto p = std::make_pair(prefix, reduced_index);
    if (delta_remove.count(p) > 0) {
      std::cout << "[bucket] tried to remove, but I already removed it." << std::endl;
      exit(1);
    }
    if (delta_add.count(p) > 0) {
      delta_add.erase(p);
    }
    delta_remove.insert(p);
  }

  void ServingFind(int32_t prefix, int64_t** possible_indexes, int* count) {
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

  void Find(int32_t prefix, int64_t** possible_indexes, int* count) {
    int64_t *v1, *v2;
    int c1, c2;
    ServingFind(prefix, &v1, &c1);
    std::cout << "[bucket] serving found " << c1 << " possible record indexes." << std::endl;
    DeltaFind(prefix, &v2, &c2);
    std::cout << "[bucket] delta found " << c2 << " possible record indexes." << std::endl;
    CombineResults(v1, c1, v2, c2, possible_indexes, count);
    std::cout << "[bucket] total found " << *count << " possible record indexes." << std::endl;
  }

  void Rebuild() {
    // std::set<std::pair<int32_t, int32_t>> s;
    // for (int i = 0; i < serving_size; ++i) {
    //   s.insert(std::make_pair(serving_prefixes[i], serving_rindexes[i]));
    // }
  }

 private:
  const int MAX_DELTA_SIZE = 4096; // total 1M
  const int MAX_SERVING_SIZE = 4194304; // total 1G

  int32_t* serving_prefixes;
  int32_t* serving_rindexes;
  int32_t serving_size;

  std::set<std::pair<int32_t, int32_t>> delta_add;
  std::set<std::pair<int32_t, int32_t>> delta_remove;
};

class AHVCache_Base {
 public:
  virtual void Add(const std::string& hash, int64_t record_index) = 0;
  virtual void Remove(const std::string& hash, int64_t record_index) = 0;
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

  void Remove(const std::string& hash, int64_t record_index) override {
    int32_t prefix, bucket, reduced_index;
    EncodePrefix(hash, &prefix, &bucket);
    EncodeReducedIndex(record_index, &reduced_index);
    buckets[bucket].Remove(prefix, reduced_index);
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
  void Add(const std::string& hash, int64_t record_index) override {
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
