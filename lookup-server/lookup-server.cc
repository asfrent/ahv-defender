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
#include <chrono>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include "ahvdefender.grpc.pb.h"

#include "BCryptHasher.hpp"
#include "DiskRecord.hpp"
#include "AHVCache_RadixBucket.hpp"
#include "AHVCache_HashMap.hpp"
#include "AHVCache_Radix.hpp"
#include "AHVStore_File.hpp"

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
  std::ios::sync_with_stdio(false);
  SetUpSigIntHandler();
  RunServer();
  std::cout << "Bye." << std::endl;
  return 0;
}
