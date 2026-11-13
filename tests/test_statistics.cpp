// 담당: 조장 (Eno74)
// 과목 통계의 최근 학기 수강 인원, 학사경고 (정규학기 평점 1.75 미만·수강신청 미이행)
#include <string>
#include <vector>

#include "check.h"
#include "grade_manager.h"
#include "statistics.h"
#include "test_support.h"

// ----- #14 과목별 최근 학기 수강 인원 -----

TEST_CASE(courseStats_latest_enrolled_semester) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    CHECK(gm.addCourse(makeCourse("100002", "과목2", 3)));
    for (const std::string id : {"24000001", "24000002", "24000003"}) {
        CHECK(gm.addStudent(makeStudent(id, "학생")));
    }
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100001", "2025-1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000002", "100001", "2025-1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000003", "100001", "2026-1")));

    const Statistics::CourseStats stats = Statistics::courseStats(gm, "100001");
    CHECK_EQ(stats.latestSemester, std::string("2026-1"));
    CHECK_EQ(stats.latestEnrolled, 1);

    const Statistics::CourseStats empty = Statistics::courseStats(gm, "100002");  // 신청 없음
    CHECK_EQ(empty.latestSemester, std::string(""));
    CHECK_EQ(empty.latestEnrolled, 0);
}

// ----- #17 학사경고 -----

TEST_CASE(academicWarnings_low_gpa_kept_after_retake) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100001", "2025-1")));
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2025-1", 50)));  // F

    std::vector<Statistics::WarningEntry> warnings = Statistics::academicWarnings(gm, "24000001");
    CHECK_EQ(warnings.size(), static_cast<size_t>(1));
    if (warnings.size() == 1) {
        CHECK_EQ(warnings[0].semester, std::string("2025-1"));
        CHECK_EQ(warnings[0].reason, std::string("평점평균 1.75 미만"));
        CHECK(warnings[0].hasGpa);
        CHECK_NEAR(warnings[0].gpa, 0.0, 1e-9);
        CHECK_EQ(warnings[0].credits, 3);
    }

    // 다음 학기 재수강 A+ → 이전 F 는 삭제된 성적이 되지만 학사경고는 유지
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100001", "2025-2")));
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2025-2", 97)));
    warnings = Statistics::academicWarnings(gm, "24000001");
    CHECK_EQ(warnings.size(), static_cast<size_t>(1));
    if (warnings.size() == 1) {
        CHECK_EQ(warnings[0].semester, std::string("2025-1"));
    }
}

// 학사경고의 원래 등급은 Grade::letter() (상대평가 등급 포함): 점수 90 이지만 assignedLetter D0
TEST_CASE(academicWarnings_uses_assignedLetter) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100001", "2026-1")));
    Grade g = makeGrade("24000001", "100001", "2026-1", 90);
    g.assignedLetter = "D0";  // 평점 1.0
    CHECK(gm.addGrade(g));

    const std::vector<Statistics::WarningEntry> warnings = Statistics::academicWarnings(gm, "24000001");
    CHECK_EQ(warnings.size(), static_cast<size_t>(1));
    if (warnings.size() == 1) {
        CHECK_EQ(warnings[0].semester, std::string("2026-1"));
        CHECK_EQ(warnings[0].reason, std::string("평점평균 1.75 미만"));
        CHECK(warnings[0].hasGpa);
        CHECK_NEAR(warnings[0].gpa, 1.0, 1e-9);
    }
}

TEST_CASE(academicWarnings_missing_enrollment_semester) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    CHECK(gm.addCourse(makeCourse("100002", "과목2", 3)));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100001", "2025-1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100002", "2026-1")));
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2025-1", 90)));
    CHECK(gm.addGrade(makeGrade("24000001", "100002", "2026-1", 90)));  // 가장 늦은 정규학기 2026-1

    const std::vector<Statistics::WarningEntry> warnings = Statistics::academicWarnings(gm, "24000001");
    CHECK_EQ(warnings.size(), static_cast<size_t>(1));
    if (warnings.size() == 1) {
        CHECK_EQ(warnings[0].semester, std::string("2025-2"));
        CHECK_EQ(warnings[0].reason, std::string("수강신청 미이행"));
        CHECK(!warnings[0].hasGpa);
        CHECK_EQ(warnings[0].credits, 0);
    }
}

