// 담당: 조장 (Eno74)
// GradeManager: 과목코드 변경 시 성적 코드도 같이 바뀜, 학생·과목 삭제 시 성적도 같이 삭제
//   평가 기준(6분류 비율·등급 방식)으로 최종 성적 자동 계산·재계산·삭제, 출석 자동, 상대평가, 연쇄 처리, 저장/불러오기
#include <string>
#include <vector>

#include "check.h"
#include "grade_manager.h"
#include "relative_grading.h"
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

// ----- 학생 수정: 이름·학과·학년이 입력값 그대로 저장되는지 (학생 수정 후 이름이 바뀐다는 보고의 회귀 테스트) -----

namespace {

Student studentOf(const std::string& id, const std::string& name, const std::string& major, int year) {
    Student s = makeStudent(id, name, year);
    s.major = major;
    return s;
}

bool sameStudentValues(const Student* s, const std::string& id, const std::string& name, const std::string& major,
                       int year) {
    return s != nullptr && s->id == id && s->name == name && s->major == major && s->year == year;
}

}  // namespace

TEST_CASE(updateStudent_keeps_each_field_as_entered) {
    TempDir dir;
    GradeManager gm(dir.str());
    fill(gm);  // 24000001 가, 24000002 나

    // 칸을 하나씩 바꿈: 바꾼 칸만 바뀌고 나머지는 그대로, 다른 학생도 그대로
    CHECK(gm.updateStudent("24000001", studentOf("24000001", "가나다", "컴퓨터공학", 1)));
    CHECK(sameStudentValues(gm.findStudent("24000001"), "24000001", "가나다", "컴퓨터공학", 1));
    CHECK(gm.updateStudent("24000001", studentOf("24000001", "가나다", "소프트웨어학", 1)));
    CHECK(sameStudentValues(gm.findStudent("24000001"), "24000001", "가나다", "소프트웨어학", 1));
    CHECK(gm.updateStudent("24000001", studentOf("24000001", "가나다", "소프트웨어학", 3)));
    CHECK(sameStudentValues(gm.findStudent("24000001"), "24000001", "가나다", "소프트웨어학", 3));
    CHECK(sameStudentValues(gm.findStudent("24000002"), "24000002", "나", "컴퓨터공학", 1));
    CHECK_EQ(gm.getStudents().size(), static_cast<size_t>(2));
}

TEST_CASE(updateStudent_id_change_keeps_values_and_survives_reload) {
    TempDir dir;
    {
        GradeManager gm(dir.str());
        CHECK(gm.load());
        fill(gm);
        // 학번과 이름·학과·학년을 같이 바꿈 → 새 학번으로 입력값 그대로, 성적도 따라감
        CHECK(gm.updateStudent("24000001", studentOf("24000009", "새이름", "전자공학", 2)));
        CHECK(gm.findStudent("24000001") == nullptr);
        CHECK(sameStudentValues(gm.findStudent("24000009"), "24000009", "새이름", "전자공학", 2));
        CHECK_EQ(gm.getGradesByStudent("24000009").size(), static_cast<size_t>(2));
        CHECK(sameStudentValues(gm.findStudent("24000002"), "24000002", "나", "컴퓨터공학", 1));
        CHECK(gm.save());
    }
    // 재시작(다시 불러오기) 후에도 그대로
    GradeManager loaded(dir.str());
    CHECK(loaded.load());
    CHECK(sameStudentValues(loaded.findStudent("24000009"), "24000009", "새이름", "전자공학", 2));
    CHECK(sameStudentValues(loaded.findStudent("24000002"), "24000002", "나", "컴퓨터공학", 1));
    CHECK(loaded.findStudent("24000001") == nullptr);
}

// ----- 평가 기준 (#66): 6분류 비율 → 최종 성적 자동 계산, 출석 자동, 등급 방식 -----

