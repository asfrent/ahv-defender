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

  bool Add(const std::string& ahv) {
    AHVAddRequest request;
    request.set_ahv(ahv);
    AHVAddResponse response;
    ClientContext context;
    Status status = stub_->Add(&context, request, &response);
    if (!status.ok()) {
      std::cerr << status.error_code() << ": " << status.error_message() << std::endl;
      exit(1);
    }
    return response.added();
  }

  bool Remove(const std::string& ahv) {
    AHVRemoveRequest request;
    request.set_ahv(ahv);
    AHVRemoveResponse response;
    ClientContext context;
    Status status = stub_->Remove(&context, request, &response);
    if (!status.ok()) {
      std::cerr << status.error_code() << ": " << status.error_message() << std::endl;
      exit(1);
    }
    return response.removed();
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
  std::cerr << "Usage: ./cli db_server_address add|remove|lookup [quiet]" << std::endl;
}

int main(int argc, char** argv) {
  // Check argument count.
  if (argc < 3 || argc > 4) {
    PrintUsage();
    exit(1);
  }

  // Find target, action from args.
  std::string target(argv[1]);
  std::string action(argv[2]);

  // Sort out quiet arg.
  bool quiet = false;
  if (argc == 4) {
    if (strcmp(argv[3], "quiet") == 0) {
      quiet = true;
    } else {
      PrintUsage();
      exit(1);
    }
  }

  // Init db client.
  auto ahv_database_client = AHVDatabaseClient::New(target);

  // One lambda per action.
  auto add_fn = [&] (const std::string& ahv) -> void {
    bool result = ahv_database_client->Add(ahv);
    if (quiet) return;
    std::cout << (result ? "true" : "false") << std::endl;
  };
  auto remove_fn = [&] (const std::string& ahv) -> void {
    bool result = ahv_database_client->Remove(ahv);
    if (quiet) return;
    std::cout << (result ? "true" : "false") << std::endl;
  };
  auto lookup_fn = [&] (const std::string& ahv) -> void {
    bool result = ahv_database_client->Lookup(ahv);
    if (quiet) return;
    std::cout << (result ? "true" : "false") << std::endl;
  };

  // Choose the lambda based on action arg.
  std::function<void(const std::string& ahv)> f;
  if (action == "add") {
    f = add_fn;
  } else if (action == "remove") {
    f = remove_fn;
  } else if (action == "lookup") {
    f = lookup_fn;
  } else {
    std::cerr << "Unknown action \"" << action << "\"." << std::endl;
    PrintUsage();
    exit(1);
  }

  // Read all lines, execute action.
  std::string ahv;
  while (std::getline(std::cin, ahv)) {
    f(ahv);
  }

  return 0;
}
