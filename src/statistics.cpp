#include "statistics.h"

namespace Statistics {

// TODO(C): 아래 전부 구현

double calcGPA(const GradeManager&, const std::string&, const std::string&) {
    return 0.0;
}

CourseStats courseStats(const GradeManager&, const std::string& courseCode) {
    CourseStats stats;
    stats.courseCode = courseCode;
    return stats;
}

std::vector<RankEntry> ranking(const GradeManager&, const std::string&) {
    return {};
}

}  // namespace Statistics