TEST_CASE(academicWarnings_exactly_175_is_not_warning) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    CHECK(gm.addCourse(makeCourse("100002", "과목2", 3)));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100001", "2026-1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100002", "2026-1")));
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2026-1", 75)));  // C+ 2.5
    CHECK(gm.addGrade(makeGrade("24000001", "100002", "2026-1", 60)));  // D0 1.0 → (2.5*3 + 1.0*3) / 6 = 1.75

    CHECK(Statistics::academicWarnings(gm, "24000001").empty());
}

TEST_CASE(academicWarnings_ignores_seasonal_semester) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    CHECK(gm.addCourse(makeCourse("100002", "과목2", 3)));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100001", "2025-1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "100002", "2025-3")));
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2025-1", 90)));
    CHECK(gm.addGrade(makeGrade("24000001", "100002", "2025-3", 50)));  // 여름 계절학기 F

    CHECK(Statistics::academicWarnings(gm, "24000001").empty());
}

TEST_CASE(academicWarnings_without_enrollment_is_empty) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2025-1", 0)));  // 성적은 있지만 수강신청 없음

    CHECK(Statistics::academicWarnings(gm, "24000001").empty());
    CHECK(Statistics::academicWarnings(gm, "99999999").empty());  // 없는 학생
}

// ----- #32 학기별 GPA 요약 -----

TEST_CASE(gpaSummary_without_grades_is_empty) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));

    const Statistics::GpaSummary summary = Statistics::gpaSummary(gm, "24000001");
    CHECK(summary.semesters.empty());
    CHECK_NEAR(summary.gpa, 0.0, 1e-9);
    CHECK_EQ(summary.credits, 0);

    const Statistics::GpaSummary unknown = Statistics::gpaSummary(gm, "99999999");  // 없는 학생
    CHECK(unknown.semesters.empty());
    CHECK_EQ(unknown.credits, 0);
}

// 재수강으로 삭제된 성적만 있던 학기(2025-2)는 요약에서 빠짐
TEST_CASE(gpaSummary_drops_replaced_semester_and_limits_retake) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("25103008", "윤지아")));
    CHECK(gm.addCourse(makeCourse("100001", "미적분학", 3)));
    CHECK(gm.addCourse(makeCourse("100002", "과목2", 2)));
    CHECK(gm.addCourse(makeCourse("100003", "과목3", 3)));
    CHECK(gm.addGrade(makeGrade("25103008", "100002", "2025-1", 87)));  // B+ 3.5, 2학점
    CHECK(gm.addGrade(makeGrade("25103008", "100003", "2025-1", 82)));  // B0 3.0, 3학점
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2025-2", 40)));  // F → 재수강으로 삭제
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2026-1", 98)));  // 재수강 A+ → A0 4.0, 3학점

    const Statistics::GpaSummary summary = Statistics::gpaSummary(gm, "25103008");
    CHECK_EQ(summary.semesters.size(), static_cast<size_t>(2));
    if (summary.semesters.size() == 2) {
        CHECK_EQ(summary.semesters[0].semester, std::string("2025-1"));
        CHECK_NEAR(summary.semesters[0].gpa, (3.5 * 2 + 3.0 * 3) / 5, 1e-9);  // 3.2
        CHECK_EQ(summary.semesters[0].credits, 5);
        CHECK_EQ(summary.semesters[1].semester, std::string("2026-1"));
        CHECK_NEAR(summary.semesters[1].gpa, 4.0, 1e-9);
        CHECK_EQ(summary.semesters[1].credits, 3);
    }
    CHECK_NEAR(summary.gpa, (3.5 * 2 + 3.0 * 3 + 4.0 * 3) / 8, 1e-9);  // 3.5
    CHECK_EQ(summary.credits, 8);
}

