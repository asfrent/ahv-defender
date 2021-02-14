#include <string>
#include <iostream>

#include "AHVUtil.hpp"
#include "AHVDatabaseClient.hpp"
#include "AHVExtractor.hpp"
#include "AHVExtractorStandard.hpp"
#include "AHVExtractorThorough.hpp"
#include "AHVExtractorParanoid.hpp"


std::string ReadAllFromStdin() {
  std::ios::sync_with_stdio(false);
  std::ostringstream sstream;
  sstream << std::cin.rdbuf();
  return sstream.str();
}

void PrintUsage() {
  std::cerr << "Usage: ./email-analyzer standard|thorough|paranoid [db_server_address]" << std::endl;
}

// Builds a new AHVExtractor from the supplied string specification.
std::unique_ptr<AHVExtractor> NewExtractorFromSpec(const std::string& spec) {
  if (spec == "standard") {
    return std::make_unique<AHVExtractorStandard>();
  } else if (spec == "thorough") {
    return std::make_unique<AHVExtractorThorough>();
  } else if (spec == "paranoid") {
    return std::make_unique<AHVExtractorParanoid>();
  } else {
    PrintUsage();
    exit(-1);
  }
}

int main(int argc, char** argv) {
  // Check argument count.
  if (argc < 2) {
    PrintUsage();
    exit(-1);
  }

  // Initialize a database client if the target argument was supplied.
  auto ahv_database_client =
      argc == 3 ? AHVDatabaseClient::New(argv[2])
                : std::unique_ptr<AHVDatabaseClient>(nullptr);

  // Build extractor from spec and use it to process all text from standard
  // input.
  auto extractor = NewExtractorFromSpec(argv[1]);
  extractor->Process(ReadAllFromStdin());
  for (const std::string& ahv : extractor->Results()) {
    if (ahv_database_client.get() == nullptr || ahv_database_client->Lookup(ahv)) {
      std::cout << ahv << std::endl;
    }
  }

  return 0;
}