namespace {

const char* const kCode = "109001";
const char* const kSemester = "2026-2";

// assessmentCategories() 순서: 출석, 중간고사, 기말고사, 과제, 팀프로젝트, 기타
const std::vector<int> kWeights = {10, 30, 40, 20, 0, 0};

AssessmentScore makeScore(const std::string& studentId, const std::string& name, double score,
                          const std::string& code = kCode, const std::string& semester = kSemester) {
    AssessmentScore s;
    s.studentId = studentId;
    s.courseCode = code;
    s.semester = semester;
    s.name = name;
    s.score = score;
    return s;
}

// (학번, 과목, 학기) 성적. 없으면 nullptr
const Grade* findGrade(const GradeManager& gm, const std::string& studentId, const std::string& code,
                       const std::string& semester) {
    for (const Grade& g : gm.getGrades()) {
        if (g.studentId == studentId && g.courseCode == code && g.semester == semester) {
            return &g;
        }
    }
    return nullptr;
}

std::string letterOf(const GradeManager& gm, const std::string& studentId, const std::string& code = kCode) {
    const Grade* g = findGrade(gm, studentId, code, kSemester);
    return g == nullptr ? std::string("(없음)") : Retake::officialLetter(*g, gm.getGrades());
}

std::string assignedOf(const GradeManager& gm, const std::string& studentId, const std::string& code = kCode) {
    const Grade* g = findGrade(gm, studentId, code, kSemester);
    return g == nullptr ? std::string("(없음)") : g->assignedLetter;
}

// (과목, 학기) 의 name 분류 점수. 없으면 -1
double scoreOf(const std::vector<AssessmentScore>& scores, const std::string& studentId, const std::string& name) {
    for (const AssessmentScore& s : scores) {
        if (s.studentId == studentId && s.name == name) {
            return s.score;
        }
    }
    return -1;
}

// 학생 3명 (24000001·24000002 는 109001 2026-2 수강신청, 24000003 은 안 함), 과목 2개,
// 109001 2026-2 평가 기준: 출석 10, 중간고사 30, 기말고사 40, 과제 20, 팀프로젝트 0, 기타 0 (mode)
void fillAssessments(GradeManager& gm, const std::string& mode = kGradingAbsolute) {
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addStudent(makeStudent("24000002", "나")));
    CHECK(gm.addStudent(makeStudent("24000003", "다")));
    CHECK(gm.addCourse(makeCourse(kCode, "자료구조")));
    CHECK(gm.addCourse(makeCourse("109002", "운영체제")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", kCode, kSemester)));
    CHECK(gm.addEnrollment(makeEnrollment("24000002", kCode, kSemester)));
    CHECK(gm.setGradingScheme(kCode, kSemester, kWeights, mode));
}

// 24000001: 중간 90, 기말 80, 과제 100 (+ 출결 기록 없음 → 출석 100) → 89 (B+)
void fillScores(GradeManager& gm, const std::string& code = kCode) {
    CHECK(gm.setAssessmentScore(makeScore("24000001", "중간고사", 90, code)));
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말고사", 80, code)));
    CHECK(gm.setAssessmentScore(makeScore("24000001", "과제", 100, code)));
}

// 24000002: 중간 80, 기말 70, 과제 86.75 (+ 출석 100) → 10 + 24 + 28 + 17.35 = 79.35 (C+)
void fillSecondScores(GradeManager& gm, const std::string& code = kCode) {
    CHECK(gm.setAssessmentScore(makeScore("24000002", "중간고사", 80, code)));
    CHECK(gm.setAssessmentScore(makeScore("24000002", "기말고사", 70, code)));
    CHECK(gm.setAssessmentScore(makeScore("24000002", "과제", 86.75, code)));
}

}  // namespace

TEST_CASE(gradingScheme_saves_six_items_in_category_order) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);

    CHECK(gm.hasAssessments(kCode, kSemester));
    CHECK(!gm.hasAssessments(kCode, "2026-1"));
    CHECK(!gm.hasAssessments("109002", kSemester));
    const std::vector<Assessment> items = gm.getAssessments(kCode, kSemester);
    CHECK_EQ(items.size(), static_cast<size_t>(6));
    for (size_t i = 0; i < items.size() && i < 6; i++) {
        CHECK_EQ(items[i].name, assessmentCategories()[i]);
        CHECK_EQ(items[i].weight, kWeights[i]);
    }
    CHECK_EQ(gm.getAllAssessments().size(), static_cast<size_t>(6));
    CHECK_EQ(gm.getGradingMode(kCode, kSemester), std::string(kGradingAbsolute));
    CHECK_EQ(gm.getGradingMode("109002", kSemester), std::string(kGradingAbsolute));  // 기록 없음

    // 다시 저장하면 바꿈 (6개 그대로, 비율·방식만 바뀜)
    CHECK(gm.setGradingScheme(kCode, kSemester, {0, 50, 50, 0, 0, 0}, kGradingRelative));
    const std::vector<Assessment> changed = gm.getAssessments(kCode, kSemester);
    CHECK(changed.size() == 6 && changed[0].weight == 0 && changed[1].weight == 50 && changed[3].weight == 0);
    CHECK_EQ(gm.getGradingMode(kCode, kSemester), std::string(kGradingRelative));
}

