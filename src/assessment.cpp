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

// 공백(스페이스·탭·줄바꿈)만 있거나 비어있으면 true
bool isBlank(const std::string& s) {
    for (unsigned char c : s) {
        if (!std::isspace(c)) return false;
    }
    return true;
}

}  // namespace

// 6분류 목록 (검사에 쓰는 쪽 구현: #66)
const std::vector<std::string>& assessmentCategories() {
    static const std::vector<std::string> categories = {"출석", "중간고사", "기말고사", "과제", "팀프로젝트", "기타"};
    return categories;
}

// 구현: #66 (지금도 형식은 검사함)
bool GradingMode::isValid() const {
    if (!isValidCourseCode(courseCode)) return false;
    if (!isValidSemester(semester)) return false;
    return mode == kGradingAbsolute || mode == kGradingRelative;
}

bool Assessment::isValid() const {
    if (!isValidCourseCode(courseCode)) return false;  // 과목코드: 숫자 6자리 (course.h)
    if (!isValidSemester(semester)) return false;
    if (isBlank(name) || name.size() > 60) return false;  // 60바이트 (한글은 글자당 3바이트)
    // CSV 한 줄에 저장하므로 줄바꿈이 든 이름은 받지 않음
    if (name.find('\n') != std::string::npos || name.find('\r') != std::string::npos) return false;
    if (weight < 1 || weight > 100) return false;
    return true;
}

bool AssessmentScore::isValid() const {
    if (!isEightDigits(studentId)) return false;
    if (!isValidCourseCode(courseCode)) return false;
    if (!isValidSemester(semester)) return false;
    if (name.empty()) return false;
    if (std::isnan(score) || score < 0.0 || score > 100.0) return false;  // NaN 도 거름
    return true;
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
