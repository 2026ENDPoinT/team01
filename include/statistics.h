// 담당: C (원준)
// GPA, 과목별 평균/최고/최저, 학사경고 계산
// 데이터를 바꾸지 않고 계산만 하므로 클래스 대신 namespace 안의 함수로 둠
// 재수강 규칙(retake.h): 재수강으로 삭제된 성적은 모든 계산에서 뺌 → Retake::activeGrades(gm.getGrades())
//   평점은 Retake::officialLetter(grade, gm.getGrades()) 의 등급으로 (재수강 성적은 A0 까지만)
#pragma once

#include <string>
#include <vector>

#include "grade_manager.h"

namespace Statistics {

// 학생 한 명의 GPA (4.5 만점, 학점 가중 평균). semester 가 비어있으면 전체 학기
//   삭제된 성적 제외, 재수강 성적은 A0 제한 적용 (위 재수강 규칙)
// 성적이 없으면 0.0
double calcGPA(const GradeManager& gm, const std::string& studentId,
               const std::string& semester = "");

// 학기 하나의 평점 요약
struct SemesterSummary {
    std::string semester;
    double gpa = 0.0;   // 그 학기 GPA (calcGPA(gm, id, semester) 와 같음)
    int credits = 0;    // 평점 계산에 들어간 과목의 학점 합
    double majorGpa = 0.0;  // 전공(전공필수·전공선택) 과목만의 GPA. majorCredits 가 0 이면 의미 없음
    int majorCredits = 0;   // 평점 계산에 들어간 전공 과목의 학점 합
};
// 학생 한 명의 학기별·전체 평점 요약
struct GpaSummary {
    std::vector<SemesterSummary> semesters;  // 성적이 있는 학기만, 학기 오름차순
    double gpa = 0.0;                        // 전체 GPA (calcGPA(gm, id) 와 같음)
    int credits = 0;                         // 평점 계산에 들어간 전체 학점 합
    double majorGpa = 0.0;  // 전체 전공 GPA (majorCredits 가 0 이면 의미 없음, 서버는 null 로 내보냄)
    int majorCredits = 0;
};
// calcGPA 와 같은 규칙으로 계산: 삭제된 성적 제외, 재수강 성적은 A0 제한, 없는 과목의 성적 제외
// 성적이 없으면 semesters 는 빈 목록, gpa 0.0, credits 0
GpaSummary gpaSummary(const GradeManager& gm, const std::string& studentId);

// 과목 하나의 점수 통계 (삭제된 성적 제외). 성적이 없으면 count = 0, 나머지 0
//   latestSemester: 이 과목에 수강신청이 1건 이상 있는 가장 늦은 학기 (없으면 ""), latestEnrolled: 그 학기 신청 인원
struct CourseStats {
    std::string courseCode;
    int count = 0;         // 수강 인원 (성적 기준)
    double average = 0.0;
    double max = 0.0;
    double min = 0.0;
    std::string latestSemester;
    int latestEnrolled = 0;
};
CourseStats courseStats(const GradeManager& gm, const std::string& courseCode);

// 학사경고 한 건
struct WarningEntry {
    std::string semester;
    std::string reason;    // "수강신청 미이행" 또는 "평점평균 1.75 미만"
    bool hasGpa = false;   // 수강신청 미이행이면 false (평점 없음)
    double gpa = 0.0;
    int credits = 0;       // 그 학기 신청 학점 합계
};
// 학생 한 명의 학사경고 목록 (학기 오름차순)
//   판단 학기: 그 학생의 첫 수강신청 학기부터 전체 성적 중 가장 늦은 정규학기까지의 정규학기(N = 1·2)
//   - 신청 학점 합계 < 1 → 수강신청 미이행
//   - 아니고 그 학기 성적이 있으면, 원래 등급(Grade::letter, 재수강 규칙 미적용, 삭제된 성적 포함)의
//     학점 가중 평점이 1.75 미만 → 평점평균 1.75 미만
//   수강신청 기록이 없거나 정규학기 성적이 하나도 없으면 빈 목록
std::vector<WarningEntry> academicWarnings(const GradeManager& gm, const std::string& studentId);

// ----- 재수강 시뮬레이션 (저장하지 않음: gm 은 const) -----
struct RetakeItem {
    std::string courseCode;
    std::string letter;  // 받고 싶은 등급 (A+ A0 B+ B0 C+ C0 D+ D0 F)
};
struct RetakeItemResult {
    std::string courseCode;
    std::string previousLetter;  // 지금 반영된 가장 늦은 학기 성적의 등급 (Retake::officialLetter)
    std::string appliedLetter;   // 가상 성적의 등급 (Retake::officialLetter, 재수강이면 A+ → A0)
};
enum class RetakeError { None, NoCourse, NoGrade, BadLetter, Duplicate, FutureGrade };
struct RetakeSimulation {
    RetakeError error = RetakeError::None;
    std::string message;  // error 가 None 이 아닐 때 한 줄 문구
    GpaSummary before;
    GpaSummary after;
    std::vector<RetakeItemResult> items;
};
// 그 학생의 성적 복사본에 items 마다 currentSemester 의 가상 성적(letterToScore)을 넣고 (그 학기에 이미 성적이 있으면 대체),
// 기존 gpaSummary 규칙으로 전/후를 계산. 현재 학기보다 늦은 학기 성적이 있는 과목은 FutureGrade 오류
RetakeSimulation simulateRetake(const GradeManager& gm, const std::string& studentId,
                                const std::string& currentSemester,
                                const std::vector<RetakeItem>& items);

}  // namespace Statistics
