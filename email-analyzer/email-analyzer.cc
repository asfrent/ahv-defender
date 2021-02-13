#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <cctype>
#include <sstream>
#include <regex>
#include <deque>
#include <functional>

#include <grpcpp/grpcpp.h>

#include "ahvdefender.grpc.pb.h"
#include "AHVUtil.hpp"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using ahvdefender::AHVDatabase;
using ahvdefender::AHVLookupRequest;
using ahvdefender::AHVLookupResponse;

class AHVDatabaseClient {
 public:
  bool Lookup(const std::string& ahv) {
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

class AHVExtractor {
 public:
  // To be implemented in the derived classes.
  virtual void Process(const std::string& text) = 0;

  // Gives access to the results set.
  const std::unordered_set<std::string>& Results() {
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
  // level. It extracts the digits out of a match, checks its validity and adds
  // the extracted AHV to the results (if valid).
  void Matched(const std::string& match) {
    std::string ahv;
    if (!AHVUtil::ExtractDigits(match, &ahv)) {
      return;
    }
    results_.insert(ahv);
  }

  // We use a set to avoid storing duplicates.
  std::unordered_set<std::string> results_;
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
    // Find out all indexes of digits in the supplied text.
    std::vector<size_t> digit_indexes;
    for (size_t i = 0; i < text.size(); ++i) {
      if (isdigit(text[i])) {
        digit_indexes.push_back(i);
      }
    }

    // If there's less than 13 digits in the string our job is done.
    if (digit_indexes.size() < 13) {
      return;
    }

    // Compute gaps between digits.
    std::vector<size_t> gaps(digit_indexes.size() - 1);
    for (size_t i = 1; i < digit_indexes.size(); ++i) {
      gaps[i - 1] = digit_indexes[i] - digit_indexes[i - 1] - 1;
    }

    // Use a sliding window of 12 (we're working on gaps now) and get the max
    // gap between elements.

    // Start with the first full window.
    std::deque<size_t> q;

    // Adds current index to the double ended queue making sure it's still
    // in descending order.
    auto AddBack = [&] (size_t current_index) {
      while (!q.empty() && gaps[q.back()] <= gaps[current_index]) {
        q.pop_back();
      }
      q.push_back(current_index);
    };

    // Remove an element from the double ended queue if it's outside it.
    auto RemoveFront = [&] (size_t window_start_index) {
      if (!q.empty() && q.front() <= window_start_index) {
        q.pop_front();
      }
    };

    // Process current window.
    auto ProcessWindow = [&] (size_t start_index, size_t end_index) {
      const int max_in_between = 2;

      // If largest gap is too large, stop.
      if (gaps[q.front()] > max_in_between) return;

      // Frist three digits need to match 756.
      if (text[digit_indexes[start_index + 0]] != '7') return;
      if (text[digit_indexes[start_index + 1]] != '5') return;
      if (text[digit_indexes[start_index + 2]] != '6') return;

      // Compose potential AHV string.
      std::string match;
      for (int i = start_index; i <= end_index; ++i) {
        match += text[digit_indexes[i]];
      }

      // Report match to base extractor.
      this->Matched(match);
    };

    // Add all indexes from first window.
    size_t window_start_index = 0;
    size_t window_end_index = 12;
    for (int i = window_start_index; i < window_end_index; ++i) {
      AddBack(i);
    }

    // Process previous window, update queue, slide.
    while(window_end_index < gaps.size()) {
      ProcessWindow(window_start_index, window_end_index);
      RemoveFront(window_start_index);
      AddBack(window_end_index);
      ++window_start_index;
      ++window_end_index;
    }

    // Process last window.
    ProcessWindow(window_start_index, window_end_index);
  }
};

void TestService() {
  auto client = AHVDatabaseClient::New("localhost:12000");

  auto start_time = std::chrono::high_resolution_clock::now();
  const int iterations = 10000;
  for (int i = 0; i < iterations; ++i) {
    client->Lookup("123");
  }
  auto end_time = std::chrono::high_resolution_clock::now();

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  std::cout << "Took " << duration.count() << "ms." << std::endl;
  int qps = (int) (iterations * 1000 / duration.count());
  std::cout << "QPS: " << qps << std::endl;
}

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
