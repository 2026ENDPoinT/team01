// 담당: 조장 (Eno74)
// FileIO: 쉼표·큰따옴표가 든 값도 저장 → 불러오기 후 그대로, 파일이 없으면 빈 목록
//   모두 임시 폴더에서만 읽고 씀 (실제 data/ 는 건드리지 않음)
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "file_io.h"
#include "grade_manager.h"
#include "test_support.h"

namespace {

bool sameStudent(const Student& a, const Student& b) {
    return a.id == b.id && a.name == b.name && a.major == b.major && a.year == b.year;
}

bool sameCourse(const Course& a, const Course& b) {
    return a.code == b.code && a.name == b.name && a.credit == b.credit && a.professor == b.professor &&
           a.category == b.category && a.area == b.area && a.schedule == b.schedule && a.language == b.language &&
           a.capacity == b.capacity && a.remote == b.remote;
}

bool sameGrade(const Grade& a, const Grade& b) {
    return a.studentId == b.studentId && a.courseCode == b.courseCode && a.semester == b.semester &&
           a.score == b.score;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::vector<Student> sampleStudents() {
    Student quoted = makeStudent("24000001", "홍\"길동\"", 2);
    quoted.major = "컴퓨터공학,AI";
    Student formula = makeStudent("17000002", "=SUM(1)", 4);  // data/ 에는 원래 값 그대로 저장
    formula.major = "";
    return {quoted, formula};
}

std::vector<Course> sampleCourses() {
    Course calc = makeCourse("100101", "미적분학", 3);
    calc.professor = "최정훈, 한수진";
    calc.category = "전공필수";
    calc.area = "자연과학";
    calc.schedule = "화(6~9) 목(6~7)";
    calc.language = "영어전용";
    calc.capacity = 30;
    Course quoted = makeCourse("109401", "캡스톤 \"디자인\", 심화", 4);
    quoted.professor = "\"";
    quoted.remote = true;
    quoted.language = "모듈강좌(M)";
    return {calc, quoted};
}

std::vector<Grade> sampleGrades() {
    return {
        makeGrade("24000001", "100101", "2026-1", 87.5),
        makeGrade("24000001", "109401", "2026-1", 59.5),
        makeGrade("17000002", "100101", "2025-2", 100),
        makeGrade("17000002", "109401", "2025-2", 0),
    };
}

}  // namespace

TEST_CASE(joinCsvLine_quotes_comma_and_quote_only) {
    CHECK_EQ(joinCsvLine({"a", "b,c", "d\"e", ""}), std::string("a,\"b,c\",\"d\"\"e\",\n"));
    CHECK_EQ(joinCsvLine({"=SUM(1)", "-1", "+1", "@x"}), std::string("=SUM(1),-1,+1,@x\n"));  // 원래 값 유지
    CHECK_EQ(joinCsvLine({}), std::string("\n"));
}

TEST_CASE(fileIO_roundtrip_keeps_special_values) {
    TempDir dir;
    const std::vector<Student> students = sampleStudents();
    const std::vector<Course> courses = sampleCourses();
    const std::vector<Grade> grades = sampleGrades();
    {
        FileIO io(dir.str());
        CHECK(io.saveStudents(students));
        CHECK(io.saveCourses(courses));
        CHECK(io.saveGrades(grades));
    }

    // 쉼표 든 교수명은 따옴표로 감싸서 저장
    CHECK(readFile(dir.path() / "courses.csv").find("\"최정훈, 한수진\"") != std::string::npos);

    FileIO io(dir.str());
    std::vector<Student> loadedStudents;
    std::vector<Course> loadedCourses;
    std::vector<Grade> loadedGrades;
    CHECK(io.loadStudents(loadedStudents));
    CHECK(io.loadCourses(loadedCourses));
    CHECK(io.loadGrades(loadedGrades));

    CHECK_EQ(loadedStudents.size(), students.size());
    for (size_t i = 0; i < students.size() && i < loadedStudents.size(); i++) {
        CHECK(sameStudent(loadedStudents[i], students[i]));
    }
    CHECK_EQ(loadedCourses.size(), courses.size());
    for (size_t i = 0; i < courses.size() && i < loadedCourses.size(); i++) {
        CHECK(sameCourse(loadedCourses[i], courses[i]));
    }
    CHECK_EQ(loadedGrades.size(), grades.size());
    for (size_t i = 0; i < grades.size() && i < loadedGrades.size(); i++) {
        CHECK(sameGrade(loadedGrades[i], grades[i]));
    }
}

TEST_CASE(fileIO_missing_files_load_empty) {
    TempDir dir;
    FileIO io((dir.path() / "missing").string());  // 만들지 않은 폴더
    // 미리 값을 넣어 두고, 불러온 뒤 비워졌는지 확인
    std::vector<Student> students = sampleStudents();
    std::vector<Course> courses = sampleCourses();
    std::vector<Grade> grades = sampleGrades();
    CHECK(io.loadStudents(students));
    CHECK(io.loadCourses(courses));
    CHECK(io.loadGrades(grades));
    CHECK(students.empty());
    CHECK(courses.empty());
    CHECK(grades.empty());
}

TEST_CASE(gradeManager_save_load_roundtrip) {
    TempDir dir;
    {
        GradeManager gm(dir.str());
        CHECK(gm.load());  // 빈 폴더 → 빈 상태
        CHECK(gm.getStudents().empty());
        for (const Student& s : sampleStudents()) {
            CHECK(gm.addStudent(s));
        }
        for (const Course& c : sampleCourses()) {
            CHECK(gm.addCourse(c));
        }
        for (const Grade& g : sampleGrades()) {
            CHECK(gm.addGrade(g));
        }
        CHECK(gm.save());
    }

    GradeManager loaded(dir.str());
    CHECK(loaded.load());
    CHECK_EQ(loaded.getStudents().size(), static_cast<size_t>(2));
    CHECK_EQ(loaded.getCourses().size(), static_cast<size_t>(2));
    CHECK_EQ(loaded.getGrades().size(), static_cast<size_t>(4));
    const Course* calc = loaded.findCourse("100101");
    CHECK(calc != nullptr && calc->professor == "최정훈, 한수진");
    const Student* quoted = loaded.findStudent("24000001");
    CHECK(quoted != nullptr && quoted->name == "홍\"길동\"" && quoted->major == "컴퓨터공학,AI");
}
