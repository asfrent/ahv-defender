#include <string>
#include <mutex>
#include <iostream>
#include <thread>
#include <random>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "BCryptHasher.hpp"

void PrintUsage() {
  std::cout << "Usage: ./db-gen count plaintext_file hashes_file" << std::endl;
}

FILE* plaintext_f;
FILE* hashes_f;

void HashAndWrite(const std::string& ahv, const BCryptHasher& hasher) {
  static std::mutex m;
  std::string h = hasher.ComputeHash(ahv);
  m.lock();
  fprintf(plaintext_f, "%s\n", ahv.c_str());
  fprintf(hashes_f, "%c%s", (char) 0x01, h.substr(h.size() - 31).c_str());
  m.unlock();
}

int main(int argc, char** argv) {
  // Check argument count.
  if (argc != 4) {
    PrintUsage();
    exit(1);
  }

  plaintext_f = fopen(argv[2], "wb");
  hashes_f = fopen(argv[3], "w");

  int nthreads = std::thread::hardware_concurrency();

  BCryptHasher hasher;

  int n = atoi(argv[1]);
  char s[14];
  s[0] = '7';
  s[1] = '5';
  s[2] = '6';
  s[13] = '\0';
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<int> dist(0, 9);
  std::vector<std::thread> t(nthreads);
  for (int i = 0; i < nthreads; ++i) {
    t[i] = std::thread([] () -> void {});
  }
  int thread_index = 0;
  while (n--) {
    if (n % 1000 == 0) {
      printf("Remaining: %d\n", n);
    }
    int checksum = 28, factor = 3;
    memset(s + 3, '0', 10);
    for (int i = 3; i <= 11; ++i) {
      int r = dist(mt);
      checksum += r * factor;
      s[i] +=  r;
      factor = 4 - factor;
    }
    int last_digit = ((checksum - 1) / 10 + 1) * 10 - checksum;
    s[12] += last_digit;
    t[thread_index].join();
    t[thread_index] = std::thread(HashAndWrite, std::string(s), hasher);
    thread_index = (thread_index + 1) % 4;
  }

  for (int i = 0; i < nthreads; ++i) {
    t[i].join();
  }

  fclose(plaintext_f);
  fclose(hashes_f);

  printf("Done.");

  return 0;
}
