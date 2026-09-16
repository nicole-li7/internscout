#include "options.hpp"

#include <ctime>
#include <map>

#include "text.hpp"

namespace options {

const std::vector<std::string>& universities() {
    static const std::vector<std::string> v = {
        "University of British Columbia", "Simon Fraser University", "University of Victoria",
        "University of Toronto", "University of Waterloo", "McGill University", "McMaster University",
        "Western University", "Queen's University", "University of Alberta", "University of Calgary",
        "University of Ottawa", "Carleton University", "York University", "Toronto Metropolitan University",
        "Université de Montréal", "Concordia University", "Dalhousie University", "University of Manitoba",
        "University of Saskatchewan", "Laval University", "Wilfrid Laurier University", "University of Guelph",
        "BCIT", "Langara College",
        "Stanford University", "MIT", "UC Berkeley", "Carnegie Mellon University", "University of Washington",
        "Georgia Tech", "University of Illinois Urbana-Champaign", "University of Michigan", "Cornell University",
        "Harvard University", "Princeton University", "Caltech", "UCLA", "UC San Diego", "University of Texas at Austin",
        "Columbia University", "New York University", "University of Pennsylvania", "Purdue University",
        "University of Wisconsin-Madison", "University of Maryland", "Northeastern University", "Duke University",
        "Yale University", "Brown University", "University of Southern California",
        "Other",
    };
    return v;
}

const std::vector<std::string>& majors() {
    static const std::vector<std::string> v = {
        "Computer Science", "Computer Engineering", "Software Engineering", "Data Science", "Statistics",
        "Mathematics", "Electrical Engineering", "Mechanical Engineering", "Mechatronics Engineering",
        "Civil Engineering", "Chemical Engineering", "Biomedical Engineering", "Engineering Physics",
        "Physics", "Chemistry", "Biology", "Biochemistry", "Microbiology", "Neuroscience", "Psychology",
        "Cognitive Science", "Commerce / Business", "Economics", "Finance", "Accounting", "Marketing",
        "Design", "Communications", "Political Science", "International Relations", "English", "Philosophy",
        "Other",
    };
    return v;
}

const std::vector<std::string>& degreeLevels() {
    static const std::vector<std::string> v = {"Bachelor's", "Master's", "PhD"};
    return v;
}

const std::vector<std::string>& yearsOfStudy() {
    static const std::vector<std::string> v = {"1st year", "2nd year", "3rd year", "4th year", "5th year+"};
    return v;
}

const std::vector<std::string>& countries() {
    static const std::vector<std::string> v = {"Canada", "United States", "United Kingdom", "Other"};
    return v;
}

// label -> keywords the matcher looks for in titles / descriptions
static const std::map<std::string, std::vector<std::string>>& interestTable() {
    static const std::map<std::string, std::vector<std::string>> t = {
        {"Software Engineering", {"software", "engineer", "developer", "sde", "swe", "programmer", "engineering"}},
        {"Backend", {"backend", "back-end", "back end", "api", "server", "distributed", "microservices"}},
        {"Frontend / Web", {"frontend", "front-end", "front end", "web", "react", "javascript", "typescript", "ui engineer"}},
        {"Full Stack", {"full stack", "fullstack", "full-stack"}},
        {"Mobile (iOS / Android)", {"mobile", "ios", "android", "swift", "kotlin"}},
        {"Machine Learning / AI", {"machine learning", "ml", "ai", "deep learning", "nlp", "computer vision", "llm", "applied scientist"}},
        {"Data Science / Analytics", {"data", "analytics", "analyst", "data science", "data scientist", "business intelligence"}},
        {"Cloud / Infrastructure / DevOps", {"cloud", "infrastructure", "devops", "sre", "platform", "reliability", "kubernetes", "aws"}},
        {"Cybersecurity", {"security", "cybersecurity", "cyber", "infosec", "penetration", "threat"}},
        {"Systems / Embedded", {"systems", "embedded", "firmware", "kernel", "low-level", "c++", "rtos"}},
        {"Hardware / Electrical", {"hardware", "electrical", "fpga", "asic", "circuit", "silicon", "rf", "pcb", "verification"}},
        {"Robotics / Controls", {"robotics", "robot", "controls", "autonomy", "autonomous", "perception", "mechatronics"}},
        {"Game Development", {"game", "gameplay", "unity", "unreal", "graphics", "rendering"}},
        {"Product Management", {"product", "product manager", "pm", "product management"}},
        {"UX / UI Design", {"ux", "ui", "design", "designer", "user experience", "product design"}},
        {"Quant / Trading", {"quant", "quantitative", "trading", "trader", "algorithmic"}},
        {"Finance / Banking", {"finance", "financial", "banking", "investment", "equity", "capital markets", "accounting", "audit"}},
        {"Consulting / Strategy", {"consulting", "consultant", "strategy", "business analyst", "operations"}},
        {"Marketing / Growth", {"marketing", "growth", "brand", "content", "communications", "social media"}},
        {"Research", {"research", "researcher", "scientist", "lab", "r&d"}},
        {"Biotech / Health", {"biotech", "biology", "clinical", "health", "pharma", "medical", "bioinformatics", "genomics"}},
        {"Sustainability / Energy", {"sustainability", "energy", "climate", "environmental", "renewable"}},
    };
    return t;
}

const std::vector<std::string>& interests() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> out;
        for (const auto& [label, _] : interestTable()) out.push_back(label);
        return out;
    }();
    return v;
}