TEST_CASE(gradingScheme_rejects_invalid_input) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);

    CHECK(!gm.setGradingScheme("109002", kSemester, {10, 30, 40, 20, 0, -1}, kGradingAbsolute));  // 0~100 밖
    CHECK(!gm.setGradingScheme("109002", kSemester, {10, 30, 40, 19, 0, 0}, kGradingAbsolute));  // 합계 99
    CHECK(!gm.setGradingScheme("109002", kSemester, {10, 30, 40, 20, 0}, kGradingAbsolute));     // 크기 5
    CHECK(!gm.setGradingScheme("109002", kSemester, {101, 0, 0, 0, 0, -1}, kGradingAbsolute));
    CHECK(!gm.setGradingScheme("109002", kSemester, kWeights, "rel"));                            // 모르는 방식
    CHECK(!gm.setGradingScheme("999999", kSemester, kWeights, kGradingAbsolute));                 // 없는 과목
    CHECK(!gm.setGradingScheme("109002", "2026-9", kWeights, kGradingAbsolute));                  // 학기 형식
    CHECK(!gm.hasAssessments("109002", kSemester));

    // 109002 2026-2 에 직접 입력한 성적이 있음 → 처음 만들기 거부, 성적 그대로
    CHECK(gm.addGrade(makeGrade("24000003", "109002", kSemester, 90)));
    CHECK(!gm.setGradingScheme("109002", kSemester, kWeights, kGradingAbsolute));
    CHECK(!gm.hasAssessments("109002", kSemester));
    const Grade* direct = findGrade(gm, "24000003", "109002", kSemester);
    CHECK(direct != nullptr && direct->score == 90.0);

    // 같은 과목이라도 다른 학기는 성적이 없으므로 가능
    CHECK(gm.setGradingScheme("109002", "2026-1", kWeights, kGradingAbsolute));

    // 이미 평가 기준이 있는 과목은 (항목으로 만든) 성적이 있어도 바꿀 수 있음
    fillScores(gm);
    CHECK(findGrade(gm, "24000001", kCode, kSemester) != nullptr);
    CHECK(gm.setGradingScheme(kCode, kSemester, {10, 30, 40, 20, 0, 0}, kGradingRelative));
}

TEST_CASE(attendance_category_is_computed_not_stored) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);

    // 출결 기록이 없는 수강생도 출석 100 이 있음 (수강신청 안 한 24000003 은 없음)
    std::vector<AssessmentScore> scores = gm.getAssessmentScores(kCode, kSemester);
    CHECK_EQ(scores.size(), static_cast<size_t>(2));
    CHECK_NEAR(scoreOf(scores, "24000001", "출석"), 100.0, 1e-9);
    CHECK_NEAR(scoreOf(scores, "24000002", "출석"), 100.0, 1e-9);
    CHECK_NEAR(scoreOf(scores, "24000003", "출석"), -1.0, 1e-9);

    // 나머지 3분류를 넣으면 최종 성적이 생김 (두 개만 넣으면 미완성)
    CHECK(gm.setAssessmentScore(makeScore("24000001", "중간고사", 90)));
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말고사", 80)));
    CHECK(findGrade(gm, "24000001", kCode, kSemester) == nullptr);
    CHECK(gm.setAssessmentScore(makeScore("24000001", "과제", 100)));
    const Grade* g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 89.0);
    CHECK_EQ(letterOf(gm, "24000001"), std::string("B+"));
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000001").size(), static_cast<size_t>(4));  // 3 + 출석
    CHECK_EQ(gm.getAssessmentScores(kCode, kSemester).size(), static_cast<size_t>(5));     // 3 + 출석 2

    // 출석·0% 분류는 직접 넣을 수 없음
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "출석", 50)));
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "팀프로젝트", 50)));
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "기타", 50)));
    g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 89.0);
}

TEST_CASE(attendance_zero_weight_is_not_added) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    // 출석 0%: 중간 40, 기말 40, 과제 20
    CHECK(gm.setGradingScheme(kCode, kSemester, {0, 40, 40, 20, 0, 0}, kGradingAbsolute));
    CHECK(gm.getAssessmentScores(kCode, kSemester).empty());

    fillScores(gm);  // 36 + 32 + 20 = 88
    const Grade* g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 88.0);
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000001").size(), static_cast<size_t>(3));
}

TEST_CASE(gradingScheme_zero_weight_removes_scores_and_recalculates) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    // 팀프로젝트 10%: 출석 10, 중간 30, 기말 30, 과제 20, 팀 10
    CHECK(gm.setGradingScheme(kCode, kSemester, {10, 30, 30, 20, 10, 0}, kGradingAbsolute));
    fillScores(gm);
    CHECK(findGrade(gm, "24000001", kCode, kSemester) == nullptr);  // 팀 점수가 없어 미완성
    CHECK(gm.setAssessmentScore(makeScore("24000001", "팀프로젝트", 50)));
    const Grade* g = findGrade(gm, "24000001", kCode, kSemester);  // 10 + 27 + 24 + 20 + 5 = 86
    CHECK(g != nullptr && g->score == 86.0);

    // 팀프로젝트를 0% 로 바꾸면 그 점수는 사라지고 성적이 다시 계산됨 (10 + 27 + 32 + 20 = 89)
    CHECK(gm.setGradingScheme(kCode, kSemester, kWeights, kGradingAbsolute));
    CHECK_NEAR(scoreOf(gm.getAssessmentScoresByStudent("24000001"), "24000001", "팀프로젝트"), -1.0, 1e-9);
    g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 89.0);

    // 다시 10% 로 올려도 지운 점수는 돌아오지 않음 → 미완성
    CHECK(gm.setGradingScheme(kCode, kSemester, {10, 30, 30, 20, 10, 0}, kGradingAbsolute));
    CHECK(findGrade(gm, "24000001", kCode, kSemester) == nullptr);
}