TEST_CASE(gpaSummary_matches_calcGPA) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("25103008", "윤지아")));
    CHECK(gm.addCourse(makeCourse("100001", "미적분학", 3)));
    CHECK(gm.addCourse(makeCourse("100002", "과목2", 2)));
    CHECK(gm.addCourse(makeCourse("100003", "과목3", 3)));
    CHECK(gm.addGrade(makeGrade("25103008", "100002", "2026-1", 87)));
    CHECK(gm.addGrade(makeGrade("25103008", "100003", "2025-2", 91)));  // 일부러 학기 순서를 섞어 추가
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2025-1", 40)));
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2026-2", 77)));  // 재수강 C+

    const Statistics::GpaSummary summary = Statistics::gpaSummary(gm, "25103008");
    CHECK_EQ(summary.semesters.size(), static_cast<size_t>(3));
    CHECK_NEAR(summary.gpa, Statistics::calcGPA(gm, "25103008"), 1e-12);
    int creditSum = 0;
    for (size_t i = 0; i < summary.semesters.size(); i++) {
        const Statistics::SemesterSummary& s = summary.semesters[i];
        CHECK_NEAR(s.gpa, Statistics::calcGPA(gm, "25103008", s.semester), 1e-12);
        creditSum += s.credits;
        if (i > 0) {
            CHECK(summary.semesters[i - 1].semester < s.semester);  // 학기 오름차순
        }
    }
    CHECK_EQ(summary.credits, creditSum);
    CHECK_EQ(summary.credits, 8);
}

// ----- 전공 GPA -----

namespace {

bool addCourseOf(GradeManager& gm, const std::string& code, const std::string& name, int credit,
                 const std::string& category) {
    Course course = makeCourse(code, name, credit);
    course.category = category;
    return gm.addCourse(course);
}

}  // namespace

// 김민준 23101001: 전공 9학점 = (3×4.5 + 3×4.5 + 3×3.5) / 9 = 4.17, 전체 3.96·14학점과 다름
TEST_CASE(gpaSummary_major_gpa_kim_minjun) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("23101001", "김민준")));
    CHECK(addCourseOf(gm, "100001", "프로그래밍기초", 3, "전공필수"));
    CHECK(addCourseOf(gm, "100002", "자료구조", 3, "전공필수"));
    CHECK(addCourseOf(gm, "100003", "알고리즘", 3, "전공선택"));
    CHECK(addCourseOf(gm, "100004", "글쓰기", 3, "교양필수"));
    CHECK(addCourseOf(gm, "100005", "음악감상", 2, "교양선택"));
    CHECK(gm.addGrade(makeGrade("23101001", "100001", "2025-1", 96)));  // A+ 4.5
    CHECK(gm.addGrade(makeGrade("23101001", "100004", "2025-1", 90)));  // A0 4.0 (교양)
    CHECK(gm.addGrade(makeGrade("23101001", "100005", "2025-1", 80)));  // B0 3.0 (교양)
    CHECK(gm.addGrade(makeGrade("23101001", "100002", "2025-2", 96)));  // A+ 4.5
    CHECK(gm.addGrade(makeGrade("23101001", "100003", "2025-2", 85)));  // B+ 3.5

    const Statistics::GpaSummary summary = Statistics::gpaSummary(gm, "23101001");
    CHECK_EQ(summary.credits, 14);
    CHECK_NEAR(summary.gpa, 55.5 / 14, 1e-9);                    // 3.96
    CHECK_EQ(summary.majorCredits, 9);
    CHECK_NEAR(summary.majorGpa, (3 * 4.5 + 3 * 4.5 + 3 * 3.5) / 9, 1e-9);  // 4.17
    CHECK_EQ(summary.semesters.size(), static_cast<size_t>(2));
    if (summary.semesters.size() == 2) {
        CHECK_EQ(summary.semesters[0].credits, 8);
        CHECK_NEAR(summary.semesters[0].gpa, 31.5 / 8, 1e-9);    // 3.94
        CHECK_EQ(summary.semesters[0].majorCredits, 3);
        CHECK_NEAR(summary.semesters[0].majorGpa, 4.5, 1e-9);
        CHECK_EQ(summary.semesters[1].majorCredits, 6);
        CHECK_NEAR(summary.semesters[1].majorGpa, (4.5 + 3.5) / 2, 1e-9);
    }
    // 전체 기준: 학기별 전공 학점 합 = 전체 전공 학점
    int majorSum = 0;
    for (const Statistics::SemesterSummary& s : summary.semesters) majorSum += s.majorCredits;
    CHECK_EQ(summary.majorCredits, majorSum);
    // 전체 GPA 계산은 그대로
    CHECK_NEAR(summary.gpa, Statistics::calcGPA(gm, "23101001"), 1e-12);
}

