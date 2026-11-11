// 담당: A (연준)
#include "grade.h"

#include <cctype>
#include <cmath>

#include "course.h"

namespace {

// 학번: 숫자 8자리 (Student::isValid 와 같은 규칙)
bool isEightDigits(const std::string& s) {
    if (s.size() != 8) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

// 학기: "YYYY-N" (예: "2026-1", "2026-2"), N 은 1~4
//   1·2 는 정규학기, 3·4 는 계절학기 등으로 쓸 수 있게 여유를 둠
bool isValidSemester(const std::string& s) {
    if (s.size() != 6) return false;
    for (int i = 0; i < 4; i++) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    if (s[4] != '-') return false;
    return s[5] >= '1' && s[5] <= '4';
}

}  // namespace

bool Grade::isValid() const {
    if (!isEightDigits(studentId)) return false;
    if (!isValidCourseCode(courseCode)) return false;  // 과목코드: 숫자 6자리 (course.h)
    if (!isValidSemester(semester)) return false;
    if (std::isnan(score) || score < 0.0 || score > 100.0) return false;  // NaN 도 거름
    return true;
}

std::string Grade::letter() const {
    return scoreToLetter(score);
}

// 경계값은 "이상" 기준: 95 → A+, 94.99 → A0, 59.5 → F (반올림하지 않음)
std::string scoreToLetter(double score) {
    if (score >= 95.0) return "A+";
    if (score >= 90.0) return "A0";
    if (score >= 85.0) return "B+";
    if (score >= 80.0) return "B0";
    if (score >= 75.0) return "C+";
    if (score >= 70.0) return "C0";
    if (score >= 65.0) return "D+";
    if (score >= 60.0) return "D0";
    return "F";  // 60 미만, NaN 포함
}

double letterToPoint(const std::string& letter) {
    if (letter == "A+") return 4.5;
    if (letter == "A0") return 4.0;
    if (letter == "B+") return 3.5;
    if (letter == "B0") return 3.0;
    if (letter == "C+") return 2.5;
    if (letter == "C0") return 2.0;
    if (letter == "D+") return 1.5;
    if (letter == "D0") return 1.0;
    return 0.0;  // F, "R", 모르는 문자열
}