TEST_CASE(assessments_recalculate_on_score_and_attendance_change) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    fillScores(gm);

    // 점수 수정 (같은 키는 추가가 아니라 수정): 기말 80 → 90 이면 10 + 27 + 36 + 20 = 93 (A0)
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말고사", 90)));
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000001").size(), static_cast<size_t>(4));
    const Grade* g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 93.0);
    CHECK_EQ(letterOf(gm, "24000001"), std::string("A0"));

    // 출결이 바뀌면 출석 점수도 바뀌어 성적이 다시 계산됨: 결석 2 → 87.5 → 8.75 + 27 + 36 + 20 = 91.75
    Attendance absent;
    absent.studentId = "24000001";
    absent.courseCode = kCode;
    absent.semester = kSemester;
    absent.status = "결석";
    for (int week : {1, 2}) {
        absent.week = week;
        CHECK(gm.setAttendance(absent));
    }
    g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 91.75);
    CHECK_NEAR(scoreOf(gm.getAssessmentScores(kCode, kSemester), "24000001", "출석"), 87.5, 1e-9);

    // 항목 점수 삭제 → 미완성 → 성적 삭제
    CHECK(gm.removeAssessmentScore("24000001", kCode, kSemester, "과제"));
    CHECK(findGrade(gm, "24000001", kCode, kSemester) == nullptr);
    CHECK(!gm.removeAssessmentScore("24000001", kCode, kSemester, "과제"));  // 이미 없음
}

TEST_CASE(assessmentScore_requires_enrollment_item_and_valid_value) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);

    CHECK(!gm.setAssessmentScore(makeScore("24000003", "중간고사", 90)));  // 수강신청 없음
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "퀴즈", 90)));      // 6분류가 아님
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "중간고사", 90, kCode, "2026-1")));  // 그 학기 평가 기준 없음
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "중간고사", -1)));
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "중간고사", 100.5)));
    CHECK(!gm.setAssessmentScore(makeScore("2400000", "중간고사", 90)));  // 학번 형식
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000001").size(), static_cast<size_t>(1));  // 출석만

    CHECK(gm.setAssessmentScore(makeScore("24000001", "중간고사", 0)));
    CHECK(gm.setAssessmentScore(makeScore("24000001", "중간고사", 100)));  // 수정
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000001").size(), static_cast<size_t>(2));
    CHECK(!gm.removeAssessmentScore("24000003", kCode, kSemester, "중간고사"));
}

TEST_CASE(gradingMode_relative_assigns_letters) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm, kGradingRelative);

    // 89점 학생만 완성: 그 한 명으로 등급을 정함
    fillScores(gm);
    const std::string expectedFirst = RelativeGrading::assignLetters({89.0})[0];
    CHECK_EQ(assignedOf(gm, "24000001"), expectedFirst);
    CHECK(!assignedOf(gm, "24000001").empty());
    CHECK_EQ(letterOf(gm, "24000001"), assignedOf(gm, "24000001"));

    // 두 번째 학생이 79.35 로 완성: 두 명으로 다시 정함
    fillSecondScores(gm);
    const Grade* second = findGrade(gm, "24000002", kCode, kSemester);
    CHECK(second != nullptr && second->score == 79.35);
    const std::vector<std::string> expected = RelativeGrading::assignLetters({89.0, 79.35});
    CHECK_EQ(assignedOf(gm, "24000001"), expected[0]);
    CHECK_EQ(assignedOf(gm, "24000002"), expected[1]);
    CHECK_EQ(assignedOf(gm, "24000001"), std::string("B+"));
    CHECK_EQ(assignedOf(gm, "24000002"), std::string("C+"));

    // 절대평가로 바꾸면 assignedLetter 가 비고 점수 기준 등급
    CHECK(gm.setGradingScheme(kCode, kSemester, kWeights, kGradingAbsolute));
    CHECK_EQ(assignedOf(gm, "24000001"), std::string(""));
    CHECK_EQ(assignedOf(gm, "24000002"), std::string(""));
    CHECK_EQ(findGrade(gm, "24000001", kCode, kSemester)->letter(), std::string("B+"));
    CHECK_EQ(findGrade(gm, "24000002", kCode, kSemester)->letter(), std::string("C+"));
}

