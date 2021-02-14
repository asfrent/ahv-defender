#ifndef AHV_DEFENDER_AHV_STORE_FILE_H_
#define AHV_DEFENDER_AHV_STORE_FILE_H_

#include <string>
#include <cstring>
#include <fstream>
#include <mutex>
#include <iostream>
#include <functional>
#include <unistd.h>
#include <sys/stat.h>

#include "DiskRecord.hpp"

class AHVStore_File {
 public:
  AHVStore_File(const std::string& filename) {
    if (!FileExists(filename)) {
      CreateEmptyFile(filename);
    }
    fs_.open(filename, std::ios::binary | std::ios::in | std::ios::out);
  }

  ~AHVStore_File() {
    fs_.close();
    std::cout << "File store cleanly destructed." << std::endl;
  }

  void ForEach(std::function<void(const char*, int64_t)> tell_record,
               std::function<void(int64_t)> tell_free) {
    fs_mutex_.lock();
    fs_.seekg(0, std::ios::end);
    int64_t remaining = fs_.tellg();
    fs_.seekg(0, std::ios::beg);
    char buffer[4096];
    int64_t record_index = 0;
    while(remaining > 0) {
      int count = remaining < 4096 ? (int) remaining : 4096;
      fs_.read(buffer, count);
      int buffer_index = 0;
      while (buffer_index < count) {
        if (*(buffer + buffer_index) == 0x01) {
          tell_record(buffer + buffer_index, record_index);
        } else {
          tell_free(record_index);
        }
        record_index += 32;
        if ((record_index / 32) % 100000 == 0) {
          std::cout << "Loaded " << (record_index / 32) << " hashes..." << std::endl;
        }
        buffer_index += 32;
      }
      remaining -= count;
    }
    fs_mutex_.unlock();
  }

  int64_t Add(const std::string& hash) {
    // Prepare disk record.
    DiskRecord disk_record;
    disk_record.set_used(true);
    memcpy(disk_record.data + 1, hash.c_str(), 31);

    // Append.
    fs_mutex_.lock();
    fs_.seekp(0, std::ios::end);
    int64_t record_index = fs_.tellp();
    fs_.write((const char*) disk_record.data, 32);
    fs_mutex_.unlock();
    return record_index;
  }

  void Remove(int64_t record_index) {
    fs_mutex_.lock();
    fs_.seekp(record_index);
    fs_.write((const char*) DiskRecord::empty_record().data, 32);
    fs_mutex_.unlock();
  }

  bool HashAtEquals(int64_t record_index, const std::string& hash) {
    char buffer[31];
    fs_mutex_.lock();
    fs_.seekg(record_index + 1);
    fs_.read(buffer, 31);
    fs_mutex_.unlock();
    return strncmp(hash.c_str(), buffer, 31) == 0;
  }

 private:
  static bool FileExists(const std::string& filename) {
    struct stat buffer;
    return stat(filename.c_str(), &buffer) == 0;
  }

  static void CreateEmptyFile(const std::string& filename) {
    std::ofstream output(filename);
    output.close();
  }

  std::fstream fs_;
  std::mutex fs_mutex_;
};

#endif  // AHV_DEFENDER_AHV_STORE_FILE_H_