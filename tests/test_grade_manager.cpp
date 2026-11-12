// 담당: 조장 (Eno74)
// GradeManager: 과목코드 변경 시 성적 코드도 같이 바뀜, 학생·과목 삭제 시 성적도 같이 삭제
#include <string>

#include "check.h"
#include "grade_manager.h"
#include "test_support.h"

namespace {

// 학생 2명, 과목 2개, 성적 4건 (학생마다 과목 2개씩)
void fill(GradeManager& gm) {
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addStudent(makeStudent("24000002", "나")));
    CHECK(gm.addCourse(makeCourse("100001", "과목1")));
    CHECK(gm.addCourse(makeCourse("100002", "과목2")));
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2026-1", 90)));
    CHECK(gm.addGrade(makeGrade("24000002", "100001", "2026-1", 80)));
    CHECK(gm.addGrade(makeGrade("24000001", "100002", "2026-1", 70)));
    CHECK(gm.addGrade(makeGrade("24000002", "100002", "2026-1", 60)));
}

}  // namespace

TEST_CASE(updateCourse_code_change_rewrites_grades) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    Course renamed = makeCourse("100009", "과목1 (새 코드)");
    CHECK(gm.updateCourse("100001", renamed));

    CHECK(gm.findCourse("100001") == nullptr);
    CHECK(gm.findCourse("100009") != nullptr);
    CHECK(gm.getGradesByCourse("100001").empty());
    CHECK_EQ(gm.getGradesByCourse("100009").size(), static_cast<size_t>(2));
    CHECK_EQ(gm.getGradesByCourse("100002").size(), static_cast<size_t>(2));  // 다른 과목은 그대로
    CHECK_EQ(gm.getGrades().size(), static_cast<size_t>(4));
}

TEST_CASE(updateCourse_to_existing_code_fails_without_changes) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    CHECK(!gm.updateCourse("100001", makeCourse("100002", "중복")));
    CHECK_EQ(gm.getGradesByCourse("100001").size(), static_cast<size_t>(2));
    CHECK_EQ(gm.getGradesByCourse("100002").size(), static_cast<size_t>(2));
    const Course* unchanged = gm.findCourse("100001");
    CHECK(unchanged != nullptr && unchanged->name == "과목1");

    CHECK(!gm.updateCourse("999999", makeCourse("100003", "없는 과목")));
}

TEST_CASE(updateCourse_same_code_keeps_grades) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    CHECK(gm.updateCourse("100001", makeCourse("100001", "이름만 바꿈", 2)));
    const Course* updated = gm.findCourse("100001");
    CHECK(updated != nullptr && updated->name == "이름만 바꿈");
    CHECK_EQ(gm.getGradesByCourse("100001").size(), static_cast<size_t>(2));
}

TEST_CASE(removeStudent_cascades_grades) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    CHECK(gm.removeStudent("24000001"));
    CHECK(gm.findStudent("24000001") == nullptr);
    CHECK(gm.getGradesByStudent("24000001").empty());
    CHECK_EQ(gm.getGradesByStudent("24000002").size(), static_cast<size_t>(2));
    CHECK_EQ(gm.getGrades().size(), static_cast<size_t>(2));

    CHECK(!gm.removeStudent("24000001"));  // 이미 없음
}

TEST_CASE(removeCourse_cascades_grades) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    CHECK(gm.removeCourse("100002"));
    CHECK(gm.findCourse("100002") == nullptr);
    CHECK(gm.getGradesByCourse("100002").empty());
    CHECK_EQ(gm.getGradesByCourse("100001").size(), static_cast<size_t>(2));
    CHECK_EQ(gm.getGrades().size(), static_cast<size_t>(2));

    CHECK(!gm.removeCourse("100002"));
}

TEST_CASE(addGrade_rejects_unknown_and_duplicate) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    CHECK(!gm.addGrade(makeGrade("24000009", "100001", "2026-1", 90)));  // 없는 학생
    CHECK(!gm.addGrade(makeGrade("24000001", "100009", "2026-1", 90)));  // 없는 과목
    CHECK(!gm.addGrade(makeGrade("24000001", "100001", "2026-1", 50)));  // 같은 (학번, 과목, 학기)
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2026-2", 50)));   // 다른 학기는 재수강
    CHECK_EQ(gm.getGrades().size(), static_cast<size_t>(5));
}
