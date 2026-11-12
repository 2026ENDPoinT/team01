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
