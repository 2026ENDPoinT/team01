#include "assessment.h"

#include <cctype>
#include <cmath>

#include "course.h"
#include "grade.h"

namespace {

bool isEightDigits(const std::string& s) {
    if (s.size() != 8) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

// 공백만이 아니고 60바이트 이하
bool isValidName(const std::string& name) {
    if (name.empty() || name.size() > 60) return false;
    for (unsigned char c : name) {
        if (!std::isspace(c)) return true;
    }
    return false;
}

}  // namespace

bool Assessment::isValid() const {
    return isValidCourseCode(courseCode) && isValidSemester(semester) && isValidName(name) && weight >= 1 &&
           weight <= 100;
}

bool AssessmentScore::isValid() const {
    return isEightDigits(studentId) && isValidCourseCode(courseCode) && isValidSemester(semester) &&
           isValidName(name) && !std::isnan(score) && score >= 0.0 && score <= 100.0;
}

double finalScore(const std::vector<Assessment>& items, const std::vector<AssessmentScore>& scores) {
    int weightSum = 0;
    for (const Assessment& item : items) {
        weightSum += item.weight;
    }
    if (items.empty() || weightSum != 100) {
        return -1;
    }

    double total = 0.0;  // Σ(점수 × 비율)
    for (const Assessment& item : items) {
        const AssessmentScore* found = nullptr;
        for (const AssessmentScore& s : scores) {
            if (s.name == item.name) {
                found = &s;
                break;
            }
        }
        if (found == nullptr) {
            return -1;
        }
        total += found->score * item.weight;
    }
    // total / 100 을 소수 둘째 자리에서 반올림 = round(total) / 100
    return std::round(total) / 100.0;
}
