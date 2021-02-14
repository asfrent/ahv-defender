#include <string>
#include <iostream>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include <mutex>

#include "BCryptHasher.hpp"

void PrintUsage() {
  std::cout << "Usage: ./db-build < plaintext_file > hashes_file" << std::endl;
}

void HashAndWrite(const std::string& ahv, const BCryptHasher& hasher) {
  static std::mutex m;
  std::string h = hasher.ComputeHash(ahv);
  m.lock();
  printf("%c%s", (char) 0x01, h.substr(h.size() - 31).c_str());
  m.unlock();
}

int main(int argc, char** argv) {
  // Check argument count.
  if (argc != 1) {
    PrintUsage();
    exit(1);
  }

  BCryptHasher hasher;
  int nthreads = std::thread::hardware_concurrency();
  std::vector<std::thread> t(nthreads);
  for (int i = 0; i < nthreads; ++i) {
    t[i] = std::thread([] () -> void {});
  }
  int thread_index = 0;
  int n = 0;
  while (!feof(stdin)) {
    char ahv[1024];
    scanf("%s\n", ahv);
    t[thread_index].join();
    t[thread_index] = std::thread(HashAndWrite, std::string(ahv), hasher);
    thread_index = (thread_index + 1) % 4;
    ++n;
  }

  for (int i = 0; i < nthreads; ++i) {
    t[i].join();
  }

  return 0;
}
