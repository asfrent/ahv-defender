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

#define BCRYPT_INPUT_LEN 16
#define BCRYPT_HASH_LEN 64
#define BCRYPT_FACTOR 8

extern "C" {
char *crypt_gensalt_rn(__const char *prefix, unsigned long count, __const char *input, int size, char *output, int output_size);
char *crypt_rn(__const char *key, __const char *setting, void *data, int size);
}

class BCryptHasher {
 public:
  BCryptHasher() {
    memset(setting, 0, BCRYPT_HASH_LEN);
    crypt_gensalt_rn("$2a$", BCRYPT_FACTOR, (char*) input, BCRYPT_INPUT_LEN, setting, BCRYPT_HASH_LEN);
    std::cout << "Initialized bcrypt setting: " << setting << std::endl;
  }

  std::string ComputeHash(const std::string& plaintext) {
    char hash[BCRYPT_HASH_LEN] = {0};
    crypt_rn(plaintext.c_str(), setting, hash, BCRYPT_HASH_LEN);
    std::string result(hash);
    result = result.substr(result.size() - 31);
    std::cout << plaintext << " --> " << result << std::endl;
    return result;
  }

 private:
  char setting[BCRYPT_HASH_LEN];
  static const unsigned char input[BCRYPT_INPUT_LEN];
};

const unsigned char BCryptHasher::input[16] = {
  0xfb, 0xf5, 0x7f, 0xb4, 0x30, 0xe7, 0x07, 0x10,
  0xa3, 0x1a, 0x8a, 0x03, 0x06, 0x26, 0xd0, 0xcb,
};

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

class AHVCache_Radix {
  // TODO
};

class AHVCache_HashMap {
 public:
  void Remove(const std::string& hash) {
    m_.erase(hash);
  }

  void Add(const std::string& hash, int64_t record_index) {
    m_[hash] = record_index;
  }

  int64_t Find(const std::string& hash) {
    auto it = m_.find(hash);
    if (it == m_.end()) {
      return -1;
    }
    return it->second;
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
  }

  int64_t Add(const std::string& hash) {
    // Prepare disk record.
    DiskRecord disk_record;
    disk_record.set_used(true);
    memcpy(disk_record.data + 1, hash.c_str(), 31);

    // Append.
    fs_.seekp(0, std::ios::end);
    int64_t record_index = fs_.tellp();
    fs_.write((const char*) disk_record.data, 32);
    return record_index;
  }

  void Remove(int64_t record_index) {
    fs_.seekp(record_index);
    fs_.write((const char*) DiskRecord::empty_record().data, 32);
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
};

class AHVDiskDatabase {
 public:
  AHVDiskDatabase(const std::string& filename)
      : store_(filename) { }

  void Init() {
    store_.ForEach(
        [&] (const char* data, int64_t index) -> void {
          std::cout << "[" << index << "] Loaded hash." << std::endl;
          cache_.Add(std::string(data + 1, 31), index);
        },
        [&] (int64_t index) -> void {
          std::cout << "[" << index << "] Free space." << std::endl;
        });
  }

  bool Add(const std::string& ahv) {
    std::string hash = hasher_.ComputeHash(ahv);
    if (cache_.Find(hash) >= 0) return false;
    int64_t record_index = store_.Add(hash);
    cache_.Add(hash, record_index);
    return true;
  }

  bool Remove(const std::string& ahv) {
    std::string hash = hasher_.ComputeHash(ahv);
    int64_t record_index = cache_.Find(hash);
    if (record_index == -1) return false;
    store_.Remove(record_index);
    cache_.Remove(hash);
    return true;
  }

  bool Lookup(const std::string& ahv) {
    return cache_.Find(hasher_.ComputeHash(ahv)) >= 0;
  }

 private:
  BCryptHasher hasher_;
  AHVCache_HashMap cache_;
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
