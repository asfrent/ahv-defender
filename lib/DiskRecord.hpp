#include <cstring>

#ifndef AHV_DEFENDER_DISK_RECORD_H_
#define AHV_DEFENDER_DISK_RECORD_H_

struct DiskRecord {
  void set_used(bool used) {
    data[0] = (unsigned char) (used ? 0x01 : 0x00);
  }

  static const DiskRecord& empty_record() {
    static bool initialized = false;
    static DiskRecord empty;
    if (!initialized) {
      empty.set_used(false);
      memset((unsigned char*) empty.data + 1, 0, 31);
      initialized = true;
    }
    return empty;
  }

  // Record layout:
  //   * byte 0: T if index is used, F if index is free.
  //   * bytes 1-32: bcrypt binary hash.
  unsigned char data[32];
};

#endif  // AHV_DEFENDER_DISK_RECORD_H_