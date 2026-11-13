#include "grade_manager.h"

#include <algorithm>
#include <iostream>

// 생성자: CSV 파일이 있는 폴더 경로를 FileIO 에 넘겨 준다.
GradeManager::GradeManager(const std::string& dataDir) : fileIO(dataDir) {}

// CSV 8개(학생/과목/성적/수강신청/설정/게시물/평가 항목/항목 점수)를 모두 읽어 멤버에 채운다. 하나라도 실패하면 false.
bool GradeManager::load() {
    if (!(fileIO.loadStudents(students) && fileIO.loadCourses(courses) && fileIO.loadGrades(grades) &&
          fileIO.loadEnrollments(enrollments) && fileIO.loadSettings(enrollmentSemester) &&
          fileIO.loadPosts(posts) && fileIO.loadAssessments(assessments) &&
          fileIO.loadAssessmentScores(assessmentScores))) {
        return false;
    }
    // 신청 학기 값이 깨져 있으면 "신청 기간 아님" 으로 시작
    if (!enrollmentSemester.empty() && !isValidSemester(enrollmentSemester)) {
        std::cerr << "경고: settings.csv 의 수강신청 학기 \"" << enrollmentSemester
                  << "\" 가 올바르지 않아 비웁니다." << std::endl;
        enrollmentSemester.clear();
    }
    return true;
}

// 메모리의 학생/과목/성적/수강신청/설정/게시물/평가 항목/항목 점수를 CSV 8개에 모두 저장한다. 하나라도 실패하면 false.
bool GradeManager::save() {
    return fileIO.saveStudents(students) && fileIO.saveCourses(courses) && fileIO.saveGrades(grades) &&
           fileIO.saveEnrollments(enrollments) && fileIO.saveSettings(enrollmentSemester) &&
           fileIO.savePosts(posts) && fileIO.saveAssessments(assessments) &&
           fileIO.saveAssessmentScores(assessmentScores);
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
    if (!grade.isValid() || findStudent(grade.studentId) == nullptr ||
        findCourse(grade.courseCode) == nullptr) {
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
    if (!grade.isValid() || findStudent(grade.studentId) == nullptr ||
        findCourse(grade.courseCode) == nullptr) {
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

// 그 과목·학기의 항목 전부 (복사본 반환, 등록 순서).
std::vector<Assessment> GradeManager::getAssessments(const std::string& courseCode,
                                                     const std::string& semester) const {
    std::vector<Assessment> result;
    for (const Assessment& item : assessments) {
        if (item.courseCode == courseCode && item.semester == semester) {
            result.push_back(item);
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
std::vector<AssessmentScore> GradeManager::getAssessmentScores(const std::string& courseCode,
                                                               const std::string& semester) const {
    std::vector<AssessmentScore> result;
    for (const AssessmentScore& score : assessmentScores) {
        if (score.courseCode == courseCode && score.semester == semester) {
            result.push_back(score);
        }
    }
    return result;
}

// 특정 학생의 항목 점수 전부 (복사본 반환).
std::vector<AssessmentScore> GradeManager::getAssessmentScoresByStudent(const std::string& studentId) const {
    std::vector<AssessmentScore> result;
    for (const AssessmentScore& score : assessmentScores) {
        if (score.studentId == studentId) {
            result.push_back(score);
        }
    }
    return result;
}

// 항목 점수 입력: 같은 (학번, 과목, 학기, 이름) 이 있으면 점수 수정, 없으면 추가.
// 형식 오류, 그 이름의 항목 없음, 같은 (학번, 과목, 학기) 수강신청 없음이면 false.
// 성공하면 그 학생의 성적을 다시 계산한다.
bool GradeManager::setAssessmentScore(const AssessmentScore& score) {
    if (!score.isValid()) {
        return false;
    }
    const bool itemExists = std::any_of(assessments.begin(), assessments.end(), [&](const Assessment& item) {
        return item.courseCode == score.courseCode && item.semester == score.semester && item.name == score.name;
    });
    if (!itemExists || findEnrollment(score.studentId, score.courseCode, score.semester) == nullptr) {
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

// ----- 최종 성적 동기화 -----
// 항목이 없는 과목·학기(직접 입력 과목)는 건드리지 않는다.
// finalScore 가 0 이상이면 grades 의 (학번, 과목, 학기) 성적을 추가하거나 점수를 바꾸고,
// -1(미완성)이면 그 성적이 있을 때 지운다.
void GradeManager::syncGrade(const std::string& studentId, const std::string& courseCode,
                             const std::string& semester) {
    const std::vector<Assessment> items = getAssessments(courseCode, semester);
    if (items.empty()) {
        return;
    }
    std::vector<AssessmentScore> scores;
    for (const AssessmentScore& score : assessmentScores) {
        if (score.studentId == studentId && score.courseCode == courseCode && score.semester == semester) {
            scores.push_back(score);
        }
    }
    const double result = finalScore(items, scores);

    auto it = std::find_if(grades.begin(), grades.end(), [&](const Grade& grade) {
        return grade.studentId == studentId && grade.courseCode == courseCode && grade.semester == semester;
    });
    if (result < 0) {
        if (it != grades.end()) {
            grades.erase(it);
        }
        return;
    }
    if (it != grades.end()) {
        it->score = result;
        return;
    }
    // addGrade 와 같은 조건: 없는 학생·과목의 성적은 만들지 않음 (깨진 CSV 로 수강신청만 남은 경우)
    if (findStudent(studentId) == nullptr || findCourse(courseCode) == nullptr) {
        return;
    }
    Grade grade;
    grade.studentId = studentId;
    grade.courseCode = courseCode;
    grade.semester = semester;
    grade.score = result;
    grades.push_back(grade);
}

// 그 과목·학기에 수강신청한 학생 전원의 성적을 다시 계산한다 (syncGrade 는 enrollments 를 바꾸지 않음).
void GradeManager::syncCourse(const std::string& courseCode, const std::string& semester) {
    for (const Enrollment& enrollment : enrollments) {
        if (enrollment.courseCode == courseCode && enrollment.semester == semester) {
            syncGrade(enrollment.studentId, courseCode, semester);
        }
    }
}
