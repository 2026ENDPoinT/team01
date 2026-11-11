#include "grade_manager.h"

#include <algorithm>

// 생성자: CSV 파일이 있는 폴더 경로를 FileIO 에 넘겨 준다.
GradeManager::GradeManager(const std::string& dataDir) : fileIO(dataDir) {}

// CSV 3개(학생/과목/성적)를 모두 읽어 멤버 벡터에 채운다. 하나라도 실패하면 false.
bool GradeManager::load() {
    return fileIO.loadStudents(students) && fileIO.loadCourses(courses) && fileIO.loadGrades(grades);
}

// 메모리의 학생/과목/성적을 CSV 3개에 모두 저장한다. 하나라도 실패하면 false.
bool GradeManager::save() {
    return fileIO.saveStudents(students) && fileIO.saveCourses(courses) && fileIO.saveGrades(grades);
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

    // 학번이 바뀌면 그 학생의 성적에 적힌 학번도 같이 바꿔서 연결이 끊기지 않게 한다.
    if (student.id != id) {
        for (Grade& grade : grades) {
            if (grade.studentId == id) {
                grade.studentId = student.id;
            }
        }
    }
    // 모든 검사를 통과한 뒤에 마지막으로 덮어쓴다.
    *it = student;
    return true;
}

// 학생 삭제: 학생과 그 학생의 성적을 함께 지운다. 대상이 없으면 false.
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

// 과목 수정: updateStudent 와 같은 규칙. 과목코드가 바뀌면 성적의 과목코드도 같이 바꾼다.
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

    // 과목코드가 바뀌면 그 과목의 성적에 적힌 코드도 같이 바꾼다.
    if (course.code != code) {
        for (Grade& grade : grades) {
            if (grade.courseCode == code) {
                grade.courseCode = course.code;
            }
        }
    }
    // 모든 검사를 통과한 뒤에 마지막으로 덮어쓴다.
    *it = course;
    return true;
}

// 과목 삭제: 과목과 그 과목의 성적을 함께 지운다. 대상이 없으면 false.
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
