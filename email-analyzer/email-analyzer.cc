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
  static int CountDigitGroups(const std::string& s) {
    // Find first digit.
    size_t i = 0;
    int count = 0;
    while (i < s.size()) {
      // Skip non digit sequence.
      while (i < s.size() && !isdigit(s[i])) {
        ++i;
      }
      // Did we hit the end?
      if (i == s.size()) {
        return count;
      }
      // Then we have a digit sequence.
      ++count;
      // Skip the digit sequence.
      while (i < s.size() && isdigit(s[i])) {
        ++i;
      }
    }
    return count;
  }

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
//   * There's no more than 4 digit groups.
class AHVExtractorStandard : public AHVExtractor {
 public:
  AHVExtractorStandard() {
    regex_list_.push_back(
        std::regex( "756([ \\.\\-]?[0-9]){10}", std::regex::optimize));
  }

  void Process(const std::string& text) override {
    const int max_digit_groups = 4;

    // Iterate over regex list.
    for (const std::regex re : regex_list_) {
      auto start = std::sregex_iterator(text.begin(), text.end(), re);
      auto stop = std::sregex_iterator();
      // Iterate over matches.
      for (std::sregex_iterator i = start; i != stop; ++i) {
        // We filter out matches that have more digit groups than we allow.
        if (CountDigitGroups(i->str()) > max_digit_groups) {
          continue;
        }
        // Report the match to the base class.
        Matched(i->str());
      }
    }
  }

 private:
  std::vector<std::regex> regex_list_;
};

// In THOROUGH mode we check additional AHV templates via regular
// expressions, but we still look only for the saner of the possible forms:
//   * More separators (eg. '_', '/', ',')
//   * We allow separators to appear at most twice between two digits,
//     maybe with some additional restrictions.
//   * We allow more digit groups than standard.
class AHVExtractorThorough : public AHVExtractor {
 public:
  AHVExtractorThorough() {
    // We include the standard separators: ' ', '.', '-'
    // We include some other (sane) separators: '/' '_' '\t'
    // We allow for some typos: ',' '*' ':' ';'
    // Note on typos: these are on keys that are close to standard separators or
    // can be obtained by pressing shift on or aound standard separator keys.
    // The list is not exhaustive and only takes into consideration German and US
    // layouts.
    std::string separators =
        standard_separators_ + other_separators_ + typo_separators_;
    regex_list_.push_back(
        std::regex("756([" + separators + "]{0,2}[0-9]){10}", std::regex::optimize));
  }

  void Process(const std::string& text) override {
    const int max_digit_groups = 6;

    // Iterate over regex list.
    for (const std::regex re : regex_list_) {
      auto start = std::sregex_iterator(text.begin(), text.end(), re);
      auto stop = std::sregex_iterator();
      // Iterate over matches.
      for (std::sregex_iterator i = start; i != stop; ++i) {
        // We filter out matches that have more digit groups than we allow.
        if (CountDigitGroups(i->str()) > max_digit_groups) {
          continue;
        }
        // Report the match to the base class.
        Matched(i->str());
      }
    }
  }

 private:
   static const std::string standard_separators_;
   static const std::string other_separators_;
   static const std::string typo_separators_;

  std::vector<std::regex> regex_list_;
};

const std::string AHVExtractorThorough::standard_separators_ = " \\.\\-";
const std::string AHVExtractorThorough::other_separators_ = "\\/_\\t";
const std::string AHVExtractorThorough::typo_separators_ = ",\\*:;";

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

void PrintUsage() {
  std::cerr << "Usage: ./email-analyzer standard|thorough|paranoid" << std::endl;
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

  // Build extractor from spec and use it to process all text from standard
  // input.
  auto extractor = NewExtractorFromSpec(argv[1]);
  extractor->Process(ReadAllFromStdin());
  for (int64_t ahv : extractor->Results()) {
    std::cout << ahv << std::endl;
  }

  return 0;
}
