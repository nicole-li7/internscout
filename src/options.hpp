// options.hpp - the choices offered in the app's dropdowns and checkbox lists,
// plus how an "interest" or "location" label expands into words the matcher
// can look for in a job posting.
#pragma once

#include <string>
#include <vector>

namespace options {

const std::vector<std::string>& universities();
const std::vector<std::string>& majors();
const std::vector<std::string>& degreeLevels();   // Bachelor's, Master's, PhD
const std::vector<std::string>& yearsOfStudy();   // "1st year" ... "5th year+"
const std::vector<std::string>& countries();
const std::vector<std::string>& interests();      // "Machine Learning / AI", ...
const std::vector<std::string>& locations();      // "Vancouver", "San Francisco / Bay Area", ...
std::vector<std::string> terms();                 // next few internship terms from today
std::vector<int> graduationYears();               // this year .. +7

// "Machine Learning / AI" -> {"machine learning", "ml", "ai", ...}
// Unknown labels (custom ones the user typed) come back as themselves.
std::vector<std::string> keywordsForInterest(const std::string& label);

// "San Francisco / Bay Area" -> {"San Francisco", "SF", "Palo Alto", ...}
std::vector<std::string> placesForLocation(const std::string& label);

}  // namespace options
