// 담당: B (현준)
// 학생·과목·성적·수강신청 목록을 들고 있으면서 등록/수정/삭제/조회를 담당
//   - add/update/remove 는 성공하면 true, 실패(중복, 없음, 잘못된 값)하면 false
//   - find 는 찾으면 포인터, 없으면 nullptr
#pragma once

#include <string>
#include <vector>

#include "course.h"
#include "enrollment.h"
#include "file_io.h"
#include "grade.h"
#include "student.h"

class GradeManager {
public:
    explicit GradeManager(const std::string& dataDir);

    // 프로그램 시작 시 CSV 에서 불러오기 / 변경 후 저장 (학생·과목·성적·수강신청·설정)
    bool load();
    bool save();

    // ----- 학생 -----
    bool addStudent(const Student& student);                    // 학번 중복이면 false
    bool updateStudent(const std::string& id, const Student& student);  // 학번이 바뀌면 성적·수강신청 학번도 바꿈
    bool removeStudent(const std::string& id);                  // 그 학생의 성적·수강신청도 같이 삭제
    const Student* findStudent(const std::string& id) const;
    std::vector<Student> searchStudents(const std::string& keyword) const;  // 학번 또는 이름에 keyword 포함
    const std::vector<Student>& getStudents() const;

    // ----- 과목 -----
    bool addCourse(const Course& course);                       // 과목코드 중복이면 false
    bool updateCourse(const std::string& code, const Course& course);  // 코드가 바뀌면 성적·수강신청 코드도 바꿈
    bool removeCourse(const std::string& code);                 // 그 과목의 성적·수강신청도 같이 삭제
    const Course* findCourse(const std::string& code) const;
    const std::vector<Course>& getCourses() const;

    // ----- 성적 -----
    // 성적은 (학번, 과목코드, 학기) 세 개로 하나를 구분
    bool addGrade(const Grade& grade);                          // 없는 학생/과목이거나 중복이면 false (수강신청 검사는 서버가 함)
    bool updateGrade(const Grade& grade);                       // 같은 (학번, 과목, 학기) 의 점수 수정
    bool removeGrade(const std::string& studentId, const std::string& courseCode,
                     const std::string& semester);
    std::vector<Grade> getGradesByStudent(const std::string& studentId) const;
    std::vector<Grade> getGradesByCourse(const std::string& courseCode) const;
    std::vector<Grade> getGradesBySemester(const std::string& semester) const;
    const std::vector<Grade>& getGrades() const;

    // ----- 수강신청 -----
    // 수강신청도 (학번, 과목코드, 학기) 세 개로 하나를 구분
    // 학기 형식 오류, 없는 학생/과목, 중복이면 false. 정원·강의시간 겹침은 서버가 검사
    bool addEnrollment(const Enrollment& enrollment);
    bool removeEnrollment(const std::string& studentId, const std::string& courseCode,
                          const std::string& semester);
    const Enrollment* findEnrollment(const std::string& studentId, const std::string& courseCode,
                                     const std::string& semester) const;
    const std::vector<Enrollment>& getEnrollments() const;

    // 수강신청 학기 ("" 이면 수강신청 기간이 아님)
    const std::string& getEnrollmentSemester() const;
    bool setEnrollmentSemester(const std::string& semester);  // "" 또는 올바른 학기 형식이면 true

private:
    FileIO fileIO;
    std::vector<Student> students;
    std::vector<Course> courses;
    std::vector<Grade> grades;
    std::vector<Enrollment> enrollments;
    std::string enrollmentSemester;
};
