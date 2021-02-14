#ifndef AHV_DEFENDER_AHV_UTIL_
#define AHV_DEFENDER_AHV_UTIL_

#include <cctype>
#include <iostream>
#include <string>

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
  // representation (string). The function returns true if the digits found in
  // the supplied string form a valid AHV, false otherwise.
  static bool ExtractDigits(const std::string& ahv_in, std::string *ahv_out) {
    // Start by filtering all digits from the supplied string.
    *ahv_out = "";
    int digit_count = 0;
    for (char c : ahv_in) {
      if (isdigit(c)) {
        ++digit_count;
        // Max length of an AHV is 13 digits, we discovered more than 13 in the
        // supplied string, so this is not an AHV. However, this probably
        // indicates an error, so we also write to stderr.
        if (digit_count > 13) {
          std::cerr << "AHVUtil::FromString encountered more than 13 digits in"
                    << " the supplied string, this is probably not intended."
                    << std::endl;
          *ahv_out = "";
          return false;
        }
        *ahv_out += c;
      }
    }

    // Validate the extracted digits.
    bool is_valid =  IsValid(*ahv_out);
    if (!is_valid) {
      *ahv_out = "";
    }
    return is_valid;
  }
};

#endif  // AHV_DEFENDER_AHV_UTIL_