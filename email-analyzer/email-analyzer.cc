#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <cctype>
#include <sstream>
#include <regex>

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

class AHVUtil {
 public:
  // Checks whether the supplied ahv is valid.
  static bool IsValid(const std::string& ahv) {
    if (ahv.size() != 13) {
      // Wrong length, AHV should be exactly 13 digits long.
      return false;
    }
    if (ahv[0] != '7' || ahv[1] != '5' || ahv[2] != '6') {
      // Does not start with 756.
      return false;
    }

    for (char c : ahv) {
      if (!isdigit(c)) {
        // Contains at least one non-digit character.
        return false;
      }
    }

    int checksum = 0, factor = 1;
    for (size_t i = 0; i < 12; ++i) {
      checksum += factor * (int) (ahv[i] - '0');
      factor = 4 - factor;
    }

    // Finally check control digit.
    int expected_control_digit = ((checksum - 1) / 10 + 1) * 10 - checksum;
    int actual_control_digit = (int) (ahv[ahv.size() - 1] - '0');
    return expected_control_digit == actual_control_digit;
  }

  // This function takes a string and converts it into the system canonical
  // representation (int64_t). The function returns true if the digits found in
  // the supplied string form a valid AHV, false otherwise.
  static bool FromString(const std::string& ahvs, int64_t *ahvi) {
    // Start by filtering all digits from the supplied string.
    std::string only_digits;
    int digit_count = 0;
    for (char c : ahvs) {
      if (isdigit(c)) {
        ++digit_count;
        // Max length of an AHV is 13 digits, we discovered more than 13 in the
        // supplied string, so this is not an AHV. However, this probably
        // indicates an error, so we also write to stderr.
        if (digit_count > 13) {
          std::cerr << "AHVUtil::FromString encountered more than 13 digits in"
                    << " the supplied string, this is probably not intended."
                    << std::endl;
          return false;
        }
        only_digits += c;
      }
    }

    // We use IsValid as this process is fast enough, even though some
    // functionality overlaps. This is intended: makes for more reusable code
    // in exchange for a few more microseconds at runtime.
    if (!IsValid(only_digits)) {
      return false;
    }

    // Finally, convert the extracted digits to int64_t.
    *ahvi = 0;
    for (char c : only_digits) {
      int current_digit = (int) (c - '0');
      *ahvi *= 10;
      *ahvi += current_digit;
    }

    return true;
  }
};

class AHVExtractor {
 public:
  // To be implemented in the derived classes.
  virtual void Process(const std::string& text) = 0;

  // Gives access to the results set.
  const std::unordered_set<int64_t>& Results() {
    return results_;
  }

 protected:
  // This method should be called each time a potential AHV is found at a higher
  // level. It converts the string to int64_t and checks its validity and, if
  // these checks pass, adds the converted AHV to the results.
  void Matched(const std::string& match) {
    int64_t ahv = -1;
    if (!AHVUtil::FromString(match, &ahv)) {
      return;
    }
    results_.insert(ahv);
  }

  // We use a set to avoid storing duplicates.
  std::unordered_set<int64_t> results_;
};

// In STANDARD mode we only check a restricted set of regular expressions
// that match some of the more frequently used forms:
//   * Separators can only be one of ' ', '.', '-'.
//   * The three digits in the beginning ('756') are never separated.
//   * There is no more than one separator between two digits.
//   * There's no more than 4 separators in total.
class AHVExtractorStandard : public AHVExtractor {
 public:
  AHVExtractorStandard() {
    regex_list_.push_back(std::regex(
        "756[ \\.-]?([0-9][ \\.-]?){10}",
        std::regex::optimize));
  }

  void Process(const std::string& text) override {
    // Iterate over regex list.
    for (const std::regex re : regex_list_) {
      auto start = std::sregex_iterator(text.begin(), text.end(), re);
      auto stop = std::sregex_iterator();
      // Iterate over matches.
      for (std::sregex_iterator i = start; i != stop; ++i) {
        // We filter out matches that have more than 4 separators.
        if (CountSeparators(i->str()) > 4) {
          continue;
        }
        // Report the match to the base class.
        Matched(i->str());
      }
    }
  }

 private:
  static int CountSeparators(const std::string& ahv) {
    int count = 0;
    for (char c : ahv) {
      if (!isdigit(c)) {
        ++count;
      }
    }
    return count;
  }

  std::vector<std::regex> regex_list_;
};

// In THOROUGH mode we check additional AHV templates via regular
// expressions, but we still look only for the saner of the possible forms:
//   * More separators (eg. '_', '/', ',')
//   * We allow separators to appear more than once between two digits,
//     maybe with some additional restrictions.
//   * We allow up to 6 separators.
class AHVExtractorThorough : public AHVExtractor {
 public:
  void Process(const std::string& text) override {
    // TODO implement.
  }
};

// In PARANOID mode use a double ended queue to scan for any string that
// contains 13 digits with only loose restrictions: they may be separated
// by any characters and need to be "close enough" between them.
class AHVExtractorParanoid : public AHVExtractor {
 public:
  void Process(const std::string& text) override {
    // TODO implement.
  }
};

void TestService() {
  auto client = AHVDatabaseClient::New("localhost:12000");

  auto start_time = std::chrono::high_resolution_clock::now();
  const int iterations = 10000;
  for (int i = 0; i < iterations; ++i) {
    client->Lookup(123);
  }
  auto end_time = std::chrono::high_resolution_clock::now();

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  std::cout << "Took " << duration.count() << "ms." << std::endl;
  int qps = (int) (iterations * 1000 / duration.count());
  std::cout << "QPS: " << qps << std::endl;
}

std::string ReadAllFromStdin() {
  std::ostringstream sstream;
  sstream << std::cin.rdbuf();
  return sstream.str();
}

int main(int argc, char** argv) {
  std::string text = ReadAllFromStdin();
  std::unique_ptr<AHVExtractor> extractor(new AHVExtractorStandard);
  extractor->Process(text);
  for (int64_t ahv : extractor->Results()) {
    std::cout << ahv << std::endl;
  }
  return 0;
}
