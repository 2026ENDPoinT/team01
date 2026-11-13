// 담당: B (현준)
// 주차별 출결 기록과 출석 점수 환산 (#59)
//   수업은 16주. 지각 3회 = 결석 1회, 공결은 출석과 같음
//   성적 반영은 GradeManager::attendanceCategoryScore 가 그때그때 계산 (항목 점수로 저장하지 않음)
#pragma once

#include <string>
#include <vector>

// 출결 기록 하나: (학번, 과목코드, 학기, 주차) 네 개로 하나를 구분
struct Attendance {
    std::string studentId;
    std::string courseCode;
    std::string semester;   // isValidSemester 형식
    int week = 0;           // 1~16
    std::string status;     // "출석" "지각" "결석" "공결"
    bool isValid() const;   // 학번 8자리, 과목코드 6자리, 학기 형식, 주차 1~16, 상태 4가지
};

// 한 학생·과목·학기의 출결 요약 (records 는 그 학생 것만 들어 있다고 가정)
struct AttendanceSummary {
    int present = 0;           // 출석 + 공결
    int late = 0;
    int absent = 0;
    int absentEquivalent = 0;  // absent + late / 3
    double score = -1.0;       // 기록이 없으면 -1
    bool shortage = false;     // absentEquivalent > 5
};

// 출석 점수 = round(100 × (16 − 결석 환산) / 16, 2), 0 미만이면 0. 기록하지 않은 주차는 출석으로 봄
//   records 가 비어 있으면 기본 AttendanceSummary (score = -1)
AttendanceSummary summarizeAttendance(const std::vector<Attendance>& records);
