// 담당: 조장 (Eno74) 선언 (#31), finalScore 구현: A (연준) (#32)
// 평가 항목(중간·기말·과제·출석 등)과 학생별 항목 점수
//   data/assessments.csv       : courseCode,semester,name,weight
//   data/assessment_scores.csv : studentId,courseCode,semester,name,score
#pragma once

#include <string>
#include <vector>

// 평가 항목 하나: (과목코드, 학기, 이름) 세 개로 하나를 구분
struct Assessment {
    std::string courseCode;
    std::string semester;   // isValidSemester 형식
    std::string name;       // 예: "중간고사", "출석". 공백만이면 안 됨, 60바이트 이하
    int weight = 0;         // 반영 비율(%) 정수 1~100
    bool isValid() const;
};

// 학생 한 명의 항목 점수: (학번, 과목코드, 학기, 항목 이름) 네 개로 하나를 구분
struct AssessmentScore {
    std::string studentId;
    std::string courseCode;
    std::string semester;
    std::string name;
    double score = 0.0;     // 0~100
    bool isValid() const;
};

// 최종 점수 = Σ(항목 점수 × 비율) / 100, 소수 둘째 자리 반올림
//   items 의 비율 합계가 100 이 아니거나, 점수가 없는 항목이 하나라도 있으면 -1 (미완성)
//   scores 에는 한 학생·과목·학기의 점수만 들어 있다고 가정
double finalScore(const std::vector<Assessment>& items, const std::vector<AssessmentScore>& scores);