// 교양만 들은 학기는 majorCredits 0 (서버 응답에서는 이 학기의 majorGpa 가 null)
TEST_CASE(gpaSummary_semester_with_only_liberal_arts_has_no_major) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(addCourseOf(gm, "100001", "글쓰기", 3, "교양필수"));
    CHECK(addCourseOf(gm, "100002", "자료구조", 3, "전공필수"));
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2025-1", 96)));
    CHECK(gm.addGrade(makeGrade("24000001", "100002", "2025-2", 85)));  // B+ 3.5

    const Statistics::GpaSummary summary = Statistics::gpaSummary(gm, "24000001");
    CHECK_EQ(summary.semesters.size(), static_cast<size_t>(2));
    if (summary.semesters.size() == 2) {
        CHECK_EQ(summary.semesters[0].credits, 3);
        CHECK_EQ(summary.semesters[0].majorCredits, 0);
        CHECK_EQ(summary.semesters[1].majorCredits, 3);
        CHECK_NEAR(summary.semesters[1].majorGpa, 3.5, 1e-9);
    }
    CHECK_EQ(summary.majorCredits, 3);
    CHECK_NEAR(summary.majorGpa, 3.5, 1e-9);
}

// 성적 없는 학생(26101007): 전공 학점 0 (서버 응답에서는 majorGpa null)
TEST_CASE(gpaSummary_without_grades_has_no_major) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("26101007", "조서준")));
    const Statistics::GpaSummary summary = Statistics::gpaSummary(gm, "26101007");
    CHECK_EQ(summary.majorCredits, 0);
    CHECK_NEAR(summary.majorGpa, 0.0, 1e-9);
}

// 전공 과목을 F 로 듣고 재수강: 지워진 F 는 빠지고 재수강은 A0(4.0)까지만 반영
//   2025-1: 전공선택 B+ 3학점(3.5) + 교양선택 2학점, 2025-2: 전공필수 F(삭제), 2026-1: 같은 과목 재수강 98점(A+ → A0 4.0)
//   전공 = (3.5×3 + 4.0×3) / 6 = 3.75  (F 를 넣었다면 (3.5×3 + 0 + 4.0×3) / 9 = 2.5)
TEST_CASE(gpaSummary_major_excludes_replaced_grade_and_limits_retake) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("25103008", "윤지아")));
    CHECK(addCourseOf(gm, "100001", "미적분학", 3, "전공필수"));
    CHECK(addCourseOf(gm, "100002", "알고리즘", 3, "전공선택"));
    CHECK(addCourseOf(gm, "100003", "음악감상", 2, "교양선택"));
    CHECK(gm.addGrade(makeGrade("25103008", "100002", "2025-1", 87)));  // B+ 3.5
    CHECK(gm.addGrade(makeGrade("25103008", "100003", "2025-1", 90)));  // 교양
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2025-2", 40)));  // F → 삭제
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2026-1", 98)));  // 재수강 A+ → A0 4.0

    const Statistics::GpaSummary summary = Statistics::gpaSummary(gm, "25103008");
    CHECK_EQ(summary.semesters.size(), static_cast<size_t>(2));  // 삭제된 F 만 있던 2025-2 는 없음
    if (summary.semesters.size() == 2) {
        CHECK_EQ(summary.semesters[0].majorCredits, 3);
        CHECK_NEAR(summary.semesters[0].majorGpa, 3.5, 1e-9);
        CHECK_EQ(summary.semesters[1].majorCredits, 3);
        CHECK_NEAR(summary.semesters[1].majorGpa, 4.0, 1e-9);
    }
    CHECK_EQ(summary.majorCredits, 6);
    CHECK_NEAR(summary.majorGpa, (3.5 * 3 + 4.0 * 3) / 6, 1e-9);  // 3.75
    CHECK_EQ(summary.credits, 8);
}

