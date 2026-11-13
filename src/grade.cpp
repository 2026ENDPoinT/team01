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

}  // namespace

// 학기: "YYYY-N" (예: "2026-1", "2026-2"), N: 1·2 정규학기, 3 여름 계절학기, 4 겨울 계절학기
bool isValidSemester(const std::string& s) {
    if (s.size() != 6) return false;
    for (int i = 0; i < 4; i++) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    if (s[4] != '-') return false;
    return s[5] >= '1' && s[5] <= '4';
}

bool isRegularSemester(const std::string& semester) {
    return isValidSemester(semester) && (semester[5] == '1' || semester[5] == '2');
}

int admissionYear(const std::string& studentId) {
    if (!isEightDigits(studentId)) return 0;
    return 2000 + (studentId[0] - '0') * 10 + (studentId[1] - '0');  // "23..." → 2023
}

bool isSemesterInRange(const std::string& studentId, const std::string& semester, int currentYear) {
    if (!isValidSemester(semester)) return false;
    const int admitted = admissionYear(studentId);
    if (admitted == 0) return false;
    const int year = std::stoi(semester.substr(0, 4));  // 형식 검사를 통과했으므로 숫자 4자리
    return year >= admitted && year <= currentYear + 1;
}

bool Grade::isValid() const {
    if (!isEightDigits(studentId)) return false;
    if (!isValidCourseCode(courseCode)) return false;  // 과목코드: 숫자 6자리 (course.h)
    if (!isValidSemester(semester)) return false;
    if (std::isnan(score) || score < 0.0 || score > 100.0) return false;  // NaN 도 거름
    // 상대평가로 정해진 등급: ""(없음) 이거나 9개 등급(A+~D0, F) 중 하나
    if (!assignedLetter.empty() && !isPassFailLetter(assignedLetter) &&
        letterToScore(assignedLetter) < 0.0) {
        return false;
    }
    return true;
}

// 상대평가로 정해진 등급(assignedLetter)이 있으면 그것, 없으면 점수 기준 등급
std::string Grade::letter() const {
    return assignedLetter.empty() ? scoreToLetter(score) : assignedLetter;
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

double letterToScore(const std::string& letter) {
    if (letter == "A+") return 95.0;
    if (letter == "A0") return 90.0;
    if (letter == "B+") return 85.0;
    if (letter == "B0") return 80.0;
    if (letter == "C+") return 75.0;
    if (letter == "C0") return 70.0;
    if (letter == "D+") return 65.0;
    if (letter == "D0") return 60.0;
    if (letter == "F") return 0.0;
    return -1.0;  // 모르는 문자열 ("A", "", "a+", " A+" 포함)
}
