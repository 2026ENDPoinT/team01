// 담당: 조장 (Eno74)
// 테스트 공용 도우미: 임시 폴더, 학생·과목·성적·수강신청 만들기
#pragma once

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

#include "course.h"
#include "enrollment.h"
#include "grade.h"
#include "student.h"

// 테스트마다 새로 만드는 임시 폴더 (소멸자에서 통째로 지움) → 실제 data/ 는 건드리지 않음
class TempDir {
public:
    TempDir() {
        namespace fs = std::filesystem;
        std::random_device rd;
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0;; attempt++) {
            const std::string name = "grade_tests_" + std::to_string(now) + "_" + std::to_string(rd()) + "_" +
                                     std::to_string(attempt);
            path_ = fs::temp_directory_path() / name;
            if (fs::create_directory(path_)) {  // 이미 있으면 false → 다른 이름으로 다시
                break;
            }
        }
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string str() const { return path_.string(); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

inline Student makeStudent(const std::string& id, const std::string& name, int year = 1) {
    Student s;
    s.id = id;
    s.name = name;
    s.major = "컴퓨터공학";
    s.year = year;
    return s;
}

inline Course makeCourse(const std::string& code, const std::string& name, int credit = 3) {
    Course c;
    c.code = code;
    c.name = name;
    c.credit = credit;
    return c;
}

inline Grade makeGrade(const std::string& studentId, const std::string& courseCode, const std::string& semester,
                       double score) {
    Grade g;
    g.studentId = studentId;
    g.courseCode = courseCode;
    g.semester = semester;
    g.score = score;
    return g;
}

inline Enrollment makeEnrollment(const std::string& studentId, const std::string& courseCode,
                                 const std::string& semester) {
    Enrollment e;
    e.studentId = studentId;
    e.courseCode = courseCode;
    e.semester = semester;
    return e;
}