// ----- 재수강 시뮬레이션 -----

// 전공필수 F 를 현재 학기(2026-1)에 A+ 로 재수강: 반영 등급 A0 (평점 4.0), 저장 안 됨
TEST_CASE(simulateRetake_a_plus_applied_as_a0) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("25103008", "윤지아")));
    CHECK(addCourseOf(gm, "100001", "미적분학", 3, "전공필수"));
    CHECK(addCourseOf(gm, "100002", "알고리즘", 3, "전공선택"));
    CHECK(addCourseOf(gm, "100003", "음악감상", 2, "교양선택"));
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2025-1", 40)));  // F
    CHECK(gm.addGrade(makeGrade("25103008", "100002", "2025-1", 87)));  // B+ 3.5
    CHECK(gm.addGrade(makeGrade("25103008", "100003", "2025-1", 82)));  // B0 3.0, 2학점

    const Statistics::GpaSummary beforeCall = Statistics::gpaSummary(gm, "25103008");
    const size_t gradeCount = gm.getGrades().size();

    const Statistics::RetakeSimulation sim =
        Statistics::simulateRetake(gm, "25103008", "2026-1", {{"100001", "A+"}});
    CHECK(sim.error == Statistics::RetakeError::None);
    CHECK_EQ(sim.items.size(), static_cast<size_t>(1));
    if (sim.items.size() == 1) {
        CHECK_EQ(sim.items[0].courseCode, std::string("100001"));
        CHECK_EQ(sim.items[0].previousLetter, std::string("F"));
        CHECK_EQ(sim.items[0].appliedLetter, std::string("A0"));
    }
    CHECK_EQ(sim.before.credits, 8);
    CHECK_NEAR(sim.before.gpa, (0.0 * 3 + 3.5 * 3 + 3.0 * 2) / 8, 1e-9);  // 2.0625
    CHECK_NEAR(sim.before.majorGpa, (0.0 * 3 + 3.5 * 3) / 6, 1e-9);      // 1.75
    CHECK_EQ(sim.after.credits, 8);
    CHECK_NEAR(sim.after.gpa, (4.0 * 3 + 3.5 * 3 + 3.0 * 2) / 8, 1e-9);  // 3.5625 (A+ 가 아니라 A0 4.0)
    CHECK_EQ(sim.after.majorCredits, 6);
    CHECK_NEAR(sim.after.majorGpa, (4.0 * 3 + 3.5 * 3) / 6, 1e-9);       // 3.75
    // 취득 학점: 전에는 F(3학점)가 빠져 5, 후에는 A0 로 바뀌어 8 (계산 학점은 둘 다 8)
    CHECK_EQ(sim.before.earnedCredits, 5);
    CHECK_EQ(sim.after.earnedCredits, 8);

    // 저장·변경 없음: 성적 수와 gpaSummary 가 그대로
    CHECK_EQ(gm.getGrades().size(), gradeCount);
    const Statistics::GpaSummary afterCall = Statistics::gpaSummary(gm, "25103008");
    CHECK_NEAR(afterCall.gpa, beforeCall.gpa, 1e-12);
    CHECK_EQ(afterCall.credits, beforeCall.credits);
    CHECK_NEAR(afterCall.majorGpa, beforeCall.majorGpa, 1e-12);
}

