#include "retake.h"

#include <cctype>
#include <map>
#include <utility>

namespace Retake {

bool admittedSince2018(const std::string& studentId) {
    if (studentId.size() < 2 || !std::isdigit(static_cast<unsigned char>(studentId[0])) ||
        !std::isdigit(static_cast<unsigned char>(studentId[1]))) {
        return false;
    }
    const int year = (studentId[0] - '0') * 10 + (studentId[1] - '0');  // "24..." → 24 (2024년)
    return year >= 18;
}

// 학기 문자열은 "2025-2" < "2026-1" 처럼 문자열 비교가 곧 시간 순서
bool isReplaced(const Grade& grade, const std::vector<Grade>& grades) {
    for (const Grade& g : grades) {
        if (g.studentId == grade.studentId && g.courseCode == grade.courseCode &&
            g.semester > grade.semester) {
            return true;
        }
    }
    return false;
}

bool isRetake(const Grade& grade, const std::vector<Grade>& grades) {
    for (const Grade& g : grades) {
        if (g.studentId == grade.studentId && g.courseCode == grade.courseCode &&
            g.semester < grade.semester) {
            return true;
        }
    }
    return false;
}

std::string officialLetter(const Grade& grade, const std::vector<Grade>& grades) {
    if (isReplaced(grade, grades)) {
        return "R";
    }
    std::string letter = scoreToLetter(grade.score);
    if (letter == "A+" && isRetake(grade, grades)) {
        return "A0";
    }
    return letter;
}

std::vector<Grade> activeGrades(const std::vector<Grade>& grades) {
    // (학번, 과목코드) → 가장 늦은 학기
    std::map<std::pair<std::string, std::string>, std::string> latest;
    for (const Grade& g : grades) {
        std::string& semester = latest[{g.studentId, g.courseCode}];
        if (g.semester > semester) {
            semester = g.semester;
        }
    }

    std::vector<Grade> result;
    for (const Grade& g : grades) {
        if (g.semester == latest[{g.studentId, g.courseCode}]) {
            result.push_back(g);
        }
    }
    return result;
}

}  // namespace Retake