TEST_CASE(gradingScheme_save_load_roundtrip) {
    TempDir dir;
    {
        GradeManager gm(dir.str());
        CHECK(gm.load());
        fillAssessments(gm, kGradingRelative);
        fillScores(gm);
        fillSecondScores(gm);
        CHECK(gm.setGradingScheme("109002", kSemester, {0, 0, 100, 0, 0, 0}, kGradingAbsolute));
        CHECK(gm.save());
    }

    GradeManager loaded(dir.str());
    CHECK(loaded.load());
    const std::vector<Assessment> items = loaded.getAssessments(kCode, kSemester);
    CHECK_EQ(items.size(), static_cast<size_t>(6));
    CHECK(items.size() == 6 && items[0].name == "출석" && items[0].weight == 10 && items[2].name == "기말고사" &&
          items[2].weight == 40 && items[5].name == "기타" && items[5].weight == 0);
    CHECK_EQ(loaded.getGradingMode(kCode, kSemester), std::string(kGradingRelative));
    CHECK_EQ(loaded.getGradingMode("109002", kSemester), std::string(kGradingAbsolute));
    CHECK_EQ(loaded.getGrades().size(), static_cast<size_t>(2));
    CHECK_EQ(assignedOf(loaded, "24000001"), std::string("B+"));
    CHECK_EQ(assignedOf(loaded, "24000002"), std::string("C+"));
    // 저장된 항목 점수는 6개 (출석은 저장하지 않음)
    CHECK_EQ(loaded.getAssessmentScores(kCode, kSemester).size(), static_cast<size_t>(8));  // 6 + 출석 2

    // 불러온 뒤에도 계산이 이어짐
    CHECK(loaded.setAssessmentScore(makeScore("24000001", "기말고사", 90)));
    const Grade* g = findGrade(loaded, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 93.0);
}

TEST_CASE(gradingScheme_cascade_course_code_and_student_changes) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm, kGradingRelative);
    fillScores(gm);
    fillSecondScores(gm);

    // 과목코드 변경 → 항목·항목 점수·성적·등급 방식 코드가 같이 바뀜
    CHECK(gm.updateCourse(kCode, makeCourse("109009", "자료구조 (새 코드)")));
    CHECK(!gm.hasAssessments(kCode, kSemester));
    CHECK_EQ(gm.getAssessments("109009", kSemester).size(), static_cast<size_t>(6));
    CHECK(gm.getAssessmentScores(kCode, kSemester).empty());
    CHECK_EQ(gm.getGradingMode("109009", kSemester), std::string(kGradingRelative));
    CHECK_EQ(gm.getGradingMode(kCode, kSemester), std::string(kGradingAbsolute));
    const Grade* g = findGrade(gm, "24000001", "109009", kSemester);
    CHECK(g != nullptr && g->score == 89.0);

    // 바뀐 코드로 계속 계산됨
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말고사", 90, "109009")));
    g = findGrade(gm, "24000001", "109009", kSemester);
    CHECK(g != nullptr && g->score == 93.0);

    // 학번 변경 → 항목 점수 학번도 바뀜
    CHECK(gm.updateStudent("24000001", makeStudent("24000009", "가")));
    CHECK(gm.getAssessmentScoresByStudent("24000001").empty());
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000009").size(), static_cast<size_t>(4));

    // 학생 삭제 → 그 학생 점수·성적 삭제, 남은 학생 등급을 다시 정함
    CHECK(gm.removeStudent("24000009"));
    CHECK(gm.getAssessmentScoresByStudent("24000009").empty());
    CHECK_EQ(gm.getGrades().size(), static_cast<size_t>(1));
    const std::string expectedSecond = RelativeGrading::assignLetters({79.35})[0];
    CHECK_EQ(assignedOf(gm, "24000002", "109009"), expectedSecond);
    CHECK_EQ(gm.getAssessments("109009", kSemester).size(), static_cast<size_t>(6));

    // 과목 삭제 → 항목·항목 점수·등급 방식 모두 삭제
    CHECK(gm.removeCourse("109009"));
    CHECK(gm.getAllAssessments().empty());
    CHECK(gm.getAssessmentScoresByStudent("24000002").empty());
    CHECK(gm.addCourse(makeCourse("109009", "같은 코드로 다시")));
    CHECK_EQ(gm.getGradingMode("109009", kSemester), std::string(kGradingAbsolute));
}

TEST_CASE(gradingScheme_remove_clears_everything) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm, kGradingRelative);
    fillScores(gm);
    fillSecondScores(gm);
    CHECK_EQ(gm.getGrades().size(), static_cast<size_t>(2));

    CHECK(!gm.removeGradingScheme("109002", kSemester));  // 평가 기준 없음
    CHECK(!gm.removeGradingScheme(kCode, "2026-1"));

    CHECK(gm.removeGradingScheme(kCode, kSemester));
    CHECK(!gm.hasAssessments(kCode, kSemester));
    CHECK(gm.getAssessments(kCode, kSemester).empty());
    CHECK(gm.getAssessmentScores(kCode, kSemester).empty());
    CHECK(gm.getAssessmentScoresByStudent("24000001").empty());
    CHECK_EQ(gm.getGradingMode(kCode, kSemester), std::string(kGradingAbsolute));
    CHECK(gm.getGrades().empty());
    CHECK(!gm.removeGradingScheme(kCode, kSemester));  // 이미 없음

    // 직접 입력 과목으로 돌아감
    CHECK(gm.addGrade(makeGrade("24000001", kCode, kSemester, 77)));
}