std::vector<std::string> keywordsForInterest(const std::string& label) {
    auto it = interestTable().find(label);
    if (it != interestTable().end()) return it->second;
    return {label};
}

static const std::map<std::string, std::vector<std::string>>& locationTable() {
    static const std::map<std::string, std::vector<std::string>> t = {
        {"Vancouver", {"Vancouver", "Burnaby", "Richmond, BC", "Surrey, BC"}},
        {"Victoria", {"Victoria, BC"}},
        {"Calgary", {"Calgary"}},
        {"Edmonton", {"Edmonton"}},
        {"Toronto", {"Toronto", "Mississauga", "Markham", "Scarborough"}},
        {"Waterloo / Kitchener", {"Waterloo", "Kitchener"}},
        {"Ottawa", {"Ottawa", "Kanata"}},
        {"Montreal", {"Montreal", "Montréal"}},
        {"Remote (Canada)", {"Remote in Canada", "Remote, Canada", "Canada Remote"}},
        {"Seattle", {"Seattle", "Bellevue", "Redmond", "Kirkland"}},
        {"San Francisco / Bay Area", {"San Francisco", "SF", "Bay Area", "Palo Alto", "Mountain View", "San Jose",
                                      "Sunnyvale", "Menlo Park", "Santa Clara", "Redwood City", "Cupertino",
                                      "Oakland", "Fremont", "South San Francisco"}},
        {"Los Angeles", {"Los Angeles", "LA", "Santa Monica", "Irvine", "Pasadena", "El Segundo"}},
        {"San Diego", {"San Diego"}},
        {"New York", {"New York", "NYC", "Brooklyn", "Jersey City"}},
        {"Boston", {"Boston", "Cambridge, MA", "Waltham"}},
        {"Chicago", {"Chicago"}},
        {"Austin", {"Austin"}},
        {"Washington DC", {"Washington, DC", "Washington DC", "Arlington", "Reston", "McLean"}},
        {"Denver / Boulder", {"Denver", "Boulder"}},
        {"Atlanta", {"Atlanta"}},
        {"Remote (USA)", {"Remote in USA", "Remote, USA", "Remote - US", "US Remote"}},
        {"London (UK)", {"London, UK", "London, United Kingdom", "London, England"}},
        {"Dublin", {"Dublin"}},
        {"Singapore", {"Singapore"}},
    };
    return t;
}

const std::vector<std::string>& locations() {
    static const std::vector<std::string> v = {
        "Vancouver", "Victoria", "Calgary", "Edmonton", "Toronto", "Waterloo / Kitchener", "Ottawa", "Montreal",
        "Remote (Canada)", "Seattle", "San Francisco / Bay Area", "Los Angeles", "San Diego", "New York", "Boston",
        "Chicago", "Austin", "Washington DC", "Denver / Boulder", "Atlanta", "Remote (USA)", "London (UK)",
        "Dublin", "Singapore",
    };
    return v;
}

std::vector<std::string> placesForLocation(const std::string& label) {
    auto it = locationTable().find(label);
    if (it != locationTable().end()) return it->second;
    return {label};
}

std::vector<std::string> terms() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    int year = tm.tm_year + 1900;
    int month = tm.tm_mon + 1;
    // Start from the next season that has not begun yet and list two years of terms.
    static const char* seasons[] = {"Winter", "Spring", "Summer", "Fall"};
    int seasonIdx = month <= 1 ? 1 : month <= 4 ? 2 : month <= 8 ? 3 : 0;  // index of the *next* season
    if (seasonIdx == 0) ++year;                                             // Winter belongs to the next year
    std::vector<std::string> out;
    for (int i = 0; i < 8; ++i) {
        out.push_back(std::string(seasons[seasonIdx]) + " " + std::to_string(year));
        if (++seasonIdx == 4) { seasonIdx = 0; ++year; }
    }
    return out;
}

std::vector<int> graduationYears() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    int year = tm.tm_year + 1900;
    std::vector<int> out;
    for (int y = year; y <= year + 7; ++y) out.push_back(y);
    return out;
}

}  // namespace options
