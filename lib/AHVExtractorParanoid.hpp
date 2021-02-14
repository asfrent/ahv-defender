#ifndef AHV_DEFENDER_AHV_EXTRACTOR_PARANOID_H_
#define AHV_DEFENDER_AHV_EXTRACTOR_PARANOID_H_

#include <string>
#include <regex>

#include "AHVExtractor.hpp"

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

#endif  // AHV_DEFENDER_AHV_EXTRACTOR_PARANOID_H_