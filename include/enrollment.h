// 담당: 조장 (Eno74)
// 수강신청 한 건: (학번, 과목코드, 학기) 세 개로 하나를 구분
#pragma once

#include <string>

struct Enrollment {
    std::string studentId;   // Student::id
    std::string courseCode;  // Course::code
    std::string semester;    // 학기 (예: "2026-2", isValidSemester 형식)
};
