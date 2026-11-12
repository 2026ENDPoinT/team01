// 담당: 조장 (Eno74)
// GradeManager: 과목코드 변경 시 성적 코드도 같이 바뀜, 학생·과목 삭제 시 성적도 같이 삭제
//   평가 항목·항목 점수 → 최종 성적 자동 계산 (#33)
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "assessment.h"
#include "check.h"
#include "grade_manager.h"
#include "retake.h"
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

// ----- 평가 항목·항목 점수 → 최종 성적 자동 계산 (#33) -----

namespace {

const char* const kCourse = "100001";
const char* const kSem = "2026-1";

Assessment makeItem(const std::string& name, int weight, const std::string& courseCode = kCourse,
                    const std::string& semester = kSem) {
    Assessment a;
    a.courseCode = courseCode;
    a.semester = semester;
    a.name = name;
    a.weight = weight;
    return a;
}

AssessmentScore makeScore(const std::string& studentId, const std::string& name, double score,
                          const std::string& courseCode = kCourse, const std::string& semester = kSem) {
    AssessmentScore s;
    s.studentId = studentId;
    s.courseCode = courseCode;
    s.semester = semester;
    s.name = name;
    s.score = score;
    return s;
}

const Grade* findGrade(const GradeManager& gm, const std::string& studentId, const std::string& courseCode = kCourse,
                       const std::string& semester = kSem) {
    for (const Grade& g : gm.getGrades()) {
        if (g.studentId == studentId && g.courseCode == courseCode && g.semester == semester) {
            return &g;
        }
    }
    return nullptr;
}

// 성적이 있고 점수가 expected 이면 true
bool gradeIs(const GradeManager& gm, const std::string& studentId, double expected,
             const std::string& courseCode = kCourse) {
    const Grade* g = findGrade(gm, studentId, courseCode);
    return g != nullptr && std::fabs(g->score - expected) < 1e-9;
}

// 학생 2명이 과목 100001 (2026-1) 수강, 항목 4개 (중간 30, 기말 40, 과제 20, 출석 10 = 100), 점수 전부 입력
//   24000001: 90·85·100·100 → 27 + 34 + 20 + 10 = 91 (A0)
//   24000002: 80·70·90·100  → 24 + 28 + 18 + 10 = 80 (B0)
void fillAssessed(GradeManager& gm) {
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addStudent(makeStudent("24000002", "나")));
    CHECK(gm.addCourse(makeCourse(kCourse, "과목1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", kCourse, kSem)));
    CHECK(gm.addEnrollment(makeEnrollment("24000002", kCourse, kSem)));

    CHECK(gm.addAssessment(makeItem("중간", 30)));
    CHECK(gm.addAssessment(makeItem("기말", 40)));
    CHECK(gm.addAssessment(makeItem("과제", 20)));
    CHECK(gm.addAssessment(makeItem("출석", 10)));

    const char* names[] = {"중간", "기말", "과제", "출석"};
    const double s1[] = {90, 85, 100, 100};
    const double s2[] = {80, 70, 90, 100};
    for (int i = 0; i < 4; i++) {
        CHECK(gm.setAssessmentScore(makeScore("24000001", names[i], s1[i])));
        CHECK(gm.setAssessmentScore(makeScore("24000002", names[i], s2[i])));
    }
}

}  // namespace

TEST_CASE(assessments_complete_scores_create_grade) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessed(gm);

    CHECK(gm.hasAssessments(kCourse, kSem));
    CHECK_EQ(gm.getAssessments(kCourse, kSem).size(), static_cast<size_t>(4));
    CHECK_EQ(gm.getAssessmentScores(kCourse, kSem).size(), static_cast<size_t>(8));
    CHECK_EQ(gm.getGrades().size(), static_cast<size_t>(2));

    const Grade* g1 = findGrade(gm, "24000001");
    const Grade* g2 = findGrade(gm, "24000002");
    CHECK(g1 != nullptr && g2 != nullptr);
    if (g1 != nullptr && g2 != nullptr) {
        CHECK_NEAR(g1->score, 91.0, 1e-9);
        CHECK_NEAR(g2->score, 80.0, 1e-9);
        CHECK_EQ(Retake::officialLetter(*g1, gm.getGrades()), std::string("A0"));
        CHECK_EQ(Retake::officialLetter(*g2, gm.getGrades()), std::string("B0"));
    }
}

