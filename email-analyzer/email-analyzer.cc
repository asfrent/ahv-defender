#include <iostream>
#include <memory>
#include <string>
#include <chrono>

#include <grpcpp/grpcpp.h>

#include "ahvdefender.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using ahvdefender::AHVDatabase;
using ahvdefender::AHVLookupRequest;
using ahvdefender::AHVLookupResponse;

class AHVDatabaseClient {
 public:
  AHVDatabaseClient(std::shared_ptr<Channel> channel)
      : stub_(AHVDatabase::NewStub(channel)) {}

  bool Lookup(int64_t ahv) {
    AHVLookupRequest request;
    request.set_ahv(ahv);

    AHVLookupResponse response;

    ClientContext context;
    Status status = stub_->Lookup(&context, request, &response);

    if (status.ok()) {
      return response.found();
    } else {
      std::cerr << status.error_code() << ": " << status.error_message() << std::endl;
      // TODO we should probably create a strict mode which would allow us to
      // control the returned value in case of failure.
      return false;
    }
  }

 private:
  std::unique_ptr<AHVDatabase::Stub> stub_;
};

int main(int argc, char** argv) {
  auto insecure_credentials = grpc::InsecureChannelCredentials();
  auto grpc_channel = grpc::CreateChannel("localhost:12000", insecure_credentials);
  AHVDatabaseClient client(grpc_channel);
  auto start_time = std::chrono::high_resolution_clock::now();

  const int iterations = 10000;
  for (int i = 0; i < iterations; ++i) {
    client.Lookup(123);
  }
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  std::cout << "Took " << duration.count() << "ms." << std::endl;

  int qps = (int) (iterations * 1000 / duration.count());
  std::cout << "QPS: " << qps << std::endl;

  return 0;
}
