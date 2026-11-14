// 담당: B (현준)
// 학생·과목·성적·수강신청 목록을 들고 있으면서 등록/수정/삭제/조회를 담당
//   - add/update/remove 는 성공하면 true, 실패(중복, 없음, 잘못된 값)하면 false
//   - find 는 찾으면 포인터, 없으면 nullptr
#pragma once

#include <string>
#include <vector>

#include "assessment.h"
#include "attendance.h"
#include "course.h"
#include "course_post.h"
#include "enrollment.h"
#include "file_io.h"
#include "grade.h"
#include "student.h"

class GradeManager {
public:
    explicit GradeManager(const std::string& dataDir);

    // 프로그램 시작 시 CSV 에서 불러오기 / 변경 후 저장 (학생·과목·성적·수강신청·설정·게시물·평가 항목·항목 점수·출결·등급 방식)
    bool load();
    bool save();

    // ----- 학생 -----
    bool addStudent(const Student& student);                    // 학번 중복이면 false
    bool updateStudent(const std::string& id, const Student& student);  // 학번이 바뀌면 성적·수강신청·항목 점수·출결 학번도 바꿈
    bool removeStudent(const std::string& id);                  // 그 학생의 성적·수강신청·항목 점수·출결도 같이 삭제
    const Student* findStudent(const std::string& id) const;
    std::vector<Student> searchStudents(const std::string& keyword) const;  // 학번 또는 이름에 keyword 포함
    const std::vector<Student>& getStudents() const;

    // ----- 과목 -----
    bool addCourse(const Course& course);                       // 과목코드 중복이면 false
    //   성적 방식(gradeType)은 그 과목에 성적·평가 항목이 없을 때만 바꿀 수 있음 (있으면 false)
    bool updateCourse(const std::string& code, const Course& course);  // 코드가 바뀌면 성적·수강신청·게시물·평가 항목·항목 점수·출결 코드도 바꿈
    bool removeCourse(const std::string& code);                 // 그 과목의 성적·수강신청·게시물·평가 항목·항목 점수·출결도 같이 삭제
    const Course* findCourse(const std::string& code) const;
    // 그 과목(모든 학기)에 성적 또는 평가 항목이 있으면 true (있으면 성적 방식을 바꿀 수 없음)
    bool courseHasGradesOrAssessments(const std::string& courseCode) const;
    const std::vector<Course>& getCourses() const;

    // ----- 성적 -----
    // 성적은 (학번, 과목코드, 학기) 세 개로 하나를 구분
    // 항목 계산 과목(hasAssessments)의 성적을 직접 추가·수정·삭제하지 못하게 막는 것은 서버가 함
    //   P/F 과목은 assignedLetter 가 P(점수 100)·NP(점수 0) 인 성적만, 일반 과목은 P·NP 가 아닌 성적만 받음 (아니면 false)
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
    // removeEnrollment 는 성적·항목 점수를 건드리지 않음 (성적·항목 점수가 있는 수강 취소는 서버가 막음)
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
    // 항목을 추가·수정·삭제하면 그 과목·학기에 수강신청한 학생 전원의 성적을 다시 계산
    //   finalScore ≥ 0 이면 grades 의 성적을 추가하거나 점수를 바꾸고, -1(미완성)이면 그 성적을 지움
    bool hasAssessments(const std::string& courseCode, const std::string& semester) const;
    // 그 과목·학기 항목을 assessmentCategories() 순서로 (복사본)
    std::vector<Assessment> getAssessments(const std::string& courseCode, const std::string& semester) const;
    const std::vector<Assessment>& getAllAssessments() const;
    // 과목 없음, 형식 오류, 같은 이름 중복, 비율 합계 100 초과,
    // 첫 항목인데 그 과목·학기에 이미 성적(grades)이 있으면 false
    bool addAssessment(const Assessment& item);
    // name 항목을 item 으로 바꿈 (이름이 바뀌면 항목 점수의 이름도 바꿈). 없음·이름 중복·합계 초과면 false
    //   형식 오류, item 의 과목코드·학기가 인자와 다르면(항목 이동) 도 false. 합계는 바꾸기 전 자기 비율을 빼고 계산
    bool updateAssessment(const std::string& courseCode, const std::string& semester,
                          const std::string& name, const Assessment& item);
    // 항목과 그 항목의 점수 삭제. 없으면 false
    //   마지막 항목을 지우면 그 과목·학기의 항목 점수와 성적(grades)도 모두 지움
    bool removeAssessment(const std::string& courseCode, const std::string& semester, const std::string& name);

