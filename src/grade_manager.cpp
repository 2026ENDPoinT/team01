#include "grade_manager.h"

#include <algorithm>
#include <iostream>
#include <utility>

#include "relative_grading.h"

namespace {

// 그 과목·학기 name 분류의 비율. 항목이 없으면 -1
int categoryWeight(const std::vector<Assessment>& items, const std::string& courseCode,
                   const std::string& semester, const std::string& name) {
    for (const Assessment& item : items) {
        if (item.courseCode == courseCode && item.semester == semester && item.name == name) {
            return item.weight;
        }
    }
    return -1;
}

// 성적이 그 과목의 성적 방식에 맞는지
//   P/F 과목: assignedLetter 가 P(점수 100) 또는 NP(점수 0). 일반 과목: P·NP 는 받지 않음
//   나머지 형식(학번·과목코드·학기·점수 범위)은 Grade::isValid 로 검사 (P·NP 는 그 검사에서 빼고 여기서 봄)
bool gradeFitsCourse(const Grade& grade, const Course& course) {
    const bool pfLetter = isPassFailLetter(grade.assignedLetter);
    Grade rest = grade;
    if (pfLetter) rest.assignedLetter.clear();
    if (!rest.isValid()) return false;
    if (!course.isPassFail()) return !pfLetter;
    if (!pfLetter) return false;
    return grade.score == (grade.assignedLetter == kLetterPass ? kPassScore : kNonPassScore);
}

}  // namespace

// 생성자: CSV 파일이 있는 폴더 경로를 FileIO 에 넘겨 준다.
GradeManager::GradeManager(const std::string& dataDir) : fileIO(dataDir) {}

// CSV 파일을 모두 읽어 멤버에 채운다. 하나라도 실패하면 false.
bool GradeManager::load() {
    if (!(fileIO.loadStudents(students) && fileIO.loadCourses(courses) && fileIO.loadGrades(grades) &&
          fileIO.loadEnrollments(enrollments) && fileIO.loadSettings(enrollmentSemester) &&
          fileIO.loadPosts(posts) && fileIO.loadAssessments(assessments) &&
          fileIO.loadAssessmentScores(assessmentScores) && fileIO.loadAttendance(attendance) &&
          fileIO.loadGradingModes(gradingModes) &&
           fileIO.loadRelativeGradeRatios(relativeGradeRatios))) {
        return false;
    }
    // 신청 학기 값이 깨져 있으면 "신청 기간 아님" 으로 시작
    if (!enrollmentSemester.empty() && !isValidSemester(enrollmentSemester)) {
        std::cerr << "경고: settings.csv 의 수강신청 학기 \"" << enrollmentSemester
                  << "\" 가 올바르지 않아 비웁니다." << std::endl;
        enrollmentSemester.clear();
    }

    // 저장된 상대평가 등급도 현재 비율·산출 규칙으로 다시 계산한다.
    for (const GradingMode& mode : gradingModes) {
        if (mode.mode == kGradingRelative) {
            applyGradingMode(mode.courseCode, mode.semester);
        }
    }
    return true;
}

// 메모리의 데이터를 CSV 파일들에 저장한다. 하나라도 실패하면 false.
bool GradeManager::save() {
    return fileIO.saveStudents(students) && fileIO.saveCourses(courses) && fileIO.saveGrades(grades) &&
           fileIO.saveEnrollments(enrollments) && fileIO.saveSettings(enrollmentSemester) &&
           fileIO.savePosts(posts) && fileIO.saveAssessments(assessments) &&
           fileIO.saveAssessmentScores(assessmentScores) && fileIO.saveAttendance(attendance) &&
           fileIO.saveGradingModes(gradingModes) &&
           fileIO.saveRelativeGradeRatios(relativeGradeRatios);
}

// ----- 학생 -----
// 학생 등록: 값이 잘못됐거나 학번이 이미 있으면 false.
bool GradeManager::addStudent(const Student& student) {
    if (!student.isValid() || findStudent(student.id) != nullptr) {
        return false;
    }
    students.push_back(student);
    return true;
}

// 학생 수정: id 로 대상을 찾아 student 내용으로 덮어쓴다.
// 대상이 없거나, 값이 잘못됐거나, 바뀔 학번이 다른 학생과 겹치면 false.
bool GradeManager::updateStudent(const std::string& id, const Student& student) {
    auto it = std::find_if(students.begin(), students.end(), [&](const Student& current) {
        return current.id == id;
    });
    if (it == students.end() || !student.isValid()) {
        return false;
    }

    const bool duplicateId = std::any_of(students.begin(), students.end(), [&](const Student& current) {
        return current.id == student.id && current.id != id;
    });
    if (duplicateId) {
        return false;
    }

    // 학번이 바뀌면 그 학생의 성적·수강신청·항목 점수에 적힌 학번도 같이 바꿔서 연결이 끊기지 않게 한다.
    if (student.id != id) {
        for (Grade& grade : grades) {
            if (grade.studentId == id) {
                grade.studentId = student.id;
            }
        }
        for (Enrollment& enrollment : enrollments) {
            if (enrollment.studentId == id) {
                enrollment.studentId = student.id;
            }
        }
        for (AssessmentScore& score : assessmentScores) {
            if (score.studentId == id) {
                score.studentId = student.id;
            }
        }
        for (Attendance& record : attendance) {
            if (record.studentId == id) {
                record.studentId = student.id;
            }
        }
    }
    // 모든 검사를 통과한 뒤에 마지막으로 덮어쓴다.
    *it = student;
    return true;
}

