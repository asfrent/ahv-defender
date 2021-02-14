#include <iostream>
#include <chrono>
#include <memory>
#include <string>
#include <functional>

#include "AHVDatabaseClient.hpp"

using namespace std::chrono;

void PrintUsage() {
  std::cerr << "Usage: ./cli db_server_address add|remove|lookup [quiet|time]" << std::endl;
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
  bool quiet = false, time = false;
  if (argc == 4) {
    if (strcmp(argv[3], "quiet") == 0) {
      quiet = true;
    } else if (strcmp(argv[3], "time") == 0) {
      time = true;
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
  std::ios::sync_with_stdio(false);
  std::string ahv;

  int line_count = 0;
  auto start = high_resolution_clock::now();
  while (std::getline(std::cin, ahv)) {
    f(ahv);
    ++line_count;
  }
  if (line_count == 0) return 1;
  auto stop = high_resolution_clock::now();
  auto duration_ms = duration_cast<milliseconds>(stop - start);
  auto duration_s = duration_cast<seconds>(stop - start);
  if (time) {
    std::cout << "Took " << duration_ms.count() << "ms." << std::endl;
    if (duration_s.count() > 0) {
      int qps = line_count / duration_s.count();
      std::cout << "QPS: " << qps << std::endl;
    }
    std::cout << "Average request duration: " << duration_ms.count() / line_count << "ms." << std::endl;
  }

  return 0;
}
