#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <cctype>
#include <sstream>
#include <regex>
#include <deque>
#include <functional>

#include <grpcpp/grpcpp.h>

#include "ahvdefender.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using ahvdefender::AHVDatabase;
using ahvdefender::AHVLookupRequest;
using ahvdefender::AHVLookupResponse;
using ahvdefender::AHVAddRequest;
using ahvdefender::AHVAddResponse;
using ahvdefender::AHVRemoveRequest;
using ahvdefender::AHVRemoveResponse;

class AHVDatabaseClient {
 public:
  bool Lookup(const std::string& ahv) {
    AHVLookupRequest request;
    request.set_ahv(ahv);
    AHVLookupResponse response;
    ClientContext context;
    Status status = stub_->Lookup(&context, request, &response);
    if (!status.ok()) {
      std::cerr << status.error_code() << ": " << status.error_message() << std::endl;
      exit(1);
    }
    return response.found();
  }

  void Add(const std::string& ahv) {
    AHVAddRequest request;
    request.set_ahv(ahv);
    AHVAddResponse response;
    ClientContext context;
    Status status = stub_->Add(&context, request, &response);
    if (!status.ok()) {
      std::cerr << status.error_code() << ": " << status.error_message() << std::endl;
      exit(1);
    }
  }

  void Remove(const std::string& ahv) {
    AHVRemoveRequest request;
    request.set_ahv(ahv);
    AHVRemoveResponse response;
    ClientContext context;
    Status status = stub_->Remove(&context, request, &response);
    if (!status.ok()) {
      std::cerr << status.error_code() << ": " << status.error_message() << std::endl;
      exit(1);
    }
  }

  static std::unique_ptr<AHVDatabaseClient> New(const std::string& target) {
    auto insecure_credentials = grpc::InsecureChannelCredentials();
    auto grpc_channel = grpc::CreateChannel(target, insecure_credentials);
    return std::unique_ptr<AHVDatabaseClient>(new AHVDatabaseClient(grpc_channel));
  }

 private:
  AHVDatabaseClient(std::shared_ptr<Channel> channel)
      : stub_(AHVDatabase::NewStub(channel)) {}

  std::unique_ptr<AHVDatabase::Stub> stub_;
};

void PrintUsage() {
  std::cerr << "Usage: ./cli db_server_address add|remove|lookup ahv" << std::endl;
}

int main(int argc, char** argv) {
  // Check argument count.
  if (argc != 4) {
    PrintUsage();
    exit(1);
  }

  auto ahv_database_client = AHVDatabaseClient::New(argv[1]);
  std::string action(argv[2]);
  std::string ahv(argv[3]);

  if (action == "add") {
    ahv_database_client->Add(ahv);
  } else if (action == "remove") {
    ahv_database_client->Remove(ahv);
  } else if (action == "lookup") {
    if (ahv_database_client->Lookup(ahv)) {
      std::cout << "true" << std::endl;
    } else {
      std::cout << "false" << std::endl;
    }
  } else {
    std::cerr << "Unknown action \"" << action << "\"." << std::endl;
    PrintUsage();
    exit(1);
  }

  return 0;
}
