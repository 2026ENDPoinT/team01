#include "report_exporter.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

#include "retake.h"
#include "statistics.h"

namespace {

// 엑셀이 UTF-8 로 인식하도록 파일 맨 앞에 붙이는 BOM (없으면 한글이 깨짐)
const char* const kUtf8Bom = "\xEF\xBB\xBF";

// 점수 그대로 (87.5 → "87.5", 90 → "90")
std::string scoreToString(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

// 소수점 둘째 자리까지 (GPA, 평균)
std::string fixed2(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

}  // namespace

std::string ReportExporter::studentReport(const GradeManager& gm, const std::string& studentId) {
    if (gm.findStudent(studentId) == nullptr) {
        return "";
    }

    // 학기 → 과목코드 순으로 정렬
    std::vector<Grade> grades = gm.getGradesByStudent(studentId);
    std::sort(grades.begin(), grades.end(), [](const Grade& a, const Grade& b) {
        if (a.semester != b.semester) {
            return a.semester < b.semester;
        }
        return a.courseCode < b.courseCode;
    });

    // 재수강으로 삭제된 성적: 2018년 이후 입학생은 등급 "R" (점수 칸은 비움), 그 전 입학생은 줄을 뺌
    const bool showReplaced = Retake::admittedSince2018(studentId);

    std::string csv = kUtf8Bom;
    csv += joinCsvLine({"과목코드", "과목명", "이수구분", "학점", "학기", "점수", "등급"});
    for (const Grade& g : grades) {
        const bool replaced = Retake::isReplaced(g, grades);
        if (replaced && !showReplaced) {
            continue;
        }
        const Course* course = gm.findCourse(g.courseCode);
        csv += joinCsvLine({g.courseCode, course ? course->name : "", course ? course->category : "",
                            course ? std::to_string(course->credit) : "", g.semester,
                            replaced ? "" : scoreToString(g.score),
                            Retake::officialLetter(g, grades)});
    }
    if (!grades.empty()) {
        csv += joinCsvLine({"GPA", fixed2(Statistics::calcGPA(gm, studentId))});
    }
    return csv;
}

std::string ReportExporter::courseReport(const GradeManager& gm, const std::string& courseCode) {
    if (gm.findCourse(courseCode) == nullptr) {
        return "";
    }

    // 재수강으로 삭제된 성적은 빼고, 학기 → 학번 순으로 정렬
    //   등급은 전체 목록(all)으로 판단해야 재수강 성적의 A0 제한이 적용됨
    const std::vector<Grade> all = gm.getGradesByCourse(courseCode);
    std::vector<Grade> grades = Retake::activeGrades(all);
    std::sort(grades.begin(), grades.end(), [](const Grade& a, const Grade& b) {
        if (a.semester != b.semester) {
            return a.semester < b.semester;
        }
        return a.studentId < b.studentId;
    });

    std::string csv = kUtf8Bom;
    csv += joinCsvLine({"학번", "이름", "학기", "점수", "등급"});
    for (const Grade& g : grades) {
        const Student* student = gm.findStudent(g.studentId);
        csv += joinCsvLine({g.studentId, student ? student->name : "", g.semester,
                            scoreToString(g.score), Retake::officialLetter(g, all)});
    }

    Statistics::CourseStats stats = Statistics::courseStats(gm, courseCode);
    if (stats.count > 0) {
        csv += joinCsvLine({"평균", fixed2(stats.average)});
        csv += joinCsvLine({"최고", scoreToString(stats.max)});
        csv += joinCsvLine({"최저", scoreToString(stats.min)});
    }
    return csv;
}
