// 담당: 조장 (Eno74)
// GradeManager: 과목코드 변경 시 성적 코드도 같이 바뀜, 학생·과목 삭제 시 성적도 같이 삭제
//   평가 항목·항목 점수로 최종 성적 자동 계산·재계산·삭제, 연쇄 처리, 저장/불러오기
#include <string>

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

// ----- 평가 항목·항목 점수 → 최종 성적 자동 계산 -----

namespace {

const char* const kCode = "109001";
const char* const kSemester = "2026-2";

Assessment makeItem(const std::string& name, int weight, const std::string& code = kCode,
                    const std::string& semester = kSemester) {
    Assessment a;
    a.courseCode = code;
    a.semester = semester;
    a.name = name;
    a.weight = weight;
    return a;
}

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

std::string letterOf(const GradeManager& gm, const std::string& studentId) {
    const Grade* g = findGrade(gm, studentId, kCode, kSemester);
    return g == nullptr ? std::string("(없음)") : Retake::officialLetter(*g, gm.getGrades());
}

// 학생 3명 (24000001·24000002 는 109001 2026-2 수강신청, 24000003 은 안 함), 과목 2개,
// 109001 2026-2 항목: 중간 30, 기말 40, 과제 20, 출석 10
void fillAssessments(GradeManager& gm) {
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addStudent(makeStudent("24000002", "나")));
    CHECK(gm.addStudent(makeStudent("24000003", "다")));
    CHECK(gm.addCourse(makeCourse(kCode, "자료구조")));
    CHECK(gm.addCourse(makeCourse("109002", "운영체제")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", kCode, kSemester)));
    CHECK(gm.addEnrollment(makeEnrollment("24000002", kCode, kSemester)));
    CHECK(gm.addAssessment(makeItem("중간", 30)));
    CHECK(gm.addAssessment(makeItem("기말", 40)));
    CHECK(gm.addAssessment(makeItem("과제", 20)));
    CHECK(gm.addAssessment(makeItem("출석", 10)));
}

// 24000001: 중간 90, 기말 80, 과제 100, 출석 100 → 89 (B+)
void fillScores(GradeManager& gm) {
    CHECK(gm.setAssessmentScore(makeScore("24000001", "중간", 90)));
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말", 80)));
    CHECK(gm.setAssessmentScore(makeScore("24000001", "과제", 100)));
    CHECK(gm.setAssessmentScore(makeScore("24000001", "출석", 100)));
}

}  // namespace

TEST_CASE(assessments_complete_scores_create_grade) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);

    CHECK(gm.hasAssessments(kCode, kSemester));
    CHECK(!gm.hasAssessments(kCode, "2026-1"));
    CHECK(!gm.hasAssessments("109002", kSemester));
    CHECK_EQ(gm.getAssessments(kCode, kSemester).size(), static_cast<size_t>(4));
    CHECK_EQ(gm.getAllAssessments().size(), static_cast<size_t>(4));

    // 세 개만 넣으면 미완성 → 성적 없음
    CHECK(gm.setAssessmentScore(makeScore("24000001", "중간", 90)));
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말", 80)));
    CHECK(gm.setAssessmentScore(makeScore("24000001", "과제", 100)));
    CHECK(findGrade(gm, "24000001", kCode, kSemester) == nullptr);

    CHECK(gm.setAssessmentScore(makeScore("24000001", "출석", 100)));
    const Grade* g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 89.0);
    CHECK_EQ(letterOf(gm, "24000001"), std::string("B+"));

    // 다른 학생 점수 만점 → 100, A+. 첫 학생 성적은 그대로
    for (const char* name : {"중간", "기말", "과제", "출석"}) {
        CHECK(gm.setAssessmentScore(makeScore("24000002", name, 100)));
    }
    CHECK_EQ(letterOf(gm, "24000002"), std::string("A+"));
    CHECK_EQ(letterOf(gm, "24000001"), std::string("B+"));
    CHECK_EQ(gm.getGrades().size(), static_cast<size_t>(2));
    CHECK_EQ(gm.getAssessmentScores(kCode, kSemester).size(), static_cast<size_t>(8));
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000001").size(), static_cast<size_t>(4));
}