TEST_CASE(assessments_partial_scores_have_no_grade) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addCourse(makeCourse(kCourse, "과목1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", kCourse, kSem)));
    CHECK(gm.addAssessment(makeItem("중간", 50)));
    CHECK(gm.addAssessment(makeItem("기말", 50)));

    CHECK(gm.setAssessmentScore(makeScore("24000001", "중간", 90)));
    CHECK(findGrade(gm, "24000001") == nullptr);  // 기말 점수 없음 → 미완성
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말", 80)));
    CHECK(gradeIs(gm, "24000001", 85));
    CHECK(gm.removeAssessmentScore("24000001", kCourse, kSem, "기말"));
    CHECK(findGrade(gm, "24000001") == nullptr);  // 점수를 지우면 다시 미완성 → 성적 삭제
    CHECK(!gm.removeAssessmentScore("24000001", kCourse, kSem, "기말"));
}

TEST_CASE(assessment_score_change_recalculates_only_that_student) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessed(gm);

    // 기말 85 → 95 : 27 + 38 + 20 + 10 = 95 (A+), 다른 학생은 그대로
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말", 95)));
    CHECK_EQ(gm.getAssessmentScores(kCourse, kSem).size(), static_cast<size_t>(8));  // 추가가 아니라 수정
    CHECK_EQ(gm.getGrades().size(), static_cast<size_t>(2));
    CHECK(gradeIs(gm, "24000001", 95));
    CHECK(gradeIs(gm, "24000002", 80));
    const Grade* g1 = findGrade(gm, "24000001");
    CHECK(g1 != nullptr && Retake::officialLetter(*g1, gm.getGrades()) == "A+");
}

TEST_CASE(assessment_weight_change_recalculates_course) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessed(gm);

    // 중간 30 → 20 : 합계 90 → 미완성, 두 학생 성적 모두 사라짐
    CHECK(gm.updateAssessment(kCourse, kSem, "중간", makeItem("중간", 20)));
    CHECK(gm.getGrades().empty());

    // 과제 20 → 30 : 합계 100 → 다시 계산
    //   24000001: 18 + 34 + 30 + 10 = 92,  24000002: 16 + 28 + 27 + 10 = 81
    CHECK(gm.updateAssessment(kCourse, kSem, "과제", makeItem("과제", 30)));
    CHECK(gradeIs(gm, "24000001", 92));
    CHECK(gradeIs(gm, "24000002", 81));

    // 합계 100 초과, 없는 항목, 다른 항목과 이름 중복, 과목·학기 변경은 거부
    CHECK(!gm.updateAssessment(kCourse, kSem, "출석", makeItem("출석", 11)));
    CHECK(!gm.updateAssessment(kCourse, kSem, "퀴즈", makeItem("퀴즈", 10)));
    CHECK(!gm.updateAssessment(kCourse, kSem, "출석", makeItem("중간", 10)));
    CHECK(!gm.updateAssessment(kCourse, kSem, "출석", makeItem("출석", 10, kCourse, "2026-2")));
    CHECK(gradeIs(gm, "24000001", 92));

    // 이름 변경 → 항목 점수 이름도 바뀌어 성적이 그대로 유지됨
    CHECK(gm.updateAssessment(kCourse, kSem, "출석", makeItem("출결", 10)));
    CHECK(gradeIs(gm, "24000001", 92));
    const std::vector<AssessmentScore> scores = gm.getAssessmentScoresByStudent("24000001");
    CHECK(std::none_of(scores.begin(), scores.end(), [](const AssessmentScore& s) { return s.name == "출석"; }));
    CHECK(std::any_of(scores.begin(), scores.end(), [](const AssessmentScore& s) { return s.name == "출결"; }));
}

