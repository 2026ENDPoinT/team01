// 담당: 조장 (Eno74)
// 수강신청: 등록 검사, 학생·과목 삭제/코드 변경 연쇄 처리, 저장 → 불러오기, 신청 학기, 강의시간 겹침
#include <string>

#include "check.h"
#include "course.h"
#include "grade_manager.h"
#include "test_support.h"

namespace {

// 학생 2명, 과목 2개, 수강신청 4건 (학생마다 과목 2개씩)
void fill(GradeManager& gm) {
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addStudent(makeStudent("24000002", "나")));
    CHECK(gm.addCourse(makeCourse("100001", "과목1")));
    CHECK(gm.addCourse(makeCourse("100002", "과목2")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100001", "2026-1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000002", "100001", "2026-1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100002", "2026-1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000002", "100002", "2026-1")));
}

size_t countByStudent(const GradeManager& gm, const std::string& studentId) {
    size_t count = 0;
    for (const Enrollment& e : gm.getEnrollments()) {
        if (e.studentId == studentId) count++;
    }
    return count;
}

size_t countByCourse(const GradeManager& gm, const std::string& courseCode) {
    size_t count = 0;
    for (const Enrollment& e : gm.getEnrollments()) {
        if (e.courseCode == courseCode) count++;
    }
    return count;
}

}  // namespace

TEST_CASE(addEnrollment_rejects_unknown_duplicate_and_bad_semester) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    CHECK(!gm.addEnrollment(makeEnrollment("24000001", "100001", "2026-1")));  // 중복
    CHECK(!gm.addEnrollment(makeEnrollment("24000009", "100001", "2026-1")));  // 없는 학생
    CHECK(!gm.addEnrollment(makeEnrollment("24000001", "100009", "2026-1")));  // 없는 과목
    CHECK(!gm.addEnrollment(makeEnrollment("24000001", "100001", "2026-5")));  // 잘못된 학기
    CHECK(!gm.addEnrollment(makeEnrollment("24000001", "100001", "")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100001", "2026-2")));    // 다른 학기는 됨
    CHECK_EQ(gm.getEnrollments().size(), static_cast<size_t>(5));

    CHECK(gm.findEnrollment("24000001", "100001", "2026-2") != nullptr);
    CHECK(gm.findEnrollment("24000001", "100001", "2025-2") == nullptr);
}

TEST_CASE(removeEnrollment_only_matching) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    CHECK(gm.removeEnrollment("24000001", "100001", "2026-1"));
    CHECK(gm.findEnrollment("24000001", "100001", "2026-1") == nullptr);
    CHECK_EQ(gm.getEnrollments().size(), static_cast<size_t>(3));
    CHECK(!gm.removeEnrollment("24000001", "100001", "2026-1"));  // 이미 없음
}

TEST_CASE(removeStudent_and_removeCourse_cascade_enrollments) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    CHECK(gm.removeStudent("24000001"));
    CHECK_EQ(countByStudent(gm, "24000001"), static_cast<size_t>(0));
    CHECK_EQ(countByStudent(gm, "24000002"), static_cast<size_t>(2));

    CHECK(gm.removeCourse("100002"));
    CHECK_EQ(countByCourse(gm, "100002"), static_cast<size_t>(0));
    CHECK_EQ(gm.getEnrollments().size(), static_cast<size_t>(1));
}

TEST_CASE(updateStudent_and_updateCourse_rewrite_enrollment_codes) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    CHECK(gm.updateCourse("100001", makeCourse("100009", "과목1 (새 코드)")));
    CHECK_EQ(countByCourse(gm, "100001"), static_cast<size_t>(0));
    CHECK_EQ(countByCourse(gm, "100009"), static_cast<size_t>(2));

    CHECK(gm.updateStudent("24000001", makeStudent("24000009", "가")));
    CHECK_EQ(countByStudent(gm, "24000001"), static_cast<size_t>(0));
    CHECK_EQ(countByStudent(gm, "24000009"), static_cast<size_t>(2));
    CHECK(gm.findEnrollment("24000009", "100009", "2026-1") != nullptr);

    // 실패한 수정은 수강신청을 바꾸지 않음
    CHECK(!gm.updateCourse("100002", makeCourse("100009", "중복")));
    CHECK_EQ(countByCourse(gm, "100002"), static_cast<size_t>(2));
    CHECK_EQ(gm.getEnrollments().size(), static_cast<size_t>(4));
}

TEST_CASE(enrollment_semester_setting) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.load());
    CHECK_EQ(gm.getEnrollmentSemester(), std::string(""));  // 파일 없음 → 신청 기간 아님

    CHECK(gm.setEnrollmentSemester("2026-2"));
    CHECK_EQ(gm.getEnrollmentSemester(), std::string("2026-2"));
    CHECK(!gm.setEnrollmentSemester("2026-5"));
    CHECK(!gm.setEnrollmentSemester("2026"));
    CHECK_EQ(gm.getEnrollmentSemester(), std::string("2026-2"));  // 실패하면 그대로
    CHECK(gm.setEnrollmentSemester(""));
    CHECK_EQ(gm.getEnrollmentSemester(), std::string(""));
}

TEST_CASE(enrollments_and_semester_save_load_roundtrip) {
    TempDir dir;
    {
        GradeManager gm(dir.str());
        fill(gm);
        CHECK(gm.setEnrollmentSemester("2026-2"));
        CHECK(gm.save());
    }

    GradeManager loaded(dir.str());
    CHECK(loaded.load());
    CHECK_EQ(loaded.getEnrollments().size(), static_cast<size_t>(4));
    CHECK(loaded.findEnrollment("24000002", "100002", "2026-1") != nullptr);
    CHECK_EQ(loaded.getEnrollmentSemester(), std::string("2026-2"));

    // 신청 마감("") 도 그대로 저장·불러오기
    CHECK(loaded.setEnrollmentSemester(""));
    CHECK(loaded.save());
    GradeManager reloaded(dir.str());
    CHECK(reloaded.load());
    CHECK_EQ(reloaded.getEnrollmentSemester(), std::string(""));
    CHECK_EQ(reloaded.getEnrollments().size(), static_cast<size_t>(4));
}

TEST_CASE(schedulesOverlap_same_day_ranges) {
    CHECK(schedulesOverlap("월(1~4)", "월(3~5)"));
    CHECK(!schedulesOverlap("월(1~2)", "월(3)"));
    CHECK(schedulesOverlap("화(2~3) 목(2~3)", "목(3~4)"));
    CHECK(!schedulesOverlap("", "월(1)"));
    CHECK(!schedulesOverlap("월(1)", ""));
    CHECK(!schedulesOverlap("월(1~4)", "화(1~4)"));    // 다른 요일
    CHECK(schedulesOverlap("월(4)", "월(4)"));          // 같은 교시 하나
    CHECK(schedulesOverlap("화(1),금(5~6)", "금(6~9)"));  // 쉼표 구분
    CHECK(!schedulesOverlap("월(5~3)", "월(1~9)"));     // 잘못된 칸은 무시
}