TEST_CASE(assessments_recalculate_on_score_and_weight_change) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    fillScores(gm);

    // 점수 수정 (같은 키는 추가가 아니라 수정): 기말 80 → 90 이면 27 + 36 + 20 + 10 = 93 (A0)
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말", 90)));
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000001").size(), static_cast<size_t>(4));
    const Grade* g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 93.0);
    CHECK_EQ(letterOf(gm, "24000001"), std::string("A0"));

    // 합계 초과 수정은 거부 (자기 비율 10 을 빼고 90 + 20 = 110)
    CHECK(!gm.updateAssessment(kCode, kSemester, "출석", makeItem("출석", 20)));

    // 기말 40 → 30: 합계 90 이라 미완성 → 성적 삭제
    CHECK(gm.updateAssessment(kCode, kSemester, "기말", makeItem("기말", 30)));
    CHECK(findGrade(gm, "24000001", kCode, kSemester) == nullptr);

    // 출석 10 → 20: 다시 100 → 27 + 27 + 20 + 20 = 94 (A0)
    CHECK(gm.updateAssessment(kCode, kSemester, "출석", makeItem("출석", 20)));
    g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 94.0);

    // 이름 변경: 항목 점수 이름도 같이 바뀌어 성적 유지
    CHECK(gm.updateAssessment(kCode, kSemester, "출석", makeItem("출석점수", 20)));
    g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 94.0);
    bool renamed = false;
    for (const AssessmentScore& s : gm.getAssessmentScoresByStudent("24000001")) {
        CHECK(s.name != "출석");
        renamed = renamed || (s.name == "출석점수" && s.score == 100);
    }
    CHECK(renamed);

    // 수정 실패: 없는 항목, 다른 항목과 이름 중복, 과목·학기 이동, 형식 오류
    CHECK(!gm.updateAssessment(kCode, kSemester, "없음", makeItem("없음", 10)));
    CHECK(!gm.updateAssessment(kCode, kSemester, "과제", makeItem("중간", 20)));
    CHECK(!gm.updateAssessment(kCode, kSemester, "과제", makeItem("과제", 20, "109002")));
    CHECK(!gm.updateAssessment(kCode, kSemester, "과제", makeItem("과제", 20, kCode, "2026-1")));
    CHECK(!gm.updateAssessment(kCode, kSemester, "과제", makeItem("과제", 0)));
    CHECK(!gm.updateAssessment(kCode, kSemester, "과제", makeItem("  ", 20)));
    g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 94.0);

    // 항목 점수 삭제 → 미완성 → 성적 삭제
    CHECK(gm.removeAssessmentScore("24000001", kCode, kSemester, "과제"));
    CHECK(findGrade(gm, "24000001", kCode, kSemester) == nullptr);
    CHECK(!gm.removeAssessmentScore("24000001", kCode, kSemester, "과제"));  // 이미 없음
}

TEST_CASE(assessments_extra_item_makes_incomplete_and_rejects_over_100) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    fillScores(gm);
    CHECK(findGrade(gm, "24000001", kCode, kSemester) != nullptr);

    // 합계 100 에 10 을 더 넣으면 110 → 거부, 성적 그대로
    CHECK(!gm.addAssessment(makeItem("퀴즈", 10)));
    CHECK_EQ(gm.getAssessments(kCode, kSemester).size(), static_cast<size_t>(4));
    CHECK(findGrade(gm, "24000001", kCode, kSemester) != nullptr);

    // 출석 10 → 5 로 줄이고 퀴즈 5 추가: 합계 100 이지만 퀴즈 점수가 없어 미완성 → 성적 삭제
    CHECK(gm.updateAssessment(kCode, kSemester, "출석", makeItem("출석", 5)));
    CHECK(gm.addAssessment(makeItem("퀴즈", 5)));
    CHECK(findGrade(gm, "24000001", kCode, kSemester) == nullptr);

    // 퀴즈 점수를 넣으면 다시 생김: 27 + 32 + 20 + 5 + 3 = 87
    CHECK(gm.setAssessmentScore(makeScore("24000001", "퀴즈", 60)));
    const Grade* g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 87.0);

    // 추가 실패: 같은 이름, 없는 과목, 형식 오류
    CHECK(!gm.addAssessment(makeItem("퀴즈", 1)));
    CHECK(!gm.addAssessment(makeItem("발표", 10, "999999")));
    CHECK(!gm.addAssessment(makeItem("발표", 101, "109002")));
    CHECK(!gm.addAssessment(makeItem("", 10, "109002")));
}

