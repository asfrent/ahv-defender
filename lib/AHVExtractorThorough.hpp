#ifndef AHV_DEFENDER_AHV_EXTRACTOR_THOROUGH_H_
#define AHV_DEFENDER_AHV_EXTRACTOR_THOROUGH_H_

#include <string>
#include <regex>

#include "AHVExtractor.hpp"

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


#endif  // AHV_DEFENDER_AHV_EXTRACTOR_THOROUGH_H_