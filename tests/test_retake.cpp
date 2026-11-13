// 담당: 조장 (Eno74)
// 재수강 규칙 (retake.h): 삭제된 성적 "R", 재수강 A0 제한, GPA 에서 삭제된 성적 제외, 성적표 표시
#include <string>
#include <vector>

#include "check.h"
#include "grade_manager.h"
#include "report_exporter.h"
#include "retake.h"
#include "statistics.h"
#include "test_support.h"

TEST_CASE(admittedSince2018_by_id_prefix) {
    CHECK(!Retake::admittedSince2018("17101234"));
    CHECK(Retake::admittedSince2018("18101234"));
    CHECK(Retake::admittedSince2018("24101234"));
    CHECK(!Retake::admittedSince2018(""));
}

TEST_CASE(retake_old_grade_is_R_and_new_capped_at_A0) {
    const std::vector<Grade> grades = {
        makeGrade("19000001", "100101", "2025-2", 40),  // 삭제된 성적
        makeGrade("19000001", "100101", "2026-1", 97),  // 재수강 성적
        makeGrade("19000001", "100102", "2026-1", 97),  // 재수강 아님
    };
    CHECK(Retake::isReplaced(grades[0], grades));
    CHECK(!Retake::isRetake(grades[0], grades));
    CHECK(!Retake::isReplaced(grades[1], grades));
    CHECK(Retake::isRetake(grades[1], grades));

    CHECK_EQ(Retake::officialLetter(grades[0], grades), std::string("R"));
    CHECK_EQ(Retake::officialLetter(grades[1], grades), std::string("A0"));  // 97 이지만 A0 까지만
    CHECK_EQ(Retake::officialLetter(grades[2], grades), std::string("A+"));
}

// 상대평가로 정해진 등급(assignedLetter)이 점수 기준 등급보다 우선
TEST_CASE(officialLetter_uses_assignedLetter) {
    std::vector<Grade> grades = {
        makeGrade("19000001", "100101", "2025-2", 40),  // 삭제된 성적
        makeGrade("19000001", "100101", "2026-1", 97),  // 재수강 성적
        makeGrade("19000001", "100102", "2026-1", 97),  // 재수강 아님
    };
    grades[1].assignedLetter = "A+";  // 재수강 성적은 그래도 A0 까지만
    grades[2].assignedLetter = "B+";  // 점수 97 이지만 B+
    CHECK_EQ(Retake::officialLetter(grades[0], grades), std::string("R"));
    CHECK_EQ(Retake::officialLetter(grades[1], grades), std::string("A0"));
    CHECK_EQ(Retake::officialLetter(grades[2], grades), std::string("B+"));
}

TEST_CASE(gpa_uses_assignedLetter) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("19000001", "상대")));
    CHECK(gm.addCourse(makeCourse("100101", "미적분학", 3)));
    Grade g = makeGrade("19000001", "100101", "2026-1", 97);
    g.assignedLetter = "C+";
    CHECK(gm.addGrade(g));
    CHECK_NEAR(Statistics::calcGPA(gm, "19000001"), 2.5, 1e-9);  // A+ 4.5 가 아니라 C+ 2.5
}

TEST_CASE(retake_cap_applies_to_pre2018_student) {
    const std::vector<Grade> grades = {
        makeGrade("17000001", "109201", "2025-1", 30),
        makeGrade("17000001", "109201", "2025-2", 97),
    };
    CHECK_EQ(Retake::officialLetter(grades[1], grades), std::string("A0"));
}

TEST_CASE(retake_below_A_plus_keeps_letter) {
    const std::vector<Grade> grades = {
        makeGrade("19000001", "100101", "2025-1", 50),
        makeGrade("19000001", "100101", "2025-2", 85),
    };
    CHECK_EQ(Retake::officialLetter(grades[1], grades), std::string("B+"));
}

TEST_CASE(activeGrades_drops_replaced_only) {
    const std::vector<Grade> grades = {
        makeGrade("19000001", "100101", "2025-1", 10),
        makeGrade("19000001", "100101", "2025-2", 20),
        makeGrade("19000001", "100101", "2026-1", 30),
        makeGrade("19000002", "100101", "2025-1", 40),  // 다른 학생
        makeGrade("19000001", "100102", "2025-1", 50),  // 다른 과목
    };
    const std::vector<Grade> active = Retake::activeGrades(grades);
    CHECK_EQ(active.size(), static_cast<size_t>(3));
    for (const Grade& g : active) {
        CHECK(!Retake::isReplaced(g, grades));
    }
    CHECK(Retake::activeGrades({}).empty());
}

TEST_CASE(gpa_excludes_replaced_and_caps_retake) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("19000001", "재수강")));
    CHECK(gm.addCourse(makeCourse("100101", "미적분학", 3)));
    CHECK(gm.addCourse(makeCourse("100102", "물리학", 3)));
    CHECK(gm.addGrade(makeGrade("19000001", "100101", "2025-1", 50)));  // F, 삭제된 성적
    CHECK(gm.addGrade(makeGrade("19000001", "100101", "2025-2", 97)));  // 재수강 → A0 4.0
    CHECK(gm.addGrade(makeGrade("19000001", "100102", "2025-1", 85)));  // B+ 3.5

    // (4.0*3 + 3.5*3) / 6 = 3.75 (삭제된 F 가 들어가면 2.5, A+ 로 계산하면 4.0)
    CHECK_NEAR(Statistics::calcGPA(gm, "19000001"), 3.75, 1e-9);
    CHECK_NEAR(Statistics::calcGPA(gm, "19000001", "2025-2"), 4.0, 1e-9);
    // 삭제된 성적도 목록에는 그대로 남음
    CHECK_EQ(gm.getGradesByStudent("19000001").size(), static_cast<size_t>(3));
}

// 학생 성적표: 2018년 이후 입학생은 삭제된 과목을 점수 없이 "R" 로, 그 전 입학생은 줄을 뺌
TEST_CASE(studentReport_R_row_by_admission_year) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("18000001", "신입생")));
    CHECK(gm.addStudent(makeStudent("17000001", "구학번")));
    CHECK(gm.addCourse(makeCourse("100101", "미적분학", 3)));
    for (const std::string id : {"18000001", "17000001"}) {
        CHECK(gm.addGrade(makeGrade(id, "100101", "2025-1", 30)));
        CHECK(gm.addGrade(makeGrade(id, "100101", "2025-2", 97)));
    }

    const std::string since2018 = ReportExporter::studentReport(gm, "18000001");
    CHECK(since2018.find("2025-1,,R\n") != std::string::npos);
    CHECK(since2018.find("2025-2,97,A0\n") != std::string::npos);

    const std::string before2018 = ReportExporter::studentReport(gm, "17000001");
    CHECK(before2018.find("2025-1") == std::string::npos);
    CHECK(before2018.find("2025-2,97,A0\n") != std::string::npos);
}

TEST_CASE(isRetakable_c_plus_or_lower_and_np_only) {
    for (const char* l : {"C+", "C0", "D+", "D0", "F", "NP"}) CHECK(Retake::isRetakable(l));
    for (const char* l : {"A+", "A0", "B+", "B0", "P", "R"}) CHECK(!Retake::isRetakable(l));
}