TEST_CASE(assessments_rejected_when_direct_grade_exists) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);

    // 109002 2026-2 에 직접 입력한 성적이 있음 → 첫 항목 거부
    CHECK(gm.addGrade(makeGrade("24000003", "109002", kSemester, 90)));
    CHECK(!gm.addAssessment(makeItem("중간", 50, "109002")));
    CHECK(!gm.hasAssessments("109002", kSemester));
    const Grade* direct = findGrade(gm, "24000003", "109002", kSemester);
    CHECK(direct != nullptr && direct->score == 90.0);

    // 같은 과목이라도 다른 학기는 성적이 없으므로 가능
    CHECK(gm.addAssessment(makeItem("중간", 50, "109002", "2026-1")));
    CHECK(gm.hasAssessments("109002", "2026-1"));
}

TEST_CASE(assessmentScore_requires_enrollment_item_and_valid_value) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);

    CHECK(!gm.setAssessmentScore(makeScore("24000003", "중간", 90)));  // 수강신청 없음
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "퀴즈", 90)));  // 없는 항목
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "중간", 90, kCode, "2026-1")));  // 그 학기 항목 없음
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "중간", -1)));
    CHECK(!gm.setAssessmentScore(makeScore("24000001", "중간", 100.5)));
    CHECK(!gm.setAssessmentScore(makeScore("2400000", "중간", 90)));  // 학번 형식
    CHECK(gm.getAssessmentScores(kCode, kSemester).empty());

    CHECK(gm.setAssessmentScore(makeScore("24000001", "중간", 0)));
    CHECK(gm.setAssessmentScore(makeScore("24000001", "중간", 100)));  // 수정
    CHECK_EQ(gm.getAssessmentScores(kCode, kSemester).size(), static_cast<size_t>(1));
    CHECK(!gm.removeAssessmentScore("24000003", kCode, kSemester, "중간"));
}

TEST_CASE(assessments_cascade_course_code_and_student_changes) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    fillScores(gm);

    // 과목코드 변경 → 항목·항목 점수·성적 코드가 같이 바뀜
    CHECK(gm.updateCourse(kCode, makeCourse("109009", "자료구조 (새 코드)")));
    CHECK(!gm.hasAssessments(kCode, kSemester));
    CHECK_EQ(gm.getAssessments("109009", kSemester).size(), static_cast<size_t>(4));
    CHECK(gm.getAssessmentScores(kCode, kSemester).empty());
    CHECK_EQ(gm.getAssessmentScores("109009", kSemester).size(), static_cast<size_t>(4));
    const Grade* g = findGrade(gm, "24000001", "109009", kSemester);
    CHECK(g != nullptr && g->score == 89.0);

    // 바뀐 코드로 계속 계산됨
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말", 90, "109009")));
    g = findGrade(gm, "24000001", "109009", kSemester);
    CHECK(g != nullptr && g->score == 93.0);

    // 학번 변경 → 항목 점수 학번도 바뀜
    CHECK(gm.updateStudent("24000001", makeStudent("24000009", "가")));
    CHECK(gm.getAssessmentScoresByStudent("24000001").empty());
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000009").size(), static_cast<size_t>(4));
    CHECK(gm.setAssessmentScore(makeScore("24000009", "기말", 80, "109009")));
    g = findGrade(gm, "24000009", "109009", kSemester);
    CHECK(g != nullptr && g->score == 89.0);

    // 학생 삭제 → 그 학생 항목 점수 삭제, 항목은 그대로
    CHECK(gm.setAssessmentScore(makeScore("24000002", "중간", 70, "109009")));
    CHECK(gm.removeStudent("24000009"));
    CHECK(gm.getAssessmentScoresByStudent("24000009").empty());
    CHECK_EQ(gm.getAssessmentScoresByStudent("24000002").size(), static_cast<size_t>(1));
    CHECK_EQ(gm.getAssessments("109009", kSemester).size(), static_cast<size_t>(4));

    // 과목 삭제 → 항목·항목 점수 모두 삭제
    CHECK(gm.removeCourse("109009"));
    CHECK(gm.getAllAssessments().empty());
    CHECK(gm.getAssessmentScoresByStudent("24000002").empty());
}