// ----- 출결 (#59) -----
// 출석 점수는 항목 점수로 저장하지 않고 attendanceCategoryScore 가 그때그때 계산한다.

namespace {

Attendance makeAttendance(const std::string& studentId, int week, const std::string& status,
                          const std::string& code = kCode, const std::string& semester = kSemester) {
    Attendance a;
    a.studentId = studentId;
    a.courseCode = code;
    a.semester = semester;
    a.week = week;
    a.status = status;
    return a;
}

// 24000001: 결석 1 (1주), 지각 3 (2~4주) → 결석 환산 2 → 87.5
void fillAttendance(GradeManager& gm) {
    CHECK(gm.setAttendance(makeAttendance("24000001", 1, "결석")));
    CHECK(gm.setAttendance(makeAttendance("24000001", 2, "지각")));
    CHECK(gm.setAttendance(makeAttendance("24000001", 3, "지각")));
    CHECK(gm.setAttendance(makeAttendance("24000001", 4, "지각")));
}

}  // namespace

TEST_CASE(attendance_changes_category_score_not_assessment_scores) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    fillScores(gm);

    // 기록이 하나도 없으면 100 (모두 출석)
    CHECK_NEAR(gm.attendanceCategoryScore("24000001", kCode, kSemester), 100.0, 1e-9);

    fillAttendance(gm);
    CHECK_NEAR(gm.attendanceCategoryScore("24000001", kCode, kSemester), 87.5, 1e-9);
    CHECK_EQ(gm.getAttendanceByCourse(kCode, kSemester).size(), static_cast<size_t>(4));
    CHECK_EQ(gm.getAttendanceByStudent("24000001").size(), static_cast<size_t>(4));
    // 다른 학생은 기록이 없으므로 100
    CHECK_NEAR(gm.attendanceCategoryScore("24000002", kCode, kSemester), 100.0, 1e-9);

    // 저장된 항목 점수는 그대로 3개, 출석은 계산값 87.5 로 보이고 성적은 다시 계산됨 (8.75 + 27 + 32 + 20)
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000001").size(), static_cast<size_t>(4));
    CHECK_NEAR(scoreOf(gm.getAssessmentScoresByStudent("24000001"), "24000001", "출석"), 87.5, 1e-9);
    const Grade* g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 87.75);

    // 같은 주차를 다시 기록하면 추가가 아니라 수정: 1주 결석 → 공결 → 결석 환산 1 → 93.75
    CHECK(gm.setAttendance(makeAttendance("24000001", 1, "공결")));
    CHECK_EQ(gm.getAttendanceByStudent("24000001").size(), static_cast<size_t>(4));
    CHECK_NEAR(gm.attendanceCategoryScore("24000001", kCode, kSemester), 93.75, 1e-9);
}

TEST_CASE(attendance_remove_all_returns_to_100) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    fillScores(gm);
    fillAttendance(gm);

    CHECK(!gm.removeAttendance("24000001", kCode, kSemester, 5));  // 기록 없는 주차
    CHECK(!gm.removeAttendance("24000002", kCode, kSemester, 1));  // 기록 없는 학생
    for (int week = 1; week <= 4; week++) {
        CHECK(gm.removeAttendance("24000001", kCode, kSemester, week));
    }
    CHECK(gm.getAttendanceByStudent("24000001").empty());
    CHECK_NEAR(gm.attendanceCategoryScore("24000001", kCode, kSemester), 100.0, 1e-9);
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000001").size(), static_cast<size_t>(4));
    const Grade* g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 89.0);
}

TEST_CASE(attendance_requires_enrollment_and_valid_value) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);

    CHECK(!gm.setAttendance(makeAttendance("24000003", 1, "출석")));            // 수강신청 없음
    CHECK(!gm.setAttendance(makeAttendance("24000001", 1, "출석", kCode, "2026-1")));  // 그 학기 수강신청 없음
    CHECK(!gm.setAttendance(makeAttendance("29999999", 1, "출석")));            // 없는 학생
    CHECK(!gm.setAttendance(makeAttendance("24000001", 1, "출석", "109999")));  // 없는 과목
    CHECK(!gm.setAttendance(makeAttendance("24000001", 0, "출석")));            // 주차 범위 밖
    CHECK(!gm.setAttendance(makeAttendance("24000001", 17, "출석")));
    CHECK(!gm.setAttendance(makeAttendance("24000001", 1, "조퇴")));            // 알 수 없는 상태
    CHECK(gm.getAttendanceByCourse(kCode, kSemester).empty());
}

