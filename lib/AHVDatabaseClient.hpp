#ifndef AHV_DEFENDER_AHV_DATABASE_CLIENT_H_
#define AHV_DEFENDER_AHV_DATABASE_CLIENT_H_

#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>

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

#endif  // AHV_DEFENDER_AHV_DATABASE_CLIENT_H_