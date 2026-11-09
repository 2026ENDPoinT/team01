// 담당: C (원준)
// GPA, 과목별 평균/최고/최저, 석차 계산
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

// 과목 하나의 점수 통계 (삭제된 성적 제외). 성적이 없으면 count = 0, 나머지 0
struct CourseStats {
    std::string courseCode;
    int count = 0;         // 수강 인원
    double average = 0.0;
    double max = 0.0;
    double min = 0.0;
};
CourseStats courseStats(const GradeManager& gm, const std::string& courseCode);

// 석차 한 줄
struct RankEntry {
    int rank = 0;          // 동점이면 같은 등수 (1, 2, 2, 4 ...)
    std::string studentId;
    std::string name;
    double gpa = 0.0;
};
// 전체 학생을 GPA 높은 순으로 정렬한 석차표. semester 가 비어있으면 전체 학기
std::vector<RankEntry> ranking(const GradeManager& gm, const std::string& semester = "");

}  // namespace Statistics