TEST_CASE(attendance_without_assessments_is_record_only) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    // 109002 2026-2 는 평가 기준이 없는 과목 (직접 입력 과목)
    CHECK(gm.addEnrollment(makeEnrollment("24000001", "109002", kSemester)));
    CHECK(gm.setAttendance(makeAttendance("24000001", 1, "결석", "109002")));

    CHECK_EQ(gm.getAttendanceByCourse("109002", kSemester).size(), static_cast<size_t>(1));
    CHECK(findGrade(gm, "24000001", "109002", kSemester) == nullptr);  // 성적이 생기지 않음
    CHECK(gm.getAssessmentScores("109002", kSemester).empty());

    // 직접 입력한 성적은 출결이 바뀌어도 그대로
    CHECK(gm.addGrade(makeGrade("24000001", "109002", kSemester, 77)));
    CHECK(gm.setAttendance(makeAttendance("24000001", 2, "결석", "109002")));
    const Grade* g = findGrade(gm, "24000001", "109002", kSemester);
    CHECK(g != nullptr && g->score == 77.0);
}

TEST_CASE(attendance_save_load_roundtrip) {
    TempDir dir;
    {
        GradeManager gm(dir.str());
        CHECK(gm.load());
        fillAssessments(gm);
        fillScores(gm);
        fillAttendance(gm);
        CHECK(gm.setAttendance(makeAttendance("24000002", 16, "공결")));
        CHECK(gm.save());
    }

    GradeManager loaded(dir.str());
    CHECK(loaded.load());
    CHECK_EQ(loaded.getAttendanceByCourse(kCode, kSemester).size(), static_cast<size_t>(5));
    const std::vector<Attendance> second = loaded.getAttendanceByStudent("24000002");
    CHECK(second.size() == 1 && second[0].week == 16 && second[0].status == "공결");
    CHECK_NEAR(loaded.attendanceCategoryScore("24000001", kCode, kSemester), 87.5, 1e-9);
    const Grade* g = findGrade(loaded, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 87.75);

    // 불러온 뒤에도 기록이 이어짐 (2주 지각 → 출석: 지각 2 → 결석 환산 1 → 93.75)
    CHECK(loaded.setAttendance(makeAttendance("24000001", 2, "출석")));
    CHECK_NEAR(loaded.attendanceCategoryScore("24000001", kCode, kSemester), 93.75, 1e-9);
}

TEST_CASE(attendance_cascade_course_code_and_student_changes) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    fillAttendance(gm);
    CHECK(gm.setAttendance(makeAttendance("24000002", 1, "결석")));

    // 과목코드 변경 → 출결 코드도 바뀌고 점수가 이어짐
    CHECK(gm.updateCourse(kCode, makeCourse("109009", "자료구조 (새 코드)")));
    CHECK(gm.getAttendanceByCourse(kCode, kSemester).empty());
    CHECK_EQ(gm.getAttendanceByCourse("109009", kSemester).size(), static_cast<size_t>(5));
    CHECK_NEAR(gm.attendanceCategoryScore("24000001", "109009", kSemester), 87.5, 1e-9);

    // 학번 변경 → 출결 학번도 바뀜
    CHECK(gm.updateStudent("24000001", makeStudent("24000009", "가")));
    CHECK(gm.getAttendanceByStudent("24000001").empty());
    CHECK_EQ(gm.getAttendanceByStudent("24000009").size(), static_cast<size_t>(4));
    CHECK_NEAR(gm.attendanceCategoryScore("24000009", "109009", kSemester), 87.5, 1e-9);

    // 학생 삭제 → 그 학생 출결만 삭제
    CHECK(gm.removeStudent("24000009"));
    CHECK(gm.getAttendanceByStudent("24000009").empty());
    CHECK_EQ(gm.getAttendanceByStudent("24000002").size(), static_cast<size_t>(1));

    // 과목 삭제 → 그 과목 출결 모두 삭제
    CHECK(gm.removeCourse("109009"));
    CHECK(gm.getAttendanceByStudent("24000002").empty());
}

// ----- Pass/Fail 과목 (현준 담당: 성적 입력 검사, 평가 기준과의 관계, 성적 방식 변경) -----

namespace {

const char* const kPfCode = "100901";

Grade pfGrade(const std::string& studentId, const std::string& letter, const std::string& code = kPfCode) {
    Grade g = makeGrade(studentId, code, kSemester, letter == kLetterPass ? kPassScore : kNonPassScore);
    g.assignedLetter = letter;
    return g;
}

// 학생 2명, P/F 과목 100901(1학점), 일반 과목 109001. 두 학생 모두 두 과목 2026-2 수강신청
void fillPassFail(GradeManager& gm) {
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addStudent(makeStudent("24000002", "나")));
    Course pf = makeCourse(kPfCode, "사회봉사", 1);
    pf.gradeType = kGradeTypePassFail;
    CHECK(gm.addCourse(pf));
    CHECK(gm.addCourse(makeCourse(kCode, "자료구조")));
    for (const char* id : {"24000001", "24000002"}) {
        CHECK(gm.addEnrollment(makeEnrollment(id, kPfCode, kSemester)));
        CHECK(gm.addEnrollment(makeEnrollment(id, kCode, kSemester)));
    }
}

}  // namespace