// 학생 삭제: 학생과 그 학생의 성적·수강신청·항목 점수를 함께 지운다. 대상이 없으면 false.
bool GradeManager::removeStudent(const std::string& id) {
    auto it = std::find_if(students.begin(), students.end(), [&](const Student& student) {
        return student.id == id;
    });
    if (it == students.end()) {
        return false;
    }

    // 이 학생이 성적을 가진 (과목, 학기): 지운 뒤 남은 학생들의 등급을 다시 정해야 함 (상대평가)
    std::vector<std::pair<std::string, std::string>> graded;
    for (const Grade& grade : grades) {
        if (grade.studentId == id) {
            graded.emplace_back(grade.courseCode, grade.semester);
        }
    }

    // erase-remove 관용구: 조건에 맞는 성적을 뒤로 모은 뒤 한 번에 지운다.
    students.erase(it);
    grades.erase(std::remove_if(grades.begin(), grades.end(), [&](const Grade& grade) {
        return grade.studentId == id;
    }), grades.end());
    enrollments.erase(std::remove_if(enrollments.begin(), enrollments.end(), [&](const Enrollment& enrollment) {
        return enrollment.studentId == id;
    }), enrollments.end());
    assessmentScores.erase(std::remove_if(assessmentScores.begin(), assessmentScores.end(),
                                          [&](const AssessmentScore& score) { return score.studentId == id; }),
                           assessmentScores.end());
    attendance.erase(std::remove_if(attendance.begin(), attendance.end(),
                                    [&](const Attendance& record) { return record.studentId == id; }),
                     attendance.end());
    for (const auto& [courseCode, semester] : graded) {
        applyGradingMode(courseCode, semester);
    }
    return true;
}

// 학번으로 학생 찾기: 있으면 벡터 원소의 주소, 없으면 nullptr.
// (벡터에 추가/삭제가 일어나면 이 포인터는 무효가 될 수 있어서 바로 쓰고 버려야 한다.)
const Student* GradeManager::findStudent(const std::string& id) const {
    auto it = std::find_if(students.begin(), students.end(), [&](const Student& student) {
        return student.id == id;
    });
    return it == students.end() ? nullptr : &*it;
}

// 학번 또는 이름에 keyword 가 포함된 학생 목록을 복사해서 반환.
std::vector<Student> GradeManager::searchStudents(const std::string& keyword) const {
    std::vector<Student> result;
    for (const Student& student : students) {
        if (student.id.find(keyword) != std::string::npos ||
            student.name.find(keyword) != std::string::npos) {
            result.push_back(student);
        }
    }
    return result;
}

// 전체 학생 목록 (읽기 전용 참조).
const std::vector<Student>& GradeManager::getStudents() const { return students; }

// ----- 과목 -----
// 과목 등록: 값이 잘못됐거나 과목코드가 이미 있으면 false.
bool GradeManager::addCourse(const Course& course) {
    if (!course.isValid() || findCourse(course.code) != nullptr) {
        return false;
    }
    courses.push_back(course);
    return true;
}

// 과목 수정: updateStudent 와 같은 규칙. 과목코드가 바뀌면 성적·수강신청·게시물·평가 항목·항목 점수의 과목코드도 같이 바꾼다.
bool GradeManager::updateCourse(const std::string& code, const Course& course) {
    auto it = std::find_if(courses.begin(), courses.end(), [&](const Course& current) {
        return current.code == code;
    });
    if (it == courses.end() || !course.isValid()) {
        return false;
    }
    // 성적 방식(등급 ↔ P/F)은 그 과목의 성적·평가 항목이 하나도 없을 때만 바꿀 수 있음 (있는 성적과 방식이 어긋나므로)
    if (course.gradeType != it->gradeType && courseHasGradesOrAssessments(code)) {
        return false;
    }

    const bool duplicateCode = std::any_of(courses.begin(), courses.end(), [&](const Course& current) {
        return current.code == course.code && current.code != code;
    });
    if (duplicateCode) {
        return false;
    }

    // 과목코드가 바뀌면 그 과목의 성적·수강신청·게시물·평가 항목·항목 점수에 적힌 코드도 같이 바꾼다.
    if (course.code != code) {
        for (Grade& grade : grades) {
            if (grade.courseCode == code) {
                grade.courseCode = course.code;
            }
        }
        for (Enrollment& enrollment : enrollments) {
            if (enrollment.courseCode == code) {
                enrollment.courseCode = course.code;
            }
        }
        for (CoursePost& post : posts) {
            if (post.courseCode == code) {
                post.courseCode = course.code;
            }
        }
        for (Assessment& item : assessments) {
            if (item.courseCode == code) {
                item.courseCode = course.code;
            }
        }
        for (AssessmentScore& score : assessmentScores) {
            if (score.courseCode == code) {
                score.courseCode = course.code;
            }
        }
        for (Attendance& record : attendance) {
            if (record.courseCode == code) {
                record.courseCode = course.code;
            }
        }
        for (GradingMode& mode : gradingModes) {
            if (mode.courseCode == code) {
                mode.courseCode = course.code;
            }
        }
        for (RelativeGradeRatio& ratio : relativeGradeRatios) {
            if (ratio.courseCode == code) {
                ratio.courseCode = course.code;
            }
        }
    }
    // 모든 검사를 통과한 뒤에 마지막으로 덮어쓴다.
    *it = course;
    return true;
}

