// 담당: 조장 (Eno74)
// data/ 폴더의 CSV 파일 읽기/쓰기
//   - 파일이 없으면 빈 목록으로 시작 (저장할 때 자동 생성)
//   - 형식이 깨진 줄은 건너뛰고 경고 출력
//   - 저장 시: 기존 파일을 .bak 으로 백업 → 임시파일에 쓰기 → rename (중간에 꺼져도 원본 보존)
#pragma once

#include <string>
#include <vector>

#include "course.h"
#include "enrollment.h"
#include "grade.h"
#include "student.h"

struct Account;  // account.h 가 이 파일을 include 하므로 미리 선언만 함

// 값 여러 개를 쉼표로 이어서 CSV 한 줄로 만듦 (끝에 줄바꿈 포함)
//   쉼표나 큰따옴표가 있는 값은 "..." 로 감싸고, 안의 " 는 "" 로 바꿈
//   ReportExporter 도 같은 규칙으로 CSV 를 만들도록 공개
std::string joinCsvLine(const std::vector<std::string>& fields);

class FileIO {
public:
    explicit FileIO(const std::string& dataDir);

    bool saveStudents(const std::vector<Student>& students);
    bool loadStudents(std::vector<Student>& students);

    bool saveCourses(const std::vector<Course>& courses);
    bool loadCourses(std::vector<Course>& courses);

    bool saveGrades(const std::vector<Grade>& grades);
    bool loadGrades(std::vector<Grade>& grades);

    // enrollments.csv : studentId,courseCode,semester
    bool saveEnrollments(const std::vector<Enrollment>& enrollments);
    bool loadEnrollments(std::vector<Enrollment>& enrollments);

    // settings.csv : key,value (키는 enrollmentSemester 하나, 다른 키는 무시, 파일이 없으면 "")
    bool saveSettings(const std::string& enrollmentSemester);
    bool loadSettings(std::string& enrollmentSemester);

    // accounts.csv : username,role,name,studentId,salt,passwordHash
    bool saveAccounts(const std::vector<Account>& accounts);
    bool loadAccounts(std::vector<Account>& accounts);

private:
    std::string dataDir;

    // content 를 path 에 안전하게 저장 (임시파일 + rename)
    bool atomicWrite(const std::string& path, const std::string& content);
    // path 가 있으면 path.bak 으로 복사
    void backup(const std::string& path);
};
