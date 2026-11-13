// 담당: 동현
// 평가 항목(중간·기말·과제·출석 등)과 학생별 항목 점수
//   항목이 1개 이상 있는 (과목, 학기) 는 성적을 항목 점수로 계산 (GradeManager 가 grades 에 반영)
#pragma once

#include <string>
#include <vector>

// 평가 분류 6개 (화면·API·CSV 에 이 순서와 이 글자 그대로)
const std::vector<std::string>& assessmentCategories();  // {"출석","중간고사","기말고사","과제","팀프로젝트","기타"}
inline constexpr const char* kAttendanceCategory = "출석";  // 출석부에서 자동 계산하는 분류

// 등급 산출 방식
inline constexpr const char* kGradingAbsolute = "absolute";  // 점수 기준 (scoreToLetter)
inline constexpr const char* kGradingRelative = "relative";  // 상대평가 (relative_grading.h)

// 평가 항목 하나: (과목코드, 학기, 이름) 세 개로 하나를 구분
struct Assessment {
    std::string courseCode;
    std::string semester;   // isValidSemester 형식
    std::string name;       // assessmentCategories() 중 하나
    int weight = 0;         // 0~100 정수 (0 = 이 학기에 반영 안 함)

    // 과목코드 숫자 6자리, 학기 형식, 이름이 6분류 중 하나, 비율 0~100 을 검사
    bool isValid() const;
};

// (과목, 학기) 의 등급 산출 방식. data/grading_modes.csv : courseCode,semester,mode
struct GradingMode {
    std::string courseCode;
    std::string semester;
    std::string mode;       // kGradingAbsolute 또는 kGradingRelative

    // 과목코드 숫자 6자리, 학기 형식, mode 가 두 값 중 하나인지 검사
    bool isValid() const;
};

// 학생 한 명의 항목 점수: (학번, 과목코드, 학기, 항목 이름) 네 개로 하나를 구분
struct AssessmentScore {
    std::string studentId;
    std::string courseCode;
    std::string semester;
    std::string name;
    double score = 0.0;     // 0~100

    // 학번 숫자 8자리, 과목코드·학기 형식, 이름이 6분류 중 하나, 점수 0~100 (NaN 거부) 을 검사
    bool isValid() const;
};

// 최종 점수 = Σ(항목 점수 × 비율) / 100, 소수 둘째 자리 반올림
//   items 의 비율 합계가 100 이 아니면 -1, 비율이 0 인 항목은 무시,
//   비율이 0 보다 큰 항목 중 점수가 없는 것이 있으면 -1 (미완성), items 가 비어 있어도 -1
//   scores 에는 한 학생·과목·학기의 점수만 들어 있다고 가정
double finalScore(const std::vector<Assessment>& items, const std::vector<AssessmentScore>& scores);
