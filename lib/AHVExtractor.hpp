#ifndef AHV_DEFENDER_AHV_EXTRACTOR_H_
#define AHV_DEFENDER_AHV_EXTRACTOR_H_

#include <string>
#include <unordered_set>

#include "AHVUtil.hpp"

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

#endif  // AHV_DEFENDER_AHV_EXTRACTOR_H_