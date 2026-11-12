#include "grade_manager.h"

#include <algorithm>
#include <ctime>

namespace {

bool parseSemester(const std::string& semester, int& year, int& term) {
    if (semester.size() != 6 || semester[4] != '-') {
        return false;
    }

    for (size_t i = 0; i < semester.size(); ++i) {
        if (i != 4 && (semester[i] < '0' || semester[i] > '9')) {
            return false;
        }
    }

    year = (semester[0] - '0') * 1000 + (semester[1] - '0') * 100 +
           (semester[2] - '0') * 10 + (semester[3] - '0');
    term = semester[5] - '0';
    return year > 0 && term >= 1 && term <= 4;
}

bool parseAdmissionYear(const std::string& studentId, int& year) {
    if (studentId.size() < 2 || studentId[0] < '0' || studentId[0] > '9' ||
        studentId[1] < '0' || studentId[1] > '9') {
        return false;
    }

    year = 2000 + (studentId[0] - '0') * 10 + (studentId[1] - '0');
    return true;
}

int termOrder(int term) {
    switch (term) {
        case 1: return 0;
        case 3: return 1;
        case 2: return 2;
        default: return 3;
    }
}

bool currentSemester(int& year, int& term) {
    const std::time_t now = std::time(nullptr);
    std::tm localTime{};
#ifdef _WIN32
    if (localtime_s(&localTime, &now) != 0) {
        return false;
    }
#else
    if (localtime_r(&now, &localTime) == nullptr) {
        return false;
    }
#endif

    const int calendarYear = localTime.tm_year + 1900;
    switch (localTime.tm_mon + 1) {
        case 1:
        case 2:
            year = calendarYear - 1;
            term = 4;
            break;
        case 3:
        case 4:
        case 5:
        case 6:
            year = calendarYear;
            term = 1;
            break;
        case 7:
        case 8:
            year = calendarYear;
            term = 3;
            break;
        case 9:
        case 10:
        case 11:
            year = calendarYear;
            term = 2;
            break;
        default:
            year = calendarYear;
            term = 4;
            break;
    }
    return true;
}

}  // namespace

GradeManager::GradeManager(const std::string& dataDir) : fileIO(dataDir) {}

bool GradeManager::load() {
    return fileIO.loadStudents(students) && fileIO.loadCourses(courses) && fileIO.loadGrades(grades);
}

bool GradeManager::save() {
    return fileIO.saveStudents(students) && fileIO.saveCourses(courses) && fileIO.saveGrades(grades);
}

// ----- 학생 -----
bool GradeManager::addStudent(const Student& student) {
    if (!student.isValid() || findStudent(student.id) != nullptr) {
        return false;
    }
    students.push_back(student);
    return true;
}

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

    if (student.id != id) {
        for (Grade& grade : grades) {
            if (grade.studentId == id) {
                grade.studentId = student.id;
            }
        }
    }
    *it = student;
    return true;
}

bool GradeManager::removeStudent(const std::string& id) {
    auto it = std::find_if(students.begin(), students.end(), [&](const Student& student) {
        return student.id == id;
    });
    if (it == students.end()) {
        return false;
    }

    students.erase(it);
    grades.erase(std::remove_if(grades.begin(), grades.end(), [&](const Grade& grade) {
        return grade.studentId == id;
    }), grades.end());
    return true;
}

const Student* GradeManager::findStudent(const std::string& id) const {
    auto it = std::find_if(students.begin(), students.end(), [&](const Student& student) {
        return student.id == id;
    });
    return it == students.end() ? nullptr : &*it;
}

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

const std::vector<Student>& GradeManager::getStudents() const { return students; }

// ----- 과목 -----
bool GradeManager::addCourse(const Course& course) {
    if (!course.isValid() || findCourse(course.code) != nullptr) {
        return false;
    }
    courses.push_back(course);
    return true;
}

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

    if (course.code != code) {
        for (Grade& grade : grades) {
            if (grade.courseCode == code) {
                grade.courseCode = course.code;
            }
        }
    }
    *it = course;
    return true;
}

bool GradeManager::removeCourse(const std::string& code) {
    auto it = std::find_if(courses.begin(), courses.end(), [&](const Course& course) {
        return course.code == code;
    });
    if (it == courses.end()) {
        return false;
    }

    courses.erase(it);
    grades.erase(std::remove_if(grades.begin(), grades.end(), [&](const Grade& grade) {
        return grade.courseCode == code;
    }), grades.end());
    return true;
}

