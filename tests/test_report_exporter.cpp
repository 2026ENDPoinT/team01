// 담당: 조장 (Eno74)
// 성적표 CSV: P/F 과목은 점수 칸을 비우고 등급만 P/NP, 일반 과목은 점수 그대로
#include <string>

#include "check.h"
#include "grade_manager.h"
#include "report_exporter.h"
#include "test_support.h"

namespace {

const char* const kSemester = "2025-1";

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

bool addPassFailCourse(GradeManager& gm, const std::string& code, const std::string& name) {
    Course c = makeCourse(code, name, 1);
    c.gradeType = kGradeTypePassFail;
    return gm.addCourse(c);
}

bool addPassFailGrade(GradeManager& gm, const std::string& id, const std::string& code, const std::string& letter) {
    Grade g = makeGrade(id, code, kSemester, letter == kLetterPass ? kPassScore : kNonPassScore);
    g.assignedLetter = letter;
    return gm.addGrade(g);
}

// 학생 2명, 일반 과목 100001(90점 = A0), P/F 과목 100901(P)·100902(NP)
void fill(GradeManager& gm) {
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addStudent(makeStudent("24000002", "나")));
    CHECK(gm.addCourse(makeCourse("100001", "자료구조", 3)));
    CHECK(addPassFailCourse(gm, "100901", "사회봉사"));
    CHECK(addPassFailCourse(gm, "100902", "봉사2"));
    CHECK(gm.addGrade(makeGrade("24000001", "100001", kSemester, 90)));
    CHECK(addPassFailGrade(gm, "24000001", "100901", kLetterPass));
    CHECK(addPassFailGrade(gm, "24000001", "100902", kLetterNonPass));
    CHECK(addPassFailGrade(gm, "24000002", "100901", kLetterNonPass));
}

}  // namespace

TEST_CASE(studentReport_pass_fail_rows_have_empty_score) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    const std::string csv = ReportExporter::studentReport(gm, "24000001");
    CHECK(has(csv, ",2025-1,90,A0\n"));          // 일반 과목은 점수 그대로
    CHECK(has(csv, "100901,사회봉사,"));
    CHECK(has(csv, ",2025-1,,P\n"));              // P/F: 점수 칸 비고 등급만
    CHECK(has(csv, ",2025-1,,NP\n"));
    CHECK(!has(csv, ",100,P"));
    CHECK(!has(csv, ",0,NP"));
    CHECK(!has(csv, "100.0"));
}

TEST_CASE(courseReport_pass_fail_rows_have_empty_score_and_no_summary) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    const std::string csv = ReportExporter::courseReport(gm, "100901");
    CHECK(has(csv, "24000001,가,2025-1,,P\n"));
    CHECK(has(csv, "24000002,나,2025-1,,NP\n"));
    CHECK(!has(csv, "평균"));
    CHECK(!has(csv, "최고"));
    CHECK(!has(csv, "최저"));
    CHECK(!has(csv, ",100"));
}

TEST_CASE(courseReport_letter_course_keeps_score_and_summary) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);

    const std::string csv = ReportExporter::courseReport(gm, "100001");
    CHECK(has(csv, "24000001,가,2025-1,90,A0\n"));
    CHECK(has(csv, "평균,90.00\n"));
    CHECK(has(csv, "최고,90\n"));
    CHECK(has(csv, "최저,90\n"));
}
