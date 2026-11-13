// 담당: 조장 (Eno74)
// 성적표를 CSV 문자열로 만들어줌 (웹에서 파일 다운로드로 내려줌)
//   - 엑셀에서 한글이 깨지지 않도록 맨 앞에 UTF-8 BOM 을 붙임
//   - 성적 줄은 학기 순으로 정렬, 성적이 하나도 없으면 머리줄만 있고 요약 줄(GPA, 평균 등)은 없음
//   - 재수강 규칙(삭제된 성적, A0 제한, "R" 표기)은 retake.h 참고
//   - P/F 과목은 점수 칸을 비우고 등급만 P/NP (점수는 내부적으로 P=100·NP=0 이라 내보내지 않음)
#pragma once

#include <string>

#include "grade_manager.h"

class ReportExporter {
public:
    // 학생 한 명의 성적표: 과목코드, 과목명, 이수구분, 학점, 학기, 점수, 등급 + 마지막 줄에 "GPA,4.25"
    //   재수강으로 삭제된 과목: 2018년 이후 입학생은 점수 칸 비우고 등급 "R", 그 전 입학생은 줄을 뺌
    // 없는 학생이면 빈 문자열
    static std::string studentReport(const GradeManager& gm, const std::string& studentId);

    // 과목 하나의 성적 목록: 학번, 이름, 학기, 점수, 등급 + "평균,85.50" / "최고,95" / "최저,70" 세 줄
    //   재수강으로 삭제된 성적은 뺌
    // 없는 과목이면 빈 문자열
    static std::string courseReport(const GradeManager& gm, const std::string& courseCode);
};