// 이미 현재 학기 성적이 있으면 대체 (이전 F 는 그대로 삭제된 성적)
TEST_CASE(simulateRetake_replaces_current_semester_grade) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("25103008", "윤지아")));
    CHECK(gm.addCourse(makeCourse("100001", "미적분학", 3)));
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2025-1", 40)));  // F
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2026-1", 72)));  // 재수강 C0 2.0

    const Statistics::RetakeSimulation sim =
        Statistics::simulateRetake(gm, "25103008", "2026-1", {{"100001", "B+"}});
    CHECK(sim.error == Statistics::RetakeError::None);
    CHECK_NEAR(sim.before.gpa, 2.0, 1e-9);
    CHECK_NEAR(sim.after.gpa, 3.5, 1e-9);
    CHECK_EQ(sim.items.size(), static_cast<size_t>(1));
    if (sim.items.size() == 1) {
        CHECK_EQ(sim.items[0].previousLetter, std::string("C0"));
        CHECK_EQ(sim.items[0].appliedLetter, std::string("B+"));
    }
}

TEST_CASE(simulateRetake_rejects_bad_input) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("25103008", "윤지아")));
    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    CHECK(gm.addCourse(makeCourse("100002", "과목2", 3)));
    CHECK(gm.addCourse(makeCourse("100003", "과목3", 3)));
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2025-1", 77)));
    CHECK(gm.addGrade(makeGrade("25103008", "100003", "2026-2", 77)));  // 현재 학기(2026-1)보다 늦음

    using Statistics::RetakeError;
    const auto run = [&](const std::vector<Statistics::RetakeItem>& items) {
        return Statistics::simulateRetake(gm, "25103008", "2026-1", items).error;
    };
    CHECK(run({{"100002", "A0"}}) == RetakeError::NoGrade);     // 성적 없는 과목
    CHECK(run({{"999999", "A0"}}) == RetakeError::NoCourse);    // 없는 과목
    CHECK(run({{"100001", "S"}}) == RetakeError::BadLetter);    // 9개 등급이 아님
    CHECK(run({{"100001", "R"}}) == RetakeError::BadLetter);
    CHECK(run({{"100001", "A0"}, {"100001", "B0"}}) == RetakeError::Duplicate);
    CHECK(run({{"100003", "A0"}}) == RetakeError::FutureGrade);
    CHECK(run({{"100001", "A0"}}) == RetakeError::None);
}

// P/F 과목은 등급 시뮬레이션 대상이 아님 (평점에 들어가지 않으므로 등급을 고를 수 없음)
TEST_CASE(simulateRetake_rejects_pass_fail_course) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("25103008", "윤지아")));
    Course pf = makeCourse("100001", "P/F 과목", 1);
    pf.gradeType = kGradeTypePassFail;
    CHECK(gm.addCourse(pf));
    Grade g = makeGrade("25103008", "100001", "2025-1", kPassScore);
    g.assignedLetter = kLetterPass;
    CHECK(gm.addGrade(g));

    const Statistics::RetakeSimulation sim =
        Statistics::simulateRetake(gm, "25103008", "2026-1", {{"100001", "A0"}});
    CHECK(sim.error == Statistics::RetakeError::PassFail);
}
// ----- 계산 학점 / 취득 학점 (F·P/F·재수강으로 지워진 성적) -----

namespace {

// P/F 과목 등록 + 그 과목 성적(P = 100 / NP = 0)
bool addPassFail(GradeManager& gm, const std::string& code, int credit) {
    Course c = makeCourse(code, "P/F 과목", credit);
    c.gradeType = kGradeTypePassFail;
    return gm.addCourse(c);
}

bool addPassFailGrade(GradeManager& gm, const std::string& id, const std::string& code, const std::string& sem,
                      const std::string& letter) {
    Grade g = makeGrade(id, code, sem, letter == kLetterPass ? kPassScore : kNonPassScore);
    g.assignedLetter = letter;
    return gm.addGrade(g);
}

}  // namespace