const Course* GradeManager::findCourse(const std::string& code) const {
    auto it = std::find_if(courses.begin(), courses.end(), [&](const Course& course) {
        return course.code == code;
    });
    return it == courses.end() ? nullptr : &*it;
}

const std::vector<Course>& GradeManager::getCourses() const { return courses; }

// ----- 성적 -----
namespace {

std::string validateGradeSemester(const Student* student, const std::string& semester) {
    if (student == nullptr) {
        return "학생을 찾을 수 없습니다.";
    }

    int admissionYear = 0;
    if (!parseAdmissionYear(student->id, admissionYear)) {
        return "학번 형식이 올바르지 않습니다.";
    }

    int semesterYear = 0;
    int semesterTerm = 0;
    if (!parseSemester(semester, semesterYear, semesterTerm)) {
        return "학기 형식이 올바르지 않습니다.";
    }
    if (semesterYear < admissionYear) {
        return "입학 전 학기의 성적은 등록하거나 수정할 수 없습니다.";
    }

    int currentYear = 0;
    int currentTerm = 0;
    if (!currentSemester(currentYear, currentTerm)) {
        return "현재 학기를 확인할 수 없습니다.";
    }

    const int submittedOrder = semesterYear * 4 + termOrder(semesterTerm);
    const int currentOrder = currentYear * 4 + termOrder(currentTerm);
    if (submittedOrder > currentOrder) {
        return "미래 학기의 성적은 등록하거나 수정할 수 없습니다.";
    }

    return "";
}

bool fail(GradeError* error, GradeError::Kind kind, const std::string& message) {
    if (error != nullptr) {
        error->kind = kind;
        error->message = message;
    }
    return false;
}

// 등록·수정 공통 검사. 학기 검사를 isValid 보다 먼저 해서 학기 문제면 그 이유를 돌려줌
bool checkGrade(const Student* student, const Course* course, const Grade& grade, GradeError* error) {
    if (student == nullptr) {
        return fail(error, GradeError::NotFound, "학생을 찾을 수 없습니다.");
    }
    if (course == nullptr) {
        return fail(error, GradeError::NotFound, "과목을 찾을 수 없습니다.");
    }
    const std::string semesterError = validateGradeSemester(student, grade.semester);
    if (!semesterError.empty()) {
        return fail(error, GradeError::Invalid, semesterError);
    }
    if (!grade.isValid()) {
        return fail(error, GradeError::Invalid, "성적 값이 올바르지 않습니다.");
    }
    return true;
}

}  // namespace

bool GradeManager::addGrade(const Grade& grade, GradeError* error) {
    if (!checkGrade(findStudent(grade.studentId), findCourse(grade.courseCode), grade, error)) {
        return false;
    }

    const bool duplicate = std::any_of(grades.begin(), grades.end(), [&](const Grade& current) {
        return current.studentId == grade.studentId &&
               current.courseCode == grade.courseCode &&
               current.semester == grade.semester;
    });
    if (duplicate) {
        return fail(error, GradeError::Duplicate, "같은 학기에 이미 등록된 성적이 있습니다.");
    }

    grades.push_back(grade);
    return true;
}

bool GradeManager::updateGrade(const Grade& grade, GradeError* error) {
    if (!checkGrade(findStudent(grade.studentId), findCourse(grade.courseCode), grade, error)) {
        return false;
    }

    auto it = std::find_if(grades.begin(), grades.end(), [&](const Grade& current) {
        return current.studentId == grade.studentId &&
               current.courseCode == grade.courseCode &&
               current.semester == grade.semester;
    });
    if (it == grades.end()) {
        return fail(error, GradeError::NotFound, "수정할 성적을 찾을 수 없습니다.");
    }

    *it = grade;
    return true;
}

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

std::vector<Grade> GradeManager::getGradesByStudent(const std::string& studentId) const {
    std::vector<Grade> result;
    for (const Grade& grade : grades) {
        if (grade.studentId == studentId) {
            result.push_back(grade);
        }
    }
    return result;
}

std::vector<Grade> GradeManager::getGradesByCourse(const std::string& courseCode) const {
    std::vector<Grade> result;
    for (const Grade& grade : grades) {
        if (grade.courseCode == courseCode) {
            result.push_back(grade);
        }
    }
    return result;
}

std::vector<Grade> GradeManager::getGradesBySemester(const std::string& semester) const {
    std::vector<Grade> result;
    for (const Grade& grade : grades) {
        if (grade.semester == semester) {
            result.push_back(grade);
        }
    }
    return result;
}

const std::vector<Grade>& GradeManager::getGrades() const { return grades; }
