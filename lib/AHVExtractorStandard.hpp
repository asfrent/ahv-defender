#ifndef AHV_DEFENDER_AHV_EXTRACTOR_STANDARD_H_
#define AHV_DEFENDER_AHV_EXTRACTOR_STANDARD_H_

#include <regex>
#include <string>
#include <vector>

#include "AHVExtractor.hpp"

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

#endif  // AHV_DEFENDER_AHV_EXTRACTOR_STANDARD_H_