// 과목 삭제: 과목과 그 과목의 성적·수강신청·게시물·평가 항목·항목 점수를 함께 지운다. 대상이 없으면 false.
bool GradeManager::removeCourse(const std::string& code) {
    auto it = std::find_if(courses.begin(), courses.end(), [&](const Course& course) {
        return course.code == code;
    });
    if (it == courses.end()) {
        return false;
    }

    // 이 과목의 성적도 함께 제거 (erase-remove).
    courses.erase(it);
    grades.erase(std::remove_if(grades.begin(), grades.end(), [&](const Grade& grade) {
        return grade.courseCode == code;
    }), grades.end());
    enrollments.erase(std::remove_if(enrollments.begin(), enrollments.end(), [&](const Enrollment& enrollment) {
        return enrollment.courseCode == code;
    }), enrollments.end());
    posts.erase(std::remove_if(posts.begin(), posts.end(), [&](const CoursePost& post) {
        return post.courseCode == code;
    }), posts.end());
    assessments.erase(std::remove_if(assessments.begin(), assessments.end(),
                                     [&](const Assessment& item) { return item.courseCode == code; }),
                      assessments.end());
    assessmentScores.erase(std::remove_if(assessmentScores.begin(), assessmentScores.end(),
                                          [&](const AssessmentScore& score) { return score.courseCode == code; }),
                           assessmentScores.end());
    attendance.erase(std::remove_if(attendance.begin(), attendance.end(),
                                    [&](const Attendance& record) { return record.courseCode == code; }),
                     attendance.end());
    gradingModes.erase(std::remove_if(gradingModes.begin(), gradingModes.end(),
                                      [&](const GradingMode& mode) { return mode.courseCode == code; }),
                       gradingModes.end());
    relativeGradeRatios.erase(std::remove_if(relativeGradeRatios.begin(), relativeGradeRatios.end(),
                                             [&](const RelativeGradeRatio& ratio) {
                                                 return ratio.courseCode == code;
                                             }),
                              relativeGradeRatios.end());
    return true;
}

// 과목코드로 과목 찾기: 있으면 벡터 원소의 주소, 없으면 nullptr.
const Course* GradeManager::findCourse(const std::string& code) const {
    auto it = std::find_if(courses.begin(), courses.end(), [&](const Course& course) {
        return course.code == code;
    });
    return it == courses.end() ? nullptr : &*it;
}

// 전체 과목 목록 (읽기 전용 참조).
const std::vector<Course>& GradeManager::getCourses() const { return courses; }

// ----- 성적 -----
// 성적 등록: 성적은 (학번, 과목코드, 학기) 조합으로 구분한다.
// 값이 잘못됐거나, 없는 학생/과목이거나, 같은 조합이 이미 있으면 false.
bool GradeManager::addGrade(const Grade& grade) {
    const Course* course = findCourse(grade.courseCode);
    if (course == nullptr || findStudent(grade.studentId) == nullptr || !gradeFitsCourse(grade, *course)) {
        return false;
    }

    // 같은 (학번, 과목코드, 학기) 조합이 이미 있는지 확인
    const bool duplicate = std::any_of(grades.begin(), grades.end(), [&](const Grade& current) {
        return current.studentId == grade.studentId &&
               current.courseCode == grade.courseCode &&
               current.semester == grade.semester;
    });
    if (duplicate) {
        return false;
    }

    grades.push_back(grade);
    return true;
}

// 성적 수정: 같은 (학번, 과목코드, 학기) 조합을 찾아 내용을 덮어쓴다.
// 값이 잘못됐거나, 없는 학생/과목이거나, 수정할 성적이 없으면 false.
bool GradeManager::updateGrade(const Grade& grade) {
    const Course* course = findCourse(grade.courseCode);
    if (course == nullptr || findStudent(grade.studentId) == nullptr || !gradeFitsCourse(grade, *course)) {
        return false;
    }

    // 수정할 대상 성적 찾기
    auto it = std::find_if(grades.begin(), grades.end(), [&](const Grade& current) {
        return current.studentId == grade.studentId &&
               current.courseCode == grade.courseCode &&
               current.semester == grade.semester;
    });
    if (it == grades.end()) {
        return false;
    }

    *it = grade;
    return true;
}

// 성적 삭제: (학번, 과목코드, 학기) 조합으로 찾아 지운다. 없으면 false.
bool GradeManager::removeGrade(const std::string& studentId, const std::string& courseCode,
                               const std::string& semester) {
    auto it = std::find_if(grades.begin(), grades.end(), [&](const Grade& grade) {
        return grade.studentId == studentId && grade.courseCode == courseCode &&
               grade.semester == semester;
    });
    if (it == grades.end()) {
        return false;
    }

    grades.erase(it);
    return true;
}

// 특정 학생의 성적 전부 (복사본 반환).
std::vector<Grade> GradeManager::getGradesByStudent(const std::string& studentId) const {
    std::vector<Grade> result;
    for (const Grade& grade : grades) {
        if (grade.studentId == studentId) {
            result.push_back(grade);
        }
    }
    return result;
}

// 특정 과목의 성적 전부 (복사본 반환).
std::vector<Grade> GradeManager::getGradesByCourse(const std::string& courseCode) const {
    std::vector<Grade> result;
    for (const Grade& grade : grades) {
        if (grade.courseCode == courseCode) {
            result.push_back(grade);
        }
    }
    return result;
}

// 특정 학기의 성적 전부 (복사본 반환).
std::vector<Grade> GradeManager::getGradesBySemester(const std::string& semester) const {
    std::vector<Grade> result;
    for (const Grade& grade : grades) {
        if (grade.semester == semester) {
            result.push_back(grade);
        }
    }
    return result;
}

// 전체 성적 목록 (읽기 전용 참조).
const std::vector<Grade>& GradeManager::getGrades() const { return grades; }

// ----- 수강신청 -----
// 수강신청 등록: 학기 형식이 틀렸거나, 없는 학생/과목이거나, 같은 조합이 이미 있으면 false.
// 정원·강의시간 겹침·신청 기간은 서버가 검사한다.
bool GradeManager::addEnrollment(const Enrollment& enrollment) {
    if (!isValidSemester(enrollment.semester) || findStudent(enrollment.studentId) == nullptr ||
        findCourse(enrollment.courseCode) == nullptr) {
        return false;
    }
    if (findEnrollment(enrollment.studentId, enrollment.courseCode, enrollment.semester) != nullptr) {
        return false;
    }
    enrollments.push_back(enrollment);
    // 상대평가 과목은 수강생 구성이 바뀌었으므로 전체 입력 완료 여부와 등급을 다시 확인한다.
    applyGradingMode(enrollment.courseCode, enrollment.semester);
    return true;
}

// 수강신청 삭제: (학번, 과목코드, 학기) 조합으로 찾아 지운다. 없으면 false.
bool GradeManager::removeEnrollment(const std::string& studentId, const std::string& courseCode,
                                    const std::string& semester) {
    auto it = std::find_if(enrollments.begin(), enrollments.end(), [&](const Enrollment& enrollment) {
        return enrollment.studentId == studentId && enrollment.courseCode == courseCode &&
               enrollment.semester == semester;
    });
    if (it == enrollments.end()) {
        return false;
    }

    enrollments.erase(it);
    // 수강생이 줄었으므로 남은 수강생 기준으로 상대평가를 다시 산출한다.
    applyGradingMode(courseCode, semester);
    return true;
}

// 수강신청 찾기: 있으면 벡터 원소의 주소, 없으면 nullptr.
const Enrollment* GradeManager::findEnrollment(const std::string& studentId, const std::string& courseCode,
                                               const std::string& semester) const {
    auto it = std::find_if(enrollments.begin(), enrollments.end(), [&](const Enrollment& enrollment) {
        return enrollment.studentId == studentId && enrollment.courseCode == courseCode &&
               enrollment.semester == semester;
    });
    return it == enrollments.end() ? nullptr : &*it;
}

// 전체 수강신청 목록 (읽기 전용 참조).
const std::vector<Enrollment>& GradeManager::getEnrollments() const { return enrollments; }

// 수강신청 학기 ("" 이면 수강신청 기간이 아님).
const std::string& GradeManager::getEnrollmentSemester() const { return enrollmentSemester; }

// 수강신청 학기 설정: "" 또는 올바른 학기 형식만 허용.
bool GradeManager::setEnrollmentSemester(const std::string& semester) {
    if (!semester.empty() && !isValidSemester(semester)) {
        return false;
    }
    enrollmentSemester = semester;
    return true;
}

// ----- 과목 게시물 -----
// 게시물 등록: 값이 잘못됐거나 없는 과목이면 0, 성공하면 새 id (현재 최대 id + 1, 처음이면 1).
// post.id 는 무시하고 새로 정한다.
int GradeManager::addPost(const CoursePost& post) {
    if (!post.isValid() || findCourse(post.courseCode) == nullptr) {
        return 0;
    }
    int maxId = 0;
    for (const CoursePost& current : posts) {
        maxId = std::max(maxId, current.id);
    }
    posts.push_back(post);
    posts.back().id = maxId + 1;
    return maxId + 1;
}

// 게시물 수정: 같은 id 의 게시물을 post 로 통째로 교체한다.
// 없는 id 이거나, 값이 잘못됐거나, 없는 과목이면 false.
bool GradeManager::updatePost(const CoursePost& post) {
    auto it = std::find_if(posts.begin(), posts.end(), [&](const CoursePost& current) {
        return current.id == post.id;
    });
    if (it == posts.end() || !post.isValid() || findCourse(post.courseCode) == nullptr) {
        return false;
    }
    *it = post;
    return true;
}

// 게시물 삭제: id 로 찾아 지운다. 없으면 false.
bool GradeManager::removePost(int id) {
    auto it = std::find_if(posts.begin(), posts.end(), [&](const CoursePost& current) {
        return current.id == id;
    });
    if (it == posts.end()) {
        return false;
    }
    posts.erase(it);
    return true;
}

// 게시물 찾기: 있으면 벡터 원소의 주소, 없으면 nullptr.
const CoursePost* GradeManager::findPost(int id) const {
    auto it = std::find_if(posts.begin(), posts.end(), [&](const CoursePost& current) {
        return current.id == id;
    });
    return it == posts.end() ? nullptr : &*it;
}

// 전체 게시물 목록 (읽기 전용 참조).
const std::vector<CoursePost>& GradeManager::getPosts() const { return posts; }

// ----- 평가 항목 -----
// 그 과목·학기에 항목이 1개 이상 있으면 true (= 성적을 항목 점수로 계산하는 과목).
bool GradeManager::hasAssessments(const std::string& courseCode, const std::string& semester) const {
    return std::any_of(assessments.begin(), assessments.end(), [&](const Assessment& item) {
        return item.courseCode == courseCode && item.semester == semester;
    });
}

// 그 과목·학기의 항목 전부 (복사본 반환, assessmentCategories() 순서).
std::vector<Assessment> GradeManager::getAssessments(const std::string& courseCode,
                                                     const std::string& semester) const {
    std::vector<Assessment> result;
    for (const std::string& category : assessmentCategories()) {
        for (const Assessment& item : assessments) {
            if (item.courseCode == courseCode && item.semester == semester && item.name == category) {
                result.push_back(item);
            }
        }
    }
    return result;
}

// 전체 항목 목록 (읽기 전용 참조).
const std::vector<Assessment>& GradeManager::getAllAssessments() const { return assessments; }

// 항목 추가: 형식 오류, 없는 과목, 같은 (과목, 학기, 이름) 중복, 추가 후 비율 합계 100 초과,
// 첫 항목인데 그 과목·학기에 이미 성적(직접 입력한 성적)이 있으면 false.
// 성공하면 그 과목·학기 수강생 전원의 성적을 다시 계산한다.
bool GradeManager::addAssessment(const Assessment& item) {
    if (!item.isValid() || findCourse(item.courseCode) == nullptr) {
        return false;
    }

    bool first = true;   // 그 과목·학기의 첫 항목인지
    int weightSum = 0;   // 지금 있는 항목들의 비율 합계
    for (const Assessment& current : assessments) {
        if (current.courseCode != item.courseCode || current.semester != item.semester) {
            continue;
        }
        if (current.name == item.name) {
            return false;
        }
        first = false;
        weightSum += current.weight;
    }
    if (weightSum + item.weight > 100) {
        return false;
    }
    // 직접 입력한 성적이 있는 과목·학기는 항목 계산 과목으로 바꿀 수 없음 (그 성적이 덮어써지거나 지워지므로)
    if (first) {
        const bool hasGrade = std::any_of(grades.begin(), grades.end(), [&](const Grade& grade) {
            return grade.courseCode == item.courseCode && grade.semester == item.semester;
        });
        if (hasGrade) {
            return false;
        }
    }

    assessments.push_back(item);
    syncCourse(item.courseCode, item.semester);
    return true;
}

// 항목 수정: (과목, 학기, name) 항목을 item 으로 바꾼다.
// 없음, 형식 오류, item 의 과목·학기가 인자와 다름(항목 이동은 지원 안 함), 다른 항목과 이름 중복,
// 자기 자신을 뺀 비율 합계 + 새 비율이 100 초과면 false.
// 이름이 바뀌면 그 항목의 점수에 적힌 이름도 같이 바꾸고, 수강생 전원의 성적을 다시 계산한다.
bool GradeManager::updateAssessment(const std::string& courseCode, const std::string& semester,
                                    const std::string& name, const Assessment& item) {
    // 인자가 assessments 원소를 가리킬 수도 있으므로 덮어쓰기 전에 복사해 둔다.
    const std::string code = courseCode;
    const std::string sem = semester;
    const std::string oldName = name;

    auto it = std::find_if(assessments.begin(), assessments.end(), [&](const Assessment& current) {
        return current.courseCode == code && current.semester == sem && current.name == oldName;
    });
    if (it == assessments.end() || !item.isValid() || item.courseCode != code || item.semester != sem) {
        return false;
    }

    int otherWeightSum = 0;  // 이 항목을 뺀 나머지 비율 합계
    for (auto other = assessments.begin(); other != assessments.end(); ++other) {
        if (other == it || other->courseCode != code || other->semester != sem) {
            continue;
        }
        if (other->name == item.name) {
            return false;
        }
        otherWeightSum += other->weight;
    }
    if (otherWeightSum + item.weight > 100) {
        return false;
    }

    if (item.name != oldName) {
        for (AssessmentScore& score : assessmentScores) {
            if (score.courseCode == code && score.semester == sem && score.name == oldName) {
                score.name = item.name;
            }
        }
    }
    *it = item;
    syncCourse(code, sem);
    return true;
}

// 항목 삭제: 항목과 그 항목의 점수를 지우고 수강생 전원의 성적을 다시 계산한다. 없으면 false.
// 마지막 항목이면 그 과목·학기의 항목 점수와 성적을 모두 지운다 (직접 입력 과목으로 돌아감).
bool GradeManager::removeAssessment(const std::string& courseCode, const std::string& semester,
                                    const std::string& name) {
    // 인자가 assessments 원소를 가리킬 수도 있으므로 지우기 전에 복사해 둔다.
    const std::string code = courseCode;
    const std::string sem = semester;
    const std::string itemName = name;

    auto it = std::find_if(assessments.begin(), assessments.end(), [&](const Assessment& item) {
        return item.courseCode == code && item.semester == sem && item.name == itemName;
    });
    if (it == assessments.end()) {
        return false;
    }

    assessments.erase(it);
    assessmentScores.erase(std::remove_if(assessmentScores.begin(), assessmentScores.end(),
                                          [&](const AssessmentScore& score) {
                                              return score.courseCode == code && score.semester == sem &&
                                                     score.name == itemName;
                                          }),
                           assessmentScores.end());

    if (hasAssessments(code, sem)) {
        syncCourse(code, sem);
        return true;
    }
    // 마지막 항목이었음: 남은 항목 점수와 항목으로 만든 성적을 모두 지운다.
    assessmentScores.erase(std::remove_if(assessmentScores.begin(), assessmentScores.end(),
                                          [&](const AssessmentScore& score) {
                                              return score.courseCode == code && score.semester == sem;
                                          }),
                           assessmentScores.end());
    grades.erase(std::remove_if(grades.begin(), grades.end(), [&](const Grade& grade) {
        return grade.courseCode == code && grade.semester == sem;
    }), grades.end());
    return true;
}

// ----- 항목 점수 -----
// 그 과목·학기의 항목 점수 전부 (복사본 반환).
// 출석 비율이 0 보다 크면 그 과목·학기 수강생마다 계산한 출석 점수를 뒤에 덧붙인다.
std::vector<AssessmentScore> GradeManager::getAssessmentScores(const std::string& courseCode,
                                                               const std::string& semester) const {
    std::vector<AssessmentScore> result;
    for (const AssessmentScore& score : assessmentScores) {
        if (score.courseCode == courseCode && score.semester == semester && score.name != kAttendanceCategory) {
            result.push_back(score);
        }
    }
    if (categoryWeight(assessments, courseCode, semester, kAttendanceCategory) > 0) {
        for (const Enrollment& enrollment : enrollments) {
            if (enrollment.courseCode == courseCode && enrollment.semester == semester) {
                result.push_back({enrollment.studentId, courseCode, semester, kAttendanceCategory,
                                  attendanceCategoryScore(enrollment.studentId, courseCode, semester)});
            }
        }
    }
    return result;
}

// 특정 학생의 항목 점수 전부 (복사본 반환).
// 그 학생의 수강신청 중 평가 기준이 있고 출석 비율이 0 보다 큰 (과목, 학기) 마다 출석 점수를 덧붙인다.
std::vector<AssessmentScore> GradeManager::getAssessmentScoresByStudent(const std::string& studentId) const {
    std::vector<AssessmentScore> result;
    for (const AssessmentScore& score : assessmentScores) {
        if (score.studentId == studentId && score.name != kAttendanceCategory) {
            result.push_back(score);
        }
    }
    for (const Enrollment& enrollment : enrollments) {
        if (enrollment.studentId == studentId &&
            categoryWeight(assessments, enrollment.courseCode, enrollment.semester, kAttendanceCategory) > 0) {
            result.push_back({studentId, enrollment.courseCode, enrollment.semester, kAttendanceCategory,
                              attendanceCategoryScore(studentId, enrollment.courseCode, enrollment.semester)});
        }
    }
    return result;
}

// 항목 점수 입력: 같은 (학번, 과목, 학기, 이름) 이 있으면 점수 수정, 없으면 추가.
// 형식 오류, 그 이름의 항목 없음, 같은 (학번, 과목, 학기) 수강신청 없음이면 false.
// 출석(출결에서 자동 계산)이거나 그 분류 비율이 0 이어도 false.
// 성공하면 그 학생의 성적을 다시 계산한다.
bool GradeManager::setAssessmentScore(const AssessmentScore& score) {
    if (!score.isValid() || score.name == kAttendanceCategory) {
        return false;
    }
    const bool itemCounts = categoryWeight(assessments, score.courseCode, score.semester, score.name) > 0;
    if (!itemCounts || findEnrollment(score.studentId, score.courseCode, score.semester) == nullptr) {
        return false;
    }

    auto it = std::find_if(assessmentScores.begin(), assessmentScores.end(), [&](const AssessmentScore& current) {
        return current.studentId == score.studentId && current.courseCode == score.courseCode &&
               current.semester == score.semester && current.name == score.name;
    });
    if (it != assessmentScores.end()) {
        it->score = score.score;
    } else {
        assessmentScores.push_back(score);
    }
    syncGrade(score.studentId, score.courseCode, score.semester);
    return true;
}

// 항목 점수 삭제: 지우고 그 학생의 성적을 다시 계산한다 (미완성이 되므로 성적은 지워짐). 없으면 false.
bool GradeManager::removeAssessmentScore(const std::string& studentId, const std::string& courseCode,
                                         const std::string& semester, const std::string& name) {
    // 인자가 assessmentScores 원소를 가리킬 수도 있으므로 지우기 전에 복사해 둔다.
    const std::string id = studentId;
    const std::string code = courseCode;
    const std::string sem = semester;

    auto it = std::find_if(assessmentScores.begin(), assessmentScores.end(), [&](const AssessmentScore& score) {
        return score.studentId == id && score.courseCode == code && score.semester == sem && score.name == name;
    });
    if (it == assessmentScores.end()) {
        return false;
    }

    assessmentScores.erase(it);
    syncGrade(id, code, sem);
    return true;
}

// ----- 출결 (#59) -----
// 출결 기록: 같은 (학번, 과목, 학기, 주차) 가 있으면 상태 수정, 없으면 추가.
// 형식 오류, 없는 학생·과목, 같은 (학번, 과목, 학기) 수강신청 없음이면 false.
// 출석 점수는 항목 점수로 저장하지 않으므로, 성공하면 그 학생의 성적만 다시 계산한다.
bool GradeManager::setAttendance(const Attendance& record) {
    if (!record.isValid() || findStudent(record.studentId) == nullptr ||
        findCourse(record.courseCode) == nullptr ||
        findEnrollment(record.studentId, record.courseCode, record.semester) == nullptr) {
        return false;
    }

    auto it = std::find_if(attendance.begin(), attendance.end(), [&](const Attendance& current) {
        return current.studentId == record.studentId && current.courseCode == record.courseCode &&
               current.semester == record.semester && current.week == record.week;
    });
    if (it != attendance.end()) {
        it->status = record.status;
    } else {
        attendance.push_back(record);
    }
    syncGrade(record.studentId, record.courseCode, record.semester);
    return true;
}

// 출결 기록 삭제: 지우고 그 학생의 성적을 다시 계산한다. 없으면 false.
bool GradeManager::removeAttendance(const std::string& studentId, const std::string& courseCode,
                                    const std::string& semester, int week) {
    // 인자가 attendance 원소를 가리킬 수도 있으므로 지우기 전에 복사해 둔다.
    const std::string id = studentId;
    const std::string code = courseCode;
    const std::string sem = semester;

    auto it = std::find_if(attendance.begin(), attendance.end(), [&](const Attendance& record) {
        return record.studentId == id && record.courseCode == code && record.semester == sem &&
               record.week == week;
    });
    if (it == attendance.end()) {
        return false;
    }

    attendance.erase(it);
    syncGrade(id, code, sem);
    return true;
}

// 그 과목·학기의 출결 기록 전부 (복사본 반환).
std::vector<Attendance> GradeManager::getAttendanceByCourse(const std::string& courseCode,
                                                            const std::string& semester) const {
    std::vector<Attendance> result;
    for (const Attendance& record : attendance) {
        if (record.courseCode == courseCode && record.semester == semester) {
            result.push_back(record);
        }
    }
    return result;
}

// 특정 학생의 출결 기록 전부 (복사본 반환).
std::vector<Attendance> GradeManager::getAttendanceByStudent(const std::string& studentId) const {
    std::vector<Attendance> result;
    for (const Attendance& record : attendance) {
        if (record.studentId == studentId) {
            result.push_back(record);
        }
    }
    return result;
}

// 성적 계산용 출석 점수: 그 학생·과목·학기 기록이 있으면 환산 점수, 하나도 없으면 100 (모두 출석).
// (summarizeAttendance 의 "기록 없음 = -1" 은 화면·API 요약용이라 여기서만 100 으로 바꾼다.)
double GradeManager::attendanceCategoryScore(const std::string& studentId, const std::string& courseCode,
                                             const std::string& semester) const {
    std::vector<Attendance> records;
    for (const Attendance& record : attendance) {
        if (record.studentId == studentId && record.courseCode == courseCode && record.semester == semester) {
            records.push_back(record);
        }
    }
    if (records.empty()) {
        return 100.0;
    }
    return summarizeAttendance(records).score;
}

