// 담당: 조장 (Eno74)
// 석차: GPA 가 같으면 같은 등수, 다음 등수는 그만큼 건너뜀 (1, 2, 2, 4)
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
