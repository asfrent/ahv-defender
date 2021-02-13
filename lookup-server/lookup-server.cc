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
    return std::string(hash);
  }

 private:
  char setting[BCRYPT_HASH_LEN];
  static const unsigned char input[BCRYPT_INPUT_LEN];
};

const unsigned char BCryptHasher::input[16] = {
  0xfb, 0xf5, 0x7f, 0xb4, 0x30, 0xe7, 0x07, 0x10,
  0xa3, 0x1a, 0x8a, 0x03, 0x06, 0x26, 0xd0, 0xcb,
};

class BinaryHash {
 public:
  BinaryHash(const std::string& bcrypt_hash) {
    memset(data_, 0, 24);
    // TODO convert bcrypt_hash to binary and store in data_.
  }

 private:
  unsigned char data_[24];
};

class DiskRecord {
 public:
  void set_used(bool used) {
    data_[0] = (unsigned char) (used ? 0x01 : 0x00);
  }

  void set_data(const char* data[23]) {
    memcpy(data_, data, 23);
  }

 private:
  // Record layout:
  //   * byte 0: 0x01 if index is used, 0x00 if index is free.
  //   * bytes 1-23: bcrypt binary hash.
  unsigned char data_[24];
};

class AHVDiskDatabase {
 public:
  AHVDiskDatabase(const std::string& filename) {
    if (!FileExists(filename)) {
      CreateEmptyFile(filename);
    }
    fs_.open(filename, std::ios::binary | std::ios::in | std::ios::out);
  }

  ~AHVDiskDatabase() {
    fs_.close();
    std::cout << "Database object cleanly destructed." << std::endl;
  }

  void Add(const std::string& ahv) {
    BinaryHash binary_hash(hasher_.ComputeHash(ahv));
    // TODO
  }

  void Remove(const std::string& ahv) {
    BinaryHash binary_hash(hasher_.ComputeHash(ahv));
    // TODO
  }

  bool Lookup(const std::string& ahv) {
    BinaryHash binary_hash(hasher_.ComputeHash(ahv));
    // TODO
    return true;
  }

  static bool FileExists(const std::string& filename) {
    struct stat buffer;
    return stat(filename.c_str(), &buffer) == 0;
  }

  static void CreateEmptyFile(const std::string& filename) {
    std::ofstream output(filename);
    output.close();
  }

 private:
  BCryptHasher hasher_;
  std::fstream fs_;
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
    ahv_disk_database_->Add(request->ahv());
    return Status::OK;
  }

  Status Remove(ServerContext* context, const AHVRemoveRequest* request, AHVRemoveResponse* response) {
    cout_mutex.lock();
    std::cout << "Remove " << request->ahv() << std::endl;
    cout_mutex.unlock();
    ahv_disk_database_->Remove(request->ahv());
    return Status::OK;
  }

 private:
  std::mutex cout_mutex;
  std::unique_ptr<AHVDiskDatabase> ahv_disk_database_;
};

void RunServer() {
  std::unique_ptr<AHVDiskDatabase> ahv_disk_database = std::make_unique<AHVDiskDatabase>("hashes");
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