// ----- 최종 성적 동기화 -----
// 항목이 없는 과목·학기(직접 입력 과목)는 건드리지 않는다.
// 점수 목록 = 저장된 그 학생 점수(출석 제외) + 출석 비율이 0 보다 크면 attendanceCategoryScore.
// finalScore 가 0 이상이면 grades 의 (학번, 과목, 학기) 성적을 추가하거나 점수를 바꾸고,
// -1(미완성)이면 그 성적이 있을 때 지운다. 끝에서 그 과목·학기 등급을 다시 정한다 (applyGradingMode).
void GradeManager::syncGrade(const std::string& studentId, const std::string& courseCode,
                             const std::string& semester) {
    const std::vector<Assessment> items = getAssessments(courseCode, semester);
    if (items.empty()) {
        return;
    }
    std::vector<AssessmentScore> scores;
    for (const AssessmentScore& score : assessmentScores) {
        if (score.studentId == studentId && score.courseCode == courseCode && score.semester == semester &&
            score.name != kAttendanceCategory) {
            scores.push_back(score);
        }
    }
    if (categoryWeight(assessments, courseCode, semester, kAttendanceCategory) > 0) {
        scores.push_back({studentId, courseCode, semester, kAttendanceCategory,
                          attendanceCategoryScore(studentId, courseCode, semester)});
    }
    const double result = finalScore(items, scores);

    auto it = std::find_if(grades.begin(), grades.end(), [&](const Grade& grade) {
        return grade.studentId == studentId && grade.courseCode == courseCode && grade.semester == semester;
    });
    if (result < 0) {
        if (it != grades.end()) {
            grades.erase(it);
        }
    } else if (it != grades.end()) {
        it->score = result;
    } else if (findStudent(studentId) != nullptr && findCourse(courseCode) != nullptr) {
        // addGrade 와 같은 조건: 없는 학생·과목의 성적은 만들지 않음 (깨진 CSV 로 수강신청만 남은 경우)
        Grade grade;
        grade.studentId = studentId;
        grade.courseCode = courseCode;
        grade.semester = semester;
        grade.score = result;
        grades.push_back(grade);
    }
    applyGradingMode(courseCode, semester);
}

// 그 과목·학기에 수강신청한 학생 전원의 성적을 다시 계산한다 (syncGrade 는 enrollments 를 바꾸지 않음).
void GradeManager::syncCourse(const std::string& courseCode, const std::string& semester) {
    for (const Enrollment& enrollment : enrollments) {
        if (enrollment.courseCode == courseCode && enrollment.semester == semester) {
            syncGrade(enrollment.studentId, courseCode, semester);
        }
    }
}

// 그 과목(모든 학기)에 성적 또는 평가 항목이 하나라도 있으면 true (성적 방식을 바꿀 수 있는지 판단)
bool GradeManager::courseHasGradesOrAssessments(const std::string& courseCode) const {
    return std::any_of(grades.begin(), grades.end(), [&](const Grade& g) { return g.courseCode == courseCode; }) ||
           std::any_of(assessments.begin(), assessments.end(),
                       [&](const Assessment& a) { return a.courseCode == courseCode; });
}

// ----- 평가 기준 -----
// 그 과목·학기의 등급 산출 방식. 기록이 없으면 절대평가.
std::string GradeManager::getGradingMode(const std::string& courseCode, const std::string& semester) const {
    for (const GradingMode& mode : gradingModes) {
        if (mode.courseCode == courseCode && mode.semester == semester) {
            return mode.mode;
        }
    }
    return kGradingAbsolute;
}


std::vector<int> GradeManager::getRelativeGradeRatios(const std::string& courseCode,
                                                       const std::string& semester) const {
    for (const RelativeGradeRatio& ratio : relativeGradeRatios) {
        if (ratio.courseCode == courseCode && ratio.semester == semester &&
            RelativeGrading::validRatios(ratio.ratios)) {
            return ratio.ratios;
        }
    }
    return RelativeGrading::defaultRatios();
}

// 평가 기준 만들기·바꾸기: 6개 평가 항목 비율(0 포함), 8개 상대평가 등급 비율과 등급 방식을 한 번에 저장한다.
// false: 없는 과목·P/F 과목, 학기 형식 오류, weights 크기 != 6, 0~100 밖, 합계 != 100, mode 가 두 값이 아님,
//        처음 만드는데 그 과목·학기에 이미 성적(직접 입력)이 있음
bool GradeManager::setGradingScheme(const std::string& courseCode, const std::string& semester,
                                    const std::vector<int>& weights, const std::string& mode,
                                    const std::vector<int>& gradeRatios) {
    // 인자가 멤버 원소를 가리킬 수도 있으므로 바꾸기 전에 복사해 둔다.
    const std::string code = courseCode;
    const std::string sem = semester;
    const std::string newMode = mode;
    const std::vector<std::string>& categories = assessmentCategories();

    const Course* course = findCourse(code);
    // P/F 과목에는 평가 기준을 만들지 않음 (성적은 P·NP 로 직접 입력)
    if (course == nullptr || course->isPassFail() || !isValidSemester(sem) || weights.size() != categories.size()) {
        return false;
    }
    int weightSum = 0;
    for (int weight : weights) {
        if (weight < 0 || weight > 100) {
            return false;
        }
        weightSum += weight;
    }
    if (weightSum != 100 || (newMode != kGradingAbsolute && newMode != kGradingRelative) ||
        !RelativeGrading::validRatios(gradeRatios)) {
        return false;
    }
    // 직접 입력한 성적이 있는 과목·학기는 항목 계산 과목으로 바꿀 수 없음 (그 성적이 덮어써지거나 지워지므로)
    if (!hasAssessments(code, sem)) {
        const bool hasGrade = std::any_of(grades.begin(), grades.end(), [&](const Grade& grade) {
            return grade.courseCode == code && grade.semester == sem;
        });
        if (hasGrade) {
            return false;
        }
    }

    // 1) 그 과목·학기 평가 항목 6개(0 포함)를 저장한다.
    assessments.erase(std::remove_if(assessments.begin(), assessments.end(),
                                     [&](const Assessment& item) {
                                         return item.courseCode == code && item.semester == sem;
                                     }),
                      assessments.end());
    for (size_t i = 0; i < categories.size(); i++) {
        assessments.push_back({code, sem, categories[i], weights[i]});
    }
    // 2) 비율이 0 인 분류의 항목 점수를 지운다 (출석은 저장하지 않으므로 남아 있던 출석 점수도 지움).
    assessmentScores.erase(std::remove_if(assessmentScores.begin(), assessmentScores.end(),
                                          [&](const AssessmentScore& score) {
                                              return score.courseCode == code && score.semester == sem &&
                                                     (score.name == kAttendanceCategory ||
                                                      categoryWeight(assessments, code, sem, score.name) <= 0);
                                          }),
                           assessmentScores.end());
    // 3) 등급 방식을 넣거나 바꾼다.
    auto it = std::find_if(gradingModes.begin(), gradingModes.end(), [&](const GradingMode& current) {
        return current.courseCode == code && current.semester == sem;
    });
    if (it != gradingModes.end()) {
        it->mode = newMode;
    } else {
        gradingModes.push_back({code, sem, newMode});
    }
    auto ratioIt = std::find_if(relativeGradeRatios.begin(), relativeGradeRatios.end(),
                                [&](const RelativeGradeRatio& current) {
                                    return current.courseCode == code && current.semester == sem;
                                });
    if (ratioIt != relativeGradeRatios.end()) {
        ratioIt->ratios = gradeRatios;
    } else {
        relativeGradeRatios.push_back({code, sem, gradeRatios});
    }
    // 4) 수강생 전원 재계산. 수강생이 0명이어도 등급은 한 번 다시 정한다.
    syncCourse(code, sem);
    applyGradingMode(code, sem);
    return true;
}

