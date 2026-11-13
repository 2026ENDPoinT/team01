// 담당: A (연준)
#include "assessment.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "course.h"
#include "grade.h"

namespace {

// 학번: 숫자 8자리 (Student::isValid 와 같은 규칙)
bool isEightDigits(const std::string& s) {
    if (s.size() != 8) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

// 6분류 중 하나면 true
bool isCategory(const std::string& name) {
    const std::vector<std::string>& categories = assessmentCategories();
    return std::find(categories.begin(), categories.end(), name) != categories.end();
}

}  // namespace

// 6분류 목록 (화면·API·CSV 에 이 순서와 이 글자 그대로)
const std::vector<std::string>& assessmentCategories() {
    static const std::vector<std::string> categories = {"출석", "중간고사", "기말고사", "과제", "팀프로젝트", "기타"};
    return categories;
}

bool GradingMode::isValid() const {
    if (!isValidCourseCode(courseCode)) return false;
    if (!isValidSemester(semester)) return false;
    return mode == kGradingAbsolute || mode == kGradingRelative;
}

bool Assessment::isValid() const {
    if (!isValidCourseCode(courseCode)) return false;  // 과목코드: 숫자 6자리 (course.h)
    if (!isValidSemester(semester)) return false;
    if (!isCategory(name)) return false;               // 6분류 중 하나
    if (weight < 0 || weight > 100) return false;       // 0 = 이 학기에 반영 안 함
    return true;
}

bool AssessmentScore::isValid() const {
    if (!isEightDigits(studentId)) return false;
    if (!isValidCourseCode(courseCode)) return false;
    if (!isValidSemester(semester)) return false;
    if (!isCategory(name)) return false;
    if (std::isnan(score) || score < 0.0 || score > 100.0) return false;  // NaN 도 거름
    return true;
}

// 비율 합계가 100 이 아니거나 items 가 비어 있으면 -1.
// 비율이 0 인 항목은 무시하고, 비율이 0 보다 큰 항목 중 점수가 없는 것이 있으면 -1 (미완성).
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
        if (item.weight == 0) {
            continue;  // 이 학기에 반영 안 하는 분류
        }
        auto it = std::find_if(scores.begin(), scores.end(), [&](const AssessmentScore& score) {
            return score.name == item.name;
        });
        if (it == scores.end()) {
            return -1;  // 점수가 없는 항목 → 미완성
        }
        total += it->score * item.weight;
    }
    const double x = total / 100;
    return std::round(x * 100) / 100;  // 소수 둘째 자리 반올림
}