    // ----- 항목 점수 -----
    // 항목 점수를 바꾸거나 지우면 그 학생의 그 과목·학기 성적을 다시 계산
    // 출석은 저장하지 않고 계산: 출석 비율이 0 보다 크면 수강생마다 {학번, 과목, 학기, "출석", attendanceCategoryScore}
    //   를 저장된 점수 뒤에 덧붙여 돌려줌
    std::vector<AssessmentScore> getAssessmentScores(const std::string& courseCode, const std::string& semester) const;
    std::vector<AssessmentScore> getAssessmentScoresByStudent(const std::string& studentId) const;
    // 있으면 점수 수정, 없으면 추가. 형식 오류, 항목 없음, 같은 (학번, 과목, 학기) 수강신청 없음이면 false
    //   출석(자동 계산)이거나 그 분류 비율이 0 이어도 false
    bool setAssessmentScore(const AssessmentScore& score);
    // 없으면 false
    bool removeAssessmentScore(const std::string& studentId, const std::string& courseCode,
                               const std::string& semester, const std::string& name);

    // ----- 평가 기준 (과목·학기마다 6분류 비율 + 등급 산출 방식) -----
    // 등급 산출 방식. 평가 기준이 없거나 기록이 없으면 kGradingAbsolute
    std::string getGradingMode(const std::string& courseCode, const std::string& semester) const;
    // 상대평가 등급 비율 (A+, A0, B+, B0, C+, C0, D+, D0, F 순서). 설정이 없으면 기본 비율.
    // 평가 기준 만들기·바꾸기. weights 는 assessmentCategories() 순서의 6개 정수
    //   false: 없는 과목·P/F 과목, 학기 형식 오류, weights 크기 != 6, 0~100 밖, 합계 != 100, mode 가 두 값이 아님,
    //          처음 만드는데 그 과목·학기에 이미 성적(직접 입력)이 있음
    //   성공: 6개 항목을 모두 저장(0 포함), 비율이 0 이 된 분류의 항목 점수 삭제, 수강생 전원 성적 재계산
    bool setGradingScheme(const std::string& courseCode, const std::string& semester,
                          const std::vector<int>& weights, const std::string& mode,
                          const std::vector<int>& gradeRatios = {30, 0, 40, 0, 30, 0, 0, 0, 0});
    std::vector<int> getRelativeGradeRatios(const std::string& courseCode,
                                            const std::string& semester) const;
    // 평가 기준 삭제: 항목·항목 점수·등급 방식·그 과목·학기 성적을 모두 지움 (직접 입력 과목으로 돌아감). 없으면 false
    bool removeGradingScheme(const std::string& courseCode, const std::string& semester);

    // ----- 출결 (#59) -----
    // 출결은 항목 점수로 저장하지 않음. 바뀌면 그 학생의 성적만 다시 계산(syncGrade)
    // 형식 오류, 없는 학생·과목, 수강신청 없음이면 false. 있으면 수정, 없으면 추가.
    bool setAttendance(const Attendance& record);
    // 없으면 false. 지운 뒤 그 학생의 성적을 다시 계산
    bool removeAttendance(const std::string& studentId, const std::string& courseCode,
                          const std::string& semester, int week);
    std::vector<Attendance> getAttendanceByCourse(const std::string& courseCode, const std::string& semester) const;
    std::vector<Attendance> getAttendanceByStudent(const std::string& studentId) const;
    // 성적 계산에 쓰는 출석 분류 점수 (0~100)
    //   그 학생·과목·학기의 출결 기록이 있으면 summarizeAttendance(기록).score, 하나도 없으면 100 (모두 출석)
    double attendanceCategoryScore(const std::string& studentId, const std::string& courseCode,
                                   const std::string& semester) const;

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
    std::vector<Attendance> attendance;
    std::vector<GradingMode> gradingModes;
    std::vector<RelativeGradeRatio> relativeGradeRatios;

    // 항목 계산 과목이면 그 학생의 최종 점수를 다시 계산해 grades 에 반영 (항목이 없으면 아무것도 안 함)
    //   점수 = 저장된 그 학생 점수(출석 제외) + 출석 비율이 0 보다 크면 attendanceCategoryScore. 끝에서 applyGradingMode
    void syncGrade(const std::string& studentId, const std::string& courseCode, const std::string& semester);
    // 그 과목·학기에 수강신청한 학생 전원에게 syncGrade
    void syncCourse(const std::string& courseCode, const std::string& semester);
    // 그 과목·학기 grades 전부의 assignedLetter 를 다시 정함
    //   relative 면 RelativeGrading::assignLetters(점수들, grades 순서), 아니면 ""
    //   P/F 과목은 건드리지 않음 (P·NP 유지, 상대평가 대상 아님)
    void applyGradingMode(const std::string& courseCode, const std::string& semester);
};
