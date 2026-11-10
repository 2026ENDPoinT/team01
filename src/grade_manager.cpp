#include "grade_manager.h"

GradeManager::GradeManager(const std::string& dataDir) : fileIO(dataDir) {}

bool GradeManager::load() {
    return fileIO.loadStudents(students) && fileIO.loadCourses(courses) && fileIO.loadGrades(grades);
}

bool GradeManager::save() {
    return fileIO.saveStudents(students) && fileIO.saveCourses(courses) && fileIO.saveGrades(grades);
}

// TODO(B): 아래 전부 구현

// ----- 학생 -----
bool GradeManager::addStudent(const Student&) { return false; }
bool GradeManager::updateStudent(const std::string&, const Student&) { return false; }
bool GradeManager::removeStudent(const std::string&) { return false; }
const Student* GradeManager::findStudent(const std::string&) const { return nullptr; }
std::vector<Student> GradeManager::searchStudents(const std::string&) const { return {}; }
const std::vector<Student>& GradeManager::getStudents() const { return students; }

// ----- 과목 -----
bool GradeManager::addCourse(const Course&) { return false; }
bool GradeManager::updateCourse(const std::string&, const Course&) { return false; }
bool GradeManager::removeCourse(const std::string&) { return false; }
const Course* GradeManager::findCourse(const std::string&) const { return nullptr; }
const std::vector<Course>& GradeManager::getCourses() const { return courses; }

// ----- 성적 -----
bool GradeManager::addGrade(const Grade&) { return false; }
bool GradeManager::updateGrade(const Grade&) { return false; }
bool GradeManager::removeGrade(const std::string&, const std::string&, const std::string&) { return false; }
std::vector<Grade> GradeManager::getGradesByStudent(const std::string&) const { return {}; }
std::vector<Grade> GradeManager::getGradesByCourse(const std::string&) const { return {}; }
std::vector<Grade> GradeManager::getGradesBySemester(const std::string&) const { return {}; }
const std::vector<Grade>& GradeManager::getGrades() const { return grades; }
