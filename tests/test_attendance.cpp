// 담당: B (현준)
// 출결 (#59): Attendance::isValid, summarizeAttendance 의 출석 점수·결석 환산·출석 미달
#include <string>
#include <vector>

#include "attendance.h"
#include "check.h"

namespace {

Attendance makeRecord(int week, const std::string& status) {
    Attendance a;
    a.studentId = "25103008";
    a.courseCode = "109201";
    a.semester = "2026-2";
    a.week = week;
    a.status = status;
    return a;
}

// 1주부터 차례로 status 를 count 개, 나머지 주차는 출석으로 채운 16주 기록
std::vector<Attendance> weeks(const std::string& status, int count) {
    std::vector<Attendance> records;
    for (int week = 1; week <= 16; week++) {
        records.push_back(makeRecord(week, week <= count ? status : "출석"));
    }
    return records;
}

}  // namespace

TEST_CASE(attendance_all_present_is_100) {
    const AttendanceSummary s = summarizeAttendance(weeks("출석", 16));
    CHECK_EQ(s.present, 16);
    CHECK_EQ(s.absentEquivalent, 0);
    CHECK_NEAR(s.score, 100.0, 1e-9);
    CHECK(!s.shortage);
}

TEST_CASE(attendance_absent_2_is_87_5) {
    const AttendanceSummary s = summarizeAttendance(weeks("결석", 2));
    CHECK_EQ(s.absent, 2);
    CHECK_EQ(s.present, 14);
    CHECK_EQ(s.absentEquivalent, 2);
    CHECK_NEAR(s.score, 87.5, 1e-9);
}

TEST_CASE(attendance_late_3_counts_as_one_absence) {
    const AttendanceSummary s = summarizeAttendance(weeks("지각", 3));
    CHECK_EQ(s.late, 3);
    CHECK_EQ(s.absent, 0);
    CHECK_EQ(s.absentEquivalent, 1);
    CHECK_NEAR(s.score, 93.75, 1e-9);

    // 지각 2회는 정수 나눗셈으로 결석 환산 0
    CHECK_EQ(summarizeAttendance(weeks("지각", 2)).absentEquivalent, 0);
}

TEST_CASE(attendance_shortage_boundary) {
    const AttendanceSummary five = summarizeAttendance(weeks("결석", 5));
    CHECK_NEAR(five.score, 68.75, 1e-9);
    CHECK(!five.shortage);  // 5 는 미달 아님 (5 초과부터)

    const AttendanceSummary six = summarizeAttendance(weeks("결석", 6));
    CHECK_NEAR(six.score, 62.5, 1e-9);
    CHECK(six.shortage);

    // 모두 결석 → 0 (0 미만으로 내려가지 않음)
    CHECK_NEAR(summarizeAttendance(weeks("결석", 16)).score, 0.0, 1e-9);
}

TEST_CASE(attendance_excused_same_as_present) {
    const AttendanceSummary s = summarizeAttendance(weeks("공결", 4));
    CHECK_EQ(s.present, 16);
    CHECK_EQ(s.absentEquivalent, 0);
    CHECK_NEAR(s.score, 100.0, 1e-9);
}

TEST_CASE(attendance_unrecorded_weeks_count_as_present) {
    // 결석 1·지각 3 만 기록 (나머지 12주는 기록 없음) → 결석 환산 2 → 87.5
    const std::vector<Attendance> records = {makeRecord(1, "결석"), makeRecord(2, "지각"), makeRecord(3, "지각"),
                                             makeRecord(4, "지각")};
    const AttendanceSummary s = summarizeAttendance(records);
    CHECK_EQ(s.present, 0);
    CHECK_EQ(s.absentEquivalent, 2);
    CHECK_NEAR(s.score, 87.5, 1e-9);
}

TEST_CASE(attendance_no_records_is_minus_one) {
    const AttendanceSummary s = summarizeAttendance({});
    CHECK_NEAR(s.score, -1.0, 1e-9);
    CHECK_EQ(s.present + s.late + s.absent + s.absentEquivalent, 0);
    CHECK(!s.shortage);
}

TEST_CASE(attendance_isValid) {
    CHECK(makeRecord(1, "출석").isValid());
    CHECK(makeRecord(16, "공결").isValid());
    CHECK(!makeRecord(0, "출석").isValid());
    CHECK(!makeRecord(17, "출석").isValid());
    CHECK(!makeRecord(1, "조퇴").isValid());   // 알 수 없는 상태
    CHECK(!makeRecord(1, "").isValid());

    Attendance blankId = makeRecord(1, "출석");
    blankId.studentId = "        ";  // 공백 학번
    CHECK(!blankId.isValid());
    Attendance shortId = makeRecord(1, "출석");
    shortId.studentId = "2510300";
    CHECK(!shortId.isValid());
    Attendance badCode = makeRecord(1, "출석");
    badCode.courseCode = "10920";
    CHECK(!badCode.isValid());
    Attendance badSemester = makeRecord(1, "출석");
    badSemester.semester = "2026-5";
    CHECK(!badSemester.isValid());
}