TEST_CASE(adding_assessment_makes_grade_incomplete) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessed(gm);

    // 합계가 이미 100 → 항목을 더하면 100 초과라 거부, 성적은 그대로
    CHECK(!gm.addAssessment(makeItem("퀴즈", 10)));
    CHECK_EQ(gm.getAssessments(kCourse, kSem).size(), static_cast<size_t>(4));
    CHECK(gradeIs(gm, "24000001", 91));

    // 출석 10 → 5 로 줄이고 퀴즈 5 추가 → 합계 100 이지만 퀴즈 점수가 없어 미완성 → 성적 사라짐
    CHECK(gm.updateAssessment(kCourse, kSem, "출석", makeItem("출석", 5)));
    CHECK(gm.addAssessment(makeItem("퀴즈", 5)));
    CHECK(gm.getGrades().empty());

    // 퀴즈 점수를 넣으면 그 학생 성적만 다시 생김: 27 + 34 + 20 + 5 + 4 = 90
    CHECK(gm.setAssessmentScore(makeScore("24000001", "퀴즈", 80)));
    CHECK(gradeIs(gm, "24000001", 90));
    CHECK(findGrade(gm, "24000002") == nullptr);

    // 중복 이름, 없는 과목, 공백 이름, 비율 0 은 거부
    CHECK(!gm.addAssessment(makeItem("퀴즈", 1, kCourse, kSem)));
    CHECK(!gm.addAssessment(makeItem("시험", 10, "999999")));
    CHECK(!gm.addAssessment(makeItem("   ", 10, kCourse, "2026-2")));
    CHECK(!gm.addAssessment(makeItem("시험", 0, kCourse, "2026-2")));
}

TEST_CASE(removing_assessments_recalculates_and_last_clears_all) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessed(gm);

    // 출석 삭제 → 합계 90 → 미완성, 출석 점수도 삭제
    CHECK(gm.removeAssessment(kCourse, kSem, "출석"));
    CHECK(gm.getGrades().empty());
    CHECK_EQ(gm.getAssessmentScores(kCourse, kSem).size(), static_cast<size_t>(6));
    CHECK(!gm.removeAssessment(kCourse, kSem, "출석"));

    // 남은 항목을 모두 지우면 항목 점수도 모두 사라지고 직접 입력 과목으로 돌아감
    CHECK(gm.removeAssessment(kCourse, kSem, "중간"));
    CHECK(gm.removeAssessment(kCourse, kSem, "기말"));
    CHECK(gm.removeAssessment(kCourse, kSem, "과제"));
    CHECK(!gm.hasAssessments(kCourse, kSem));
    CHECK(gm.getAssessmentScores(kCourse, kSem).empty());
    CHECK(gm.getGrades().empty());
    CHECK(gm.addGrade(makeGrade("24000001", kCourse, kSem, 77)));
}

TEST_CASE(first_assessment_rejected_when_direct_grade_exists) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addCourse(makeCourse(kCourse, "과목1")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", kCourse, kSem)));
    CHECK(gm.addGrade(makeGrade("24000001", kCourse, kSem, 88)));

    CHECK(!gm.addAssessment(makeItem("중간", 50)));
    CHECK(!gm.hasAssessments(kCourse, kSem));
    CHECK(gradeIs(gm, "24000001", 88));                                  // 직접 입력 성적은 그대로
    CHECK(gm.addAssessment(makeItem("중간", 50, kCourse, "2026-2")));    // 다른 학기는 가능
}

TEST_CASE(assessment_score_requires_enrollment_and_item) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessed(gm);
    CHECK(gm.addStudent(makeStudent("24000003", "다")));  // 수강신청 없음

    CHECK(!gm.setAssessmentScore(makeScore("24000003", "중간", 90)));
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "퀴즈", 90)));                     // 없는 항목
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "중간", 101)));                    // 범위 밖
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "중간", 90, kCourse, "2026-2")));  // 그 학기 항목 없음
    CHECK(gm.getAssessmentScoresByStudent("24000003").empty());
    CHECK(findGrade(gm, "24000003") == nullptr);
}

TEST_CASE(course_code_change_and_removal_cascade_assessments) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessed(gm);

    CHECK(gm.updateCourse(kCourse, makeCourse("100009", "새 코드")));
    CHECK(!gm.hasAssessments(kCourse, kSem));
    CHECK(gm.getAssessmentScores(kCourse, kSem).empty());
    CHECK_EQ(gm.getAssessments("100009", kSem).size(), static_cast<size_t>(4));
    CHECK_EQ(gm.getAssessmentScores("100009", kSem).size(), static_cast<size_t>(8));

    // 새 코드로도 계속 계산됨: 기말 85 → 95
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말", 95, "100009")));
    CHECK(gradeIs(gm, "24000001", 95, "100009"));

    CHECK(gm.removeCourse("100009"));
    CHECK(gm.getAllAssessments().empty());
    CHECK(gm.getAssessmentScoresByStudent("24000001").empty());
    CHECK(gm.getAssessmentScoresByStudent("24000002").empty());
    CHECK(gm.getGrades().empty());
}

