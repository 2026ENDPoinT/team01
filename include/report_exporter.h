// 담당: 조장 (Eno74)
// 성적표를 CSV 문자열로 만들어줌 (웹에서 파일 다운로드로 내려줌)
#pragma once

#include <string>

#include "grade_manager.h"

class ReportExporter {
public:
    // 학생 한 명의 성적표: 과목코드, 과목명, 학점, 학기, 점수, 등급 + 마지막 줄에 GPA
    // 없는 학생이면 빈 문자열
    static std::string studentReport(const GradeManager& gm, const std::string& studentId);

    // 과목 하나의 성적 목록: 학번, 이름, 학기, 점수, 등급 + 평균/최고/최저
    // 없는 과목이면 빈 문자열
    static std::string courseReport(const GradeManager& gm, const std::string& courseCode);
};