TEST_CASE(assessments_save_load_roundtrip) {
    TempDir dir;
    {
        GradeManager gm(dir.str());
        CHECK(gm.load());
        fillAssessments(gm);
        fillScores(gm);
        CHECK(gm.setAssessmentScore(makeScore("24000002", "중간", 55.5)));
        CHECK(gm.save());
    }

    GradeManager loaded(dir.str());
    CHECK(loaded.load());
    const std::vector<Assessment> items = loaded.getAssessments(kCode, kSemester);
    CHECK_EQ(items.size(), static_cast<size_t>(4));
    CHECK(items.size() == 4 && items[0].name == "중간" && items[0].weight == 30 && items[1].name == "기말" &&
          items[1].weight == 40 && items[3].name == "출석" && items[3].weight == 10);
    CHECK_EQ(loaded.getAssessmentScores(kCode, kSemester).size(), static_cast<size_t>(5));
    const std::vector<AssessmentScore> partial = loaded.getAssessmentScoresByStudent("24000002");
    CHECK(partial.size() == 1 && partial[0].name == "중간" && partial[0].score == 55.5);
    CHECK_EQ(loaded.getGrades().size(), static_cast<size_t>(1));
    const Grade* g = findGrade(loaded, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 89.0);
    CHECK_EQ(letterOf(loaded, "24000001"), std::string("B+"));

    // 불러온 뒤에도 계산이 이어짐
    CHECK(loaded.setAssessmentScore(makeScore("24000001", "기말", 90)));
    g = findGrade(loaded, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 93.0);
}

TEST_CASE(assessments_removing_last_item_clears_scores_and_grades) {
    TempDir dir;
    GradeManager gm(dir.str());
    fillAssessments(gm);
    fillScores(gm);
    for (const char* name : {"중간", "기말", "과제", "출석"}) {
        CHECK(gm.setAssessmentScore(makeScore("24000002", name, 100)));
    }
    CHECK_EQ(gm.getGrades().size(), static_cast<size_t>(2));

    CHECK(!gm.removeAssessment(kCode, kSemester, "없음"));

    // 하나 지우면 합계 70 → 미완성 → 성적 삭제, 그 항목 점수만 삭제
    CHECK(gm.removeAssessment(kCode, kSemester, "기말"));
    CHECK(gm.getGrades().empty());
    CHECK_EQ(gm.getAssessmentScores(kCode, kSemester).size(), static_cast<size_t>(6));

    CHECK(gm.removeAssessment(kCode, kSemester, "중간"));
    CHECK(gm.removeAssessment(kCode, kSemester, "과제"));
    CHECK(gm.hasAssessments(kCode, kSemester));
    // 출석만 남음: 비율 10 이라 계속 미완성. 마지막 항목을 getAllAssessments 의 참조로 지워도 안전해야 함
    const Assessment& last = gm.getAllAssessments().front();
    CHECK(gm.removeAssessment(last.courseCode, last.semester, last.name));
    CHECK(!gm.hasAssessments(kCode, kSemester));
    CHECK(gm.getAssessmentScores(kCode, kSemester).empty());
    CHECK(gm.getGrades().empty());

    // 직접 입력 과목으로 돌아감
    CHECK(gm.addGrade(makeGrade("24000001", kCode, kSemester, 77)));
}

TEST_CASE(assessments_last_item_removal_deletes_complete_grades) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addStudent(makeStudent("24000001", "가")));
    CHECK(gm.addCourse(makeCourse(kCode, "자료구조")));
    CHECK(gm.addEnrollment(makeEnrollment("24000001", kCode, kSemester)));
    CHECK(gm.addAssessment(makeItem("기말", 100)));  // 항목 하나로 합계 100
    CHECK(gm.setAssessmentScore(makeScore("24000001", "기말", 59.5)));
    const Grade* g = findGrade(gm, "24000001", kCode, kSemester);
    CHECK(g != nullptr && g->score == 59.5);
    CHECK_EQ(letterOf(gm, "24000001"), std::string("F"));

    // 완성된 성적이 있어도 마지막 항목을 지우면 성적·항목 점수가 모두 사라짐
    CHECK(gm.removeAssessment(kCode, kSemester, "기말"));
    CHECK(gm.getGrades().empty());
    CHECK(gm.getAssessmentScoresByStudent("24000001").empty());
}