TEST_CASE(student_id_change_and_removal_cascade_assessment_scores) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessed(gm);

    CHECK(gm.updateStudent("24000001", makeStudent("24000011", "가")));
    CHECK(gm.getAssessmentScoresByStudent("24000001").empty());
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000011").size(), static_cast<size_t>(4));
    CHECK(gm.setAssessmentScore(makeScore("24000011", "기말", 95)));  // 새 학번으로 계속 계산됨
    CHECK(gradeIs(gm, "24000011", 95));

    CHECK(gm.removeStudent("24000002"));
    CHECK(gm.getAssessmentScoresByStudent("24000002").empty());
    CHECK_EQ(gm.getAssessmentScores(kCourse, kSem).size(), static_cast<size_t>(4));
    CHECK_EQ(gm.getAssessments(kCourse, kSem).size(), static_cast<size_t>(4));  // 항목은 그대로
}

TEST_CASE(assessments_survive_save_and_load) {
    TempDir dir;
    std::vector<Assessment> savedItems;
    std::vector<AssessmentScore> savedScores;
    std::vector<Grade> savedGrades;
    {
        GradeManager gm(dir.str());
        fillAssessed(gm);
        CHECK(gm.addCourse(makeCourse("100002", "과목2")));
        CHECK(gm.addAssessment(makeItem("발표, \"토론\"", 100, "100002")));  // 쉼표·따옴표가 든 이름
        CHECK(gm.setAssessmentScore(makeScore("24000001", "기말", 87.5)));   // 27 + 35 + 20 + 10 = 92
        CHECK(gm.save());
        savedItems = gm.getAllAssessments();
        savedScores = gm.getAssessmentScoresByStudent("24000001");
        const std::vector<AssessmentScore> other = gm.getAssessmentScoresByStudent("24000002");
        savedScores.insert(savedScores.end(), other.begin(), other.end());
        savedGrades = gm.getGrades();
    }

    GradeManager gm(dir.str());
    CHECK(gm.load());

    const std::vector<Assessment>& items = gm.getAllAssessments();
    CHECK_EQ(items.size(), savedItems.size());
    for (size_t i = 0; i < items.size() && i < savedItems.size(); i++) {
        CHECK(items[i].courseCode == savedItems[i].courseCode && items[i].semester == savedItems[i].semester &&
              items[i].name == savedItems[i].name && items[i].weight == savedItems[i].weight);
    }

    std::vector<AssessmentScore> scores = gm.getAssessmentScoresByStudent("24000001");
    const std::vector<AssessmentScore> other = gm.getAssessmentScoresByStudent("24000002");
    scores.insert(scores.end(), other.begin(), other.end());
    CHECK_EQ(scores.size(), savedScores.size());
    for (size_t i = 0; i < scores.size() && i < savedScores.size(); i++) {
        CHECK(scores[i].studentId == savedScores[i].studentId && scores[i].courseCode == savedScores[i].courseCode &&
              scores[i].semester == savedScores[i].semester && scores[i].name == savedScores[i].name &&
              scores[i].score == savedScores[i].score);
    }

    const std::vector<Grade>& grades = gm.getGrades();
    CHECK_EQ(grades.size(), savedGrades.size());
    for (size_t i = 0; i < grades.size() && i < savedGrades.size(); i++) {
        CHECK(grades[i].studentId == savedGrades[i].studentId && grades[i].courseCode == savedGrades[i].courseCode &&
              grades[i].semester == savedGrades[i].semester && grades[i].score == savedGrades[i].score);
    }
    CHECK(gradeIs(gm, "24000001", 92));
    CHECK(gradeIs(gm, "24000002", 80));

    // 불러온 뒤에도 동기화가 이어짐: 24000002 중간 80 → 100 → 30 + 28 + 18 + 10 = 86
    CHECK(gm.setAssessmentScore(makeScore("24000002", "중간", 100)));
    CHECK(gradeIs(gm, "24000002", 86));
}
