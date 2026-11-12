// 담당: B (현준)
// 학생·과목·성적·수강신청 목록을 들고 있으면서 등록/수정/삭제/조회를 담당
//   - add/update/remove 는 성공하면 true, 실패(중복, 없음, 잘못된 값)하면 false
//   - find 는 찾으면 포인터, 없으면 nullptr
#pragma once

#include <string>
#include <vector>

#include "assessment.h"
#include "course.h"
#include "course_post.h"
#include "enrollment.h"
#include "file_io.h"
#include "grade.h"
#include "student.h"

class GradeManager {
public:
    explicit GradeManager(const std::string& dataDir);

    // 프로그램 시작 시 CSV 에서 불러오기 / 변경 후 저장 (학생·과목·성적·수강신청·설정·게시물·항목·항목 점수)
    bool load();
    bool save();

    // ----- 학생 -----
    bool addStudent(const Student& student);                    // 학번 중복이면 false
    bool updateStudent(const std::string& id, const Student& student);  // 학번이 바뀌면 성적·수강신청·항목 점수 학번도 바꿈
    bool removeStudent(const std::string& id);                  // 그 학생의 성적·수강신청·항목 점수도 같이 삭제
    const Student* findStudent(const std::string& id) const;
    std::vector<Student> searchStudents(const std::string& keyword) const;  // 학번 또는 이름에 keyword 포함
    const std::vector<Student>& getStudents() const;

    // ----- 과목 -----
    bool addCourse(const Course& course);                       // 과목코드 중복이면 false
    bool updateCourse(const std::string& code, const Course& course);  // 코드가 바뀌면 성적·수강신청·게시물·항목·항목 점수 코드도 바꿈
    bool removeCourse(const std::string& code);                 // 그 과목의 성적·수강신청·게시물·항목·항목 점수도 같이 삭제
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

    // ----- 과목 게시물 -----
    int addPost(const CoursePost& post);        // 값 오류·없는 과목이면 0, 성공하면 새 id (최대 id + 1)
    bool updatePost(const CoursePost& post);    // 같은 id 를 post 로 교체. 없음·값 오류·없는 과목이면 false
    bool removePost(int id);                    // 없으면 false
    const CoursePost* findPost(int id) const;
    const std::vector<CoursePost>& getPosts() const;

    // ----- 평가 항목 -----
    // 항목이 1개 이상 있는 (과목, 학기) = "항목 계산 과목": 그 성적은 항목 점수로만 만들어짐
    //   항목·항목 점수가 바뀔 때마다 finalScore 로 다시 계산해서 grades 에 반영
    //   (완성 ≥ 0 이면 성적 추가·수정, 미완성 -1 이면 성적 삭제)
    bool hasAssessments(const std::string& courseCode, const std::string& semester) const;
    std::vector<Assessment> getAssessments(const std::string& courseCode, const std::string& semester) const;
    const std::vector<Assessment>& getAllAssessments() const;
    // 과목 없음, 형식 오류, 같은 이름 중복, 비율 합계 100 초과,
    // 첫 항목인데 그 과목·학기에 이미 성적(grades)이 있으면 false
    bool addAssessment(const Assessment& item);
    // name 항목을 item 으로 바꿈 (이름이 바뀌면 항목 점수의 이름도 바꿈). 없음·이름 중복·합계 초과면 false
    //   item 의 과목코드·학기는 바꿀 수 없음 (courseCode, semester 와 다르면 false)
    bool updateAssessment(const std::string& courseCode, const std::string& semester,
                          const std::string& name, const Assessment& item);
    // 항목과 그 항목의 점수 삭제. 없으면 false
    //   마지막 항목을 지우면 그 과목·학기의 항목 점수와 성적이 모두 지워짐
    bool removeAssessment(const std::string& courseCode, const std::string& semester, const std::string& name);

    // ----- 항목 점수 -----
    std::vector<AssessmentScore> getAssessmentScores(const std::string& courseCode, const std::string& semester) const;
    std::vector<AssessmentScore> getAssessmentScoresByStudent(const std::string& studentId) const;
    // 있으면 점수 수정, 없으면 추가. 형식 오류, 항목 없음, 같은 (학번, 과목, 학기) 수강신청 없음이면 false
    bool setAssessmentScore(const AssessmentScore& score);
    bool removeAssessmentScore(const std::string& studentId, const std::string& courseCode,
                               const std::string& semester, const std::string& name);

private:
    FileIO fileIO;
    std::vector<Student> students;
    std::vector<Course> courses;
    std::vector<Grade> grades;
    std::vector<Enrollment> enrollments;
    std::string enrollmentSemester;
    std::vector<CoursePost> posts;
    std::vector<Assessment> assessments;
    std::vector<AssessmentScore> assessmentScores;

    // 한 학생의 (과목, 학기) 성적을 항목 점수로 다시 계산해 grades 에 반영 (항목 계산 과목이 아니면 아무것도 안 함)
    void syncGrade(const std::string& studentId, const std::string& courseCode, const std::string& semester);
    // 그 과목·학기에 수강신청한 학생 전원에게 syncGrade
    void syncCourse(const std::string& courseCode, const std::string& semester);
};
