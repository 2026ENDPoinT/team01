// 담당: B (현준)
#include "attendance.h"

#include <cctype>
#include <cmath>

#include "course.h"
#include "grade.h"

namespace {

constexpr int kWeeks = 16;           // 수업 주차 수 (과목 게시물 주차 1~16 과 같음)
constexpr int kShortageLimit = 5;    // 결석 환산이 이보다 크면(16주의 1/3 초과) 출석 미달

// 학번: 숫자 8자리 (Student::isValid 와 같은 규칙)
bool isEightDigits(const std::string& s) {
    if (s.size() != 8) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

bool isKnownStatus(const std::string& status) {
    return status == "출석" || status == "지각" || status == "결석" || status == "공결";
}

}  // namespace

bool Attendance::isValid() const {
    return isEightDigits(studentId) && isValidCourseCode(courseCode) && isValidSemester(semester) &&
           week >= 1 && week <= kWeeks && isKnownStatus(status);
}

AttendanceSummary summarizeAttendance(const std::vector<Attendance>& records) {
    AttendanceSummary summary;
    if (records.empty()) {
        return summary;  // 기록 없음 → score -1
    }
    for (const Attendance& record : records) {
        if (record.status == "지각") {
            ++summary.late;
        } else if (record.status == "결석") {
            ++summary.absent;
        } else {
            ++summary.present;  // 출석·공결
        }
    }
    summary.absentEquivalent = summary.absent + summary.late / 3;  // 지각 3회 = 결석 1회 (정수 나눗셈)

    // 기록하지 않은 주차는 출석으로 보므로 분모는 항상 16주
    const double raw = 100.0 * (kWeeks - summary.absentEquivalent) / kWeeks;
    summary.score = raw < 0.0 ? 0.0 : std::round(raw * 100.0) / 100.0;
    summary.shortage = summary.absentEquivalent > kShortageLimit;
    return summary;
}
