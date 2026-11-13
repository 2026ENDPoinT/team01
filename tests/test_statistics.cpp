// 담당: 조장 (Eno74)
// 석차: GPA 가 같으면 같은 등수, 다음 등수는 그만큼 건너뜀 (1, 2, 2, 4)
// 과목 통계의 최근 학기 수강 인원, 학사경고 (정규학기 평점 1.75 미만·수강신청 미이행)
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "grade_manager.h"
#include "statistics.h"
#include "test_support.h"

namespace {

std::map<std::string, int> ranksById(const std::vector<Statistics::RankEntry>& entries) {
    std::map<std::string, int> ranks;
    for (const Statistics::RankEntry& e : entries) {
        ranks[e.studentId] = e.rank;
    }
    return ranks;
}

}  // namespace

TEST_CASE(ranking_ties_share_rank_and_skip_next) {
    TempDir dir;
    GradeManager gm(dir.str());
    for (const std::string id : {"24000001", "24000002", "24000003", "24000004"}) {
        CHECK(gm.addStudent(makeStudent(id, "학생" + id.substr(7))));
    }
    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    CHECK(gm.addCourse(makeCourse("100002", "과목2", 3)));

    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2026-1", 96)));  // A+ 4.5
    // 동점 4.0 을 서로 다른 방법으로: 한 과목 A0 / 두 과목 A0 + A0
    CHECK(gm.addGrade(makeGrade("24000002", "100001", "2026-1", 90)));
    CHECK(gm.addGrade(makeGrade("24000003", "100001", "2026-1", 94)));
    CHECK(gm.addGrade(makeGrade("24000003", "100002", "2026-1", 91)));
    CHECK(gm.addGrade(makeGrade("24000004", "100001", "2026-1", 80)));  // B0 3.0

    const std::vector<Statistics::RankEntry> entries = Statistics::ranking(gm);
    CHECK_EQ(entries.size(), static_cast<size_t>(4));
    std::map<std::string, int> ranks = ranksById(entries);
    CHECK_EQ(ranks["24000001"], 1);
    CHECK_EQ(ranks["24000002"], 2);
    CHECK_EQ(ranks["24000003"], 2);
    CHECK_EQ(ranks["24000004"], 4);

    // GPA 높은 순으로 정렬돼 있어야 함
    for (size_t i = 1; i < entries.size(); i++) {
        CHECK(entries[i - 1].gpa >= entries[i].gpa);
        CHECK(entries[i - 1].rank <= entries[i].rank);
    }
}

TEST_CASE(ranking_all_tied_are_rank_one) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    for (const std::string id : {"24000001", "24000002", "24000003"}) {
        CHECK(gm.addStudent(makeStudent(id, "학생")));
        CHECK(gm.addGrade(makeGrade(id, "100001", "2026-1", 88)));
    }
    for (const Statistics::RankEntry& e : Statistics::ranking(gm)) {
        CHECK_EQ(e.rank, 1);
    }
}

TEST_CASE(ranking_by_semester_and_empty) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(Statistics::ranking(gm).empty());

    CHECK(gm.addCourse(makeCourse("100001", "과목1", 3)));
    CHECK(gm.addCourse(makeCourse("100002", "과목2", 3)));
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addStudent(makeStudent("24000002", "나")));
    // 2026-1 은 둘 다 A0 (동점), 전체 학기로는 가 가 앞섬
    CHECK(gm.addGrade(makeGrade("24000001", "100001", "2026-1", 90)));
    CHECK(gm.addGrade(makeGrade("24000002", "100001", "2026-1", 90)));
    CHECK(gm.addGrade(makeGrade("24000001", "100002", "2025-2", 100)));
    CHECK(gm.addGrade(makeGrade("24000002", "100002", "2025-2", 60)));

    std::map<std::string, int> semesterRanks = ranksById(Statistics::ranking(gm, "2026-1"));
    CHECK_EQ(semesterRanks["24000001"], 1);
    CHECK_EQ(semesterRanks["24000002"], 1);

    std::map<std::string, int> totalRanks = ranksById(Statistics::ranking(gm));
    CHECK_EQ(totalRanks["24000001"], 1);
    CHECK_EQ(totalRanks["24000002"], 2);
}

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
