#include <string>
#include <iostream>
#include <random>
#include <cstdlib>
#include <cstring>
#include <cstdio>

void PrintUsage() {
  std::cout << "Usage: ./db-gen-fake count hashes_file" << std::endl;
}


int main(int argc, char** argv) {
  // Check argument count.
  if (argc != 3) {
    PrintUsage();
    exit(1);
  }

  int n = atoi(argv[1]);
  FILE* hashes_f = fopen(argv[2], "w");

  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<int64_t> dist(0, std::numeric_limits<int64_t>::max());

  int64_t* data = new int64_t[4];
  while (n--) {
    for (int i = 0; i < 4; ++i) {
      data[i] = dist(mt);
    }
    *(unsigned char*)data = 0x01;
    fwrite((void*)data, 1, 32, hashes_f);
  }
  delete[] data;

  fclose(hashes_f);
  printf("Done.\n");
  return 0;
}
