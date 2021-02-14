#include <iostream>
#include <string>
#include <random>
#include <cstdlib>
#include <cstring>

void PrintUsage() {
  std::cout << "Usage: ./gen-ahv count" << std::endl;
}

int main(int argc, char** argv) {
  // Check argument count.
  if (argc != 2) {
    PrintUsage();
    exit(1);
  }

  int n = atoi(argv[1]);
  char s[14];
  s[0] = '7';
  s[1] = '5';
  s[2] = '6';
  s[13] = '\0';
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<int> dist(0, 9);
  while (n--) {
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
    std::cout << s << std::endl;
  }

  return 0;
}