// 평가 기준 삭제: 항목·항목 점수·등급 방식·그 과목·학기 성적을 모두 지운다 (직접 입력 과목으로 돌아감).
// 평가 기준이 없으면 false.
bool GradeManager::removeGradingScheme(const std::string& courseCode, const std::string& semester) {
    const std::string code = courseCode;
    const std::string sem = semester;
    if (!hasAssessments(code, sem)) {
        return false;
    }
    assessments.erase(std::remove_if(assessments.begin(), assessments.end(),
                                     [&](const Assessment& item) {
                                         return item.courseCode == code && item.semester == sem;
                                     }),
                      assessments.end());
    assessmentScores.erase(std::remove_if(assessmentScores.begin(), assessmentScores.end(),
                                          [&](const AssessmentScore& score) {
                                              return score.courseCode == code && score.semester == sem;
                                          }),
                           assessmentScores.end());
    gradingModes.erase(std::remove_if(gradingModes.begin(), gradingModes.end(),
                                      [&](const GradingMode& current) {
                                          return current.courseCode == code && current.semester == sem;
                                      }),
                       gradingModes.end());
    relativeGradeRatios.erase(std::remove_if(relativeGradeRatios.begin(), relativeGradeRatios.end(),
                                             [&](const RelativeGradeRatio& ratio) {
                                                 return ratio.courseCode == code && ratio.semester == sem;
                                             }),
                              relativeGradeRatios.end());
    grades.erase(std::remove_if(grades.begin(), grades.end(), [&](const Grade& grade) {
        return grade.courseCode == code && grade.semester == sem;
    }), grades.end());
    return true;
}

// 그 과목·학기 성적 전부의 등급을 다시 정한다.
// 상대평가면 RelativeGrading::assignLetters 결과(grades 순서)를 assignedLetter 에 넣고, 아니면 비운다.
void GradeManager::applyGradingMode(const std::string& courseCode, const std::string& semester) {
    // P/F 과목은 assignedLetter 가 P·NP 그 자체라 건드리지 않음 (상대평가 대상도 아님)
    const Course* course = findCourse(courseCode);
    if (course != nullptr && course->isPassFail()) {
        return;
    }
    std::vector<Grade*> targets;
    for (Grade& grade : grades) {
        if (grade.courseCode == courseCode && grade.semester == semester) {
            targets.push_back(&grade);
        }
    }
    if (getGradingMode(courseCode, semester) != kGradingRelative) {
        for (Grade* grade : targets) {
            grade->assignedLetter.clear();
        }
        return;
    }
    // 상대평가는 해당 과목·학기 수강생 전원의 성적이 완성된 뒤 한 번에 산출한다.
    // grades 에는 항목 점수가 완성된 학생만 들어오므로 수강신청 인원과 비교한다.
    size_t enrolledCount = 0;
    bool allEnrolledStudentsGraded = true;
    for (const Enrollment& enrollment : enrollments) {
        if (enrollment.courseCode != courseCode || enrollment.semester != semester) {
            continue;
        }
        ++enrolledCount;
        const bool hasGrade = std::any_of(targets.begin(), targets.end(), [&](const Grade* grade) {
            return grade->studentId == enrollment.studentId;
        });
        if (!hasGrade) {
            allEnrolledStudentsGraded = false;
        }
    }

    if (enrolledCount == 0 || !allEnrolledStudentsGraded || targets.size() != enrolledCount) {
        // 아직 미완성인 동안에는 상대평가 등급을 확정하지 않는다.
        // assignedLetter 를 비우면 Grade::letter() 는 점수 기준 등급을 반환하므로,
        // API 응답에서도 아래의 완료 여부를 확인해 등급을 빈 문자열로 내려준다.
        for (Grade* grade : targets) {
            grade->assignedLetter.clear();
        }
        return;
    }

    std::vector<double> scores;
    for (const Grade* grade : targets) {
        scores.push_back(grade->score);
    }
    const std::vector<std::string> letters = RelativeGrading::assignLetters(
        scores, getRelativeGradeRatios(courseCode, semester));
    for (size_t i = 0; i < targets.size() && i < letters.size(); i++) {
        targets[i]->assignedLetter = letters[i];
    }
}