// 이슈 완료 기준: P/F 과목 P 1개 + 일반 과목 A0 3학점 → 누적 GPA 4.0, 계산 학점 3, 취득 학점은 P/F 학점 포함
TEST_CASE(gpaSummary_pass_fail_P_counts_only_in_earned) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addCourse(makeCourse("100001", "일반", 3)));
    CHECK(addPassFail(gm, "100901", 1));
    CHECK(addPassFail(gm, "100902", 2));
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2026-1", 90)));            // A0 3학점
    CHECK(addPassFailGrade(gm, "24000001", "100901", "2026-1", kLetterPass));     // P 1학점
    CHECK(addPassFailGrade(gm, "24000001", "100902", "2026-1", kLetterNonPass));  // NP 2학점 → 영향 없음

    const Statistics::GpaSummary s = Statistics::gpaSummary(gm, "24000001");
    CHECK_NEAR(s.gpa, 4.0, 1e-9);
    CHECK_EQ(s.credits, 3);
    CHECK_EQ(s.earnedCredits, 4);  // 3 + P 1, NP 0
    CHECK(s.semesters.size() == 1 && s.semesters[0].credits == 3 && s.semesters[0].earnedCredits == 4);
}

// F·P/F(P·NP)·재수강으로 지워진 성적이 섞인 경우 (학기별·누적 모두)
//   2025-1: 미적분 F 3 + 자료 B+ 2 + 봉사 P 1        → 계산 5, 취득 2 + 1 = 3
//   2025-2: 알고리즘 F 3 (2026-1 재수강으로 지워짐) + 봉사2 NP 1 → 계산 0, 취득 0 (학기는 목록에 남음)
//   2026-1: 알고리즘 재수강 A0 3                      → 계산 3, 취득 3
//   누적: 계산 8, 취득 6 (F 3 학점만큼 계산 학점보다 작음)
TEST_CASE(gpaSummary_earned_credits_with_f_pass_fail_and_retake) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("25103008", "윤지아")));
    CHECK(gm.addCourse(makeCourse("100001", "미적분", 3)));
    CHECK(gm.addCourse(makeCourse("100002", "자료", 2)));
    CHECK(gm.addCourse(makeCourse("100003", "알고리즘", 3)));
    CHECK(addPassFail(gm, "100901", 1));
    CHECK(addPassFail(gm, "100902", 1));
    CHECK(gm.addGrade(makeGrade("25103008", "100001", "2025-1", 40)));  // F
    CHECK(gm.addGrade(makeGrade("25103008", "100002", "2025-1", 87)));  // B+
    CHECK(addPassFailGrade(gm, "25103008", "100901", "2025-1", kLetterPass));
    CHECK(gm.addGrade(makeGrade("25103008", "100003", "2025-2", 50)));  // F → 재수강으로 지워짐
    CHECK(addPassFailGrade(gm, "25103008", "100902", "2025-2", kLetterNonPass));
    CHECK(gm.addGrade(makeGrade("25103008", "100003", "2026-1", 92)));  // A0

    const Statistics::GpaSummary s = Statistics::gpaSummary(gm, "25103008");
    CHECK_EQ(s.semesters.size(), static_cast<size_t>(3));
    if (s.semesters.size() == 3) {
        CHECK(s.semesters[0].semester == "2025-1" && s.semesters[0].credits == 5 && s.semesters[0].earnedCredits == 3);
        CHECK(s.semesters[1].semester == "2025-2" && s.semesters[1].credits == 0 && s.semesters[1].earnedCredits == 0);
        CHECK(s.semesters[2].semester == "2026-1" && s.semesters[2].credits == 3 && s.semesters[2].earnedCredits == 3);
    }
    CHECK_EQ(s.credits, 8);
    CHECK_EQ(s.earnedCredits, 6);
    CHECK_NEAR(s.gpa, (0.0 * 3 + 3.5 * 2 + 4.0 * 3) / 8, 1e-9);  // 2.375
}