TEST_CASE(passFail_course_accepts_only_P_and_NP) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillPassFail(gm);

    CHECK(gm.addGrade(pfGrade("24000001", kLetterPass)));
    CHECK(gm.addGrade(pfGrade("24000002", kLetterNonPass)));
    const Grade* p = findGrade(gm, "24000001", kPfCode, kSemester);
    CHECK(p != nullptr && p->assignedLetter == "P" && p->score == 100.0);

    // P/F 과목에 점수·일반 등급은 거부, P 인데 점수가 100 이 아니면 거부
    CHECK(gm.removeGrade("24000001", kPfCode, kSemester));
    CHECK(!gm.addGrade(makeGrade("24000001", kPfCode, kSemester, 90)));
    Grade wrongScore = pfGrade("24000001", kLetterPass);
    wrongScore.score = 90;
    CHECK(!gm.addGrade(wrongScore));
    CHECK(gm.addGrade(pfGrade("24000001", kLetterPass)));

    // 수정: P → NP 가능, P/F 과목을 점수로 바꾸는 수정은 거부
    CHECK(gm.updateGrade(pfGrade("24000001", kLetterNonPass)));
    CHECK_EQ(findGrade(gm, "24000001", kPfCode, kSemester)->assignedLetter, std::string("NP"));
    CHECK(!gm.updateGrade(makeGrade("24000001", kPfCode, kSemester, 80)));

    // 일반 과목에는 P·NP 를 받지 않음
    CHECK(!gm.addGrade(pfGrade("24000001", kLetterPass, kCode)));
    CHECK(gm.addGrade(makeGrade("24000001", kCode, kSemester, 90)));
    CHECK(!gm.updateGrade(pfGrade("24000001", kLetterPass, kCode)));
}

TEST_CASE(passFail_course_has_no_grading_scheme) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillPassFail(gm);
    CHECK(!gm.setGradingScheme(kPfCode, kSemester, kWeights, kGradingAbsolute));
    CHECK(!gm.hasAssessments(kPfCode, kSemester));
    CHECK(gm.setGradingScheme(kCode, kSemester, kWeights, kGradingAbsolute));  // 일반 과목은 그대로
}

TEST_CASE(passFail_gradeType_change_only_without_grades_or_assessments) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillPassFail(gm);
    Course toLetter = *gm.findCourse(kPfCode);
    toLetter.gradeType = kGradeTypeLetter;
    Course toPf = *gm.findCourse(kCode);
    toPf.gradeType = kGradeTypePassFail;

    // 성적이 없으면 바꿀 수 있음 (되돌리기까지)
    CHECK(gm.updateCourse(kPfCode, toLetter));
    CHECK(!gm.findCourse(kPfCode)->isPassFail());
    toLetter.gradeType = kGradeTypePassFail;
    CHECK(gm.updateCourse(kPfCode, toLetter));

    // P/F 성적이 있으면 등급 과목으로 못 바꿈, 이름 등 다른 값은 바꿀 수 있음
    CHECK(gm.addGrade(pfGrade("24000001", kLetterPass)));
    Course renamed = *gm.findCourse(kPfCode);
    renamed.gradeType = kGradeTypeLetter;
    CHECK(!gm.updateCourse(kPfCode, renamed));
    CHECK(gm.findCourse(kPfCode)->isPassFail());
    renamed.gradeType = kGradeTypePassFail;
    renamed.name = "사회봉사 (새 이름)";
    CHECK(gm.updateCourse(kPfCode, renamed));
    CHECK_EQ(gm.findCourse(kPfCode)->name, std::string("사회봉사 (새 이름)"));

    // 평가 기준이 있는 일반 과목도 P/F 로 못 바꿈
    CHECK(gm.setGradingScheme(kCode, kSemester, kWeights, kGradingAbsolute));
    CHECK(!gm.updateCourse(kCode, toPf));
    CHECK(!gm.findCourse(kCode)->isPassFail());
}

TEST_CASE(passFail_letters_survive_removeStudent_and_reload) {
    TempDir dir;
    {
        GradeManager gm(dir.str());
        CHECK(gm.load());
        fillPassFail(gm);
        CHECK(gm.addGrade(pfGrade("24000001", kLetterPass)));
        CHECK(gm.addGrade(pfGrade("24000002", kLetterNonPass)));
        // 학생을 지우면 남은 학생 등급을 다시 정하는데(applyGradingMode), P/F 과목의 P·NP 는 그대로
        CHECK(gm.removeStudent("24000001"));
        CHECK_EQ(findGrade(gm, "24000002", kPfCode, kSemester)->assignedLetter, std::string("NP"));
        CHECK(gm.save());
    }
    GradeManager loaded(dir.str());
    CHECK(loaded.load());
    CHECK(loaded.findCourse(kPfCode) != nullptr && loaded.findCourse(kPfCode)->isPassFail());
    const Grade* np = findGrade(loaded, "24000002", kPfCode, kSemester);
    CHECK(np != nullptr && np->assignedLetter == "NP" && np->score == 0.0);
}
