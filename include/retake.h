// 담당: 조장 (Eno74)
// 재수강 규칙
//   - 같은 학생이 같은 과목을 여러 학기에 들었으면 가장 늦은 학기가 재수강 성적, 그 전 성적은 모두 "삭제된 성적"
//   - 재수강 성적은 A0 까지만 (점수가 95 이상이어도 등급은 A0, 평점 4.0)
//   - 삭제된 성적은 GPA·과목 통계·과목 성적표에서 빠짐
//   - 학생 성적표: 2018년 이후 입학생은 삭제된 과목을 등급 "R" 로 남기고, 그 전 입학생은 줄 자체를 뺌
//   - 학사경고: 이미 내려진 학사경고는 유지. 학사경고를 판단할 때는 이 규칙을 적용하지 않고 원래 등급(Grade::letter)을 씀
//     (학사경고 판단은 statistics.h 의 Statistics::academicWarnings, 재수강 규칙 미적용. 기준은 README 참고)
//   - 삭제된 성적도 grades.csv 에는 그대로 남음 (성적표의 "R" 표시와 학사경고 판단에 필요)
#pragma once

#include <string>
#include <vector>

#include "grade.h"

namespace Retake {

// 학번 앞 2자리로 본 입학년도가 2018년 이후인지 (예: "17101234" → false, "18101234" → true)
bool admittedSince2018(const std::string& studentId);

// grades 안에 같은 학생·과목의 더 늦은 학기 성적이 있으면 true (= 이 성적은 삭제된 성적)
bool isReplaced(const Grade& grade, const std::vector<Grade>& grades);

// grades 안에 같은 학생·과목의 더 이른 학기 성적이 있으면 true (= 이 성적은 재수강 성적)
bool isRetake(const Grade& grade, const std::vector<Grade>& grades);

// 화면·성적표에 보여줄 등급: 삭제된 성적 "R", 재수강 성적은 A+ → A0, 나머지는 Grade::letter() (상대평가 등급이 있으면 그것, 없으면 scoreToLetter(score))
//   grades 에는 최소한 그 학생의 그 과목 성적이 모두 들어 있어야 함 (gm.getGrades() 를 넘기면 됨)
std::string officialLetter(const Grade& grade, const std::vector<Grade>& grades);

// 삭제된 성적을 뺀 목록 (GPA·과목 통계는 이것으로 계산)
std::vector<Grade> activeGrades(const std::vector<Grade>& grades);

}  // namespace Retake
