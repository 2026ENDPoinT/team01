// 담당: 조장 (Eno74)
// FileIO: 쉼표·큰따옴표가 든 값도 저장 → 불러오기 후 그대로, 파일이 없으면 빈 목록 (평가 항목·항목 점수·등급 방식 포함)
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
           a.score == b.score && a.assignedLetter == b.assignedLetter;
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
    Grade relative = makeGrade("24000001", "109401", "2026-1", 59.5);
    relative.assignedLetter = "D0";  // 상대평가로 정해진 등급
    return {
        makeGrade("24000001", "100101", "2026-1", 87.5),
        relative,
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
    CHECK(readFile(dir.path() / "grades.csv").rfind("studentId,courseCode,semester,score,letter\n", 0) == 0);
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

TEST_CASE(fileIO_assessments_roundtrip) {
    TempDir dir;
    Assessment midterm;
    midterm.courseCode = "109001";
    midterm.semester = "2026-2";
    midterm.name = "중간고사";
    midterm.weight = 30;
    Assessment homework = midterm;
    homework.name = "과제";
    homework.weight = 70;
    Assessment zero = midterm;
    zero.name = "팀프로젝트";
    zero.weight = 0;  // 0% 도 저장
    const std::vector<Assessment> items = {midterm, homework, zero};

    AssessmentScore first;
    first.studentId = "26101001";
    first.courseCode = "109001";
    first.semester = "2026-2";
    first.name = "중간고사";
    first.score = 87.5;
    AssessmentScore second = first;
    second.name = "과제";
    second.score = 0;
    AssessmentScore third = first;
    third.studentId = "26101002";
    third.score = 100;
    const std::vector<AssessmentScore> scores = {first, second, third};
    {
        FileIO io(dir.str());
        CHECK(io.saveAssessments(items));
        CHECK(io.saveAssessmentScores(scores));
    }

    CHECK(readFile(dir.path() / "assessments.csv").rfind("courseCode,semester,name,weight\n", 0) == 0);
    CHECK(readFile(dir.path() / "assessment_scores.csv").rfind("studentId,courseCode,semester,name,score\n", 0) == 0);

    FileIO io(dir.str());
    std::vector<Assessment> loadedItems;
    std::vector<AssessmentScore> loadedScores;
    CHECK(io.loadAssessments(loadedItems));
    CHECK(io.loadAssessmentScores(loadedScores));

    CHECK_EQ(loadedItems.size(), items.size());
    for (size_t i = 0; i < items.size() && i < loadedItems.size(); i++) {
        CHECK(loadedItems[i].courseCode == items[i].courseCode && loadedItems[i].semester == items[i].semester &&
              loadedItems[i].name == items[i].name && loadedItems[i].weight == items[i].weight);
    }
    CHECK_EQ(loadedScores.size(), scores.size());
    for (size_t i = 0; i < scores.size() && i < loadedScores.size(); i++) {
        CHECK(loadedScores[i].studentId == scores[i].studentId && loadedScores[i].courseCode == scores[i].courseCode &&
              loadedScores[i].semester == scores[i].semester && loadedScores[i].name == scores[i].name &&
              loadedScores[i].score == scores[i].score);
    }
}

TEST_CASE(fileIO_assessments_skip_broken_lines) {
    TempDir dir;
    {
        std::ofstream out(dir.path() / "assessments.csv", std::ios::binary);
        out << "courseCode,semester,name,weight\n"
            << "109001,2026-2,중간고사,30\n"
            << "109001,2026-2,기말고사,abc\n"  // 숫자 아님
            << "109001,2026-2,과제,101\n"      // 비율 범위 밖
            << "109001,2026-2,퀴즈,10\n"       // 6분류가 아님
            << "109001,2026-2,중간고사,40\n"   // 같은 이름 중복
            << "109001,2026-2,기타,0\n"        // 0% 는 정상
            << "109001,2026-2\n";              // 칸 부족
    }
    {
        std::ofstream out(dir.path() / "assessment_scores.csv", std::ios::binary);
        out << "studentId,courseCode,semester,name,score\n"
            << "26101001,109001,2026-2,중간고사,90\n"
            << "26101001,109001,2026-2,기말고사,101\n"  // 점수 범위 밖
            << "26101001,109001,2026-2,과제,nan\n"      // NaN
            << "26101001,109001,2026-2,중간,50\n"       // 6분류가 아님
            << "2610100,109001,2026-2,중간고사,50\n";   // 학번 7자리
    }

    FileIO io(dir.str());
    std::vector<Assessment> items;
    std::vector<AssessmentScore> scores;
    CHECK(io.loadAssessments(items));
    CHECK(io.loadAssessmentScores(scores));
    CHECK_EQ(items.size(), static_cast<size_t>(2));
    CHECK(items.size() == 2 && items[0].name == "중간고사" && items[0].weight == 30 && items[1].name == "기타" &&
          items[1].weight == 0);
    CHECK_EQ(scores.size(), static_cast<size_t>(1));
    CHECK(!scores.empty() && scores[0].score == 90);

    // 파일이 없으면 빈 목록
    FileIO missing((dir.path() / "missing").string());
    CHECK(missing.loadAssessments(items));
    CHECK(missing.loadAssessmentScores(scores));
    CHECK(items.empty());
    CHECK(scores.empty());
}

// ----- 출결 (#59) -----

TEST_CASE(fileIO_attendance_roundtrip) {
    TempDir dir;
    Attendance first;
    first.studentId = "25103008";
    first.courseCode = "109201";
    first.semester = "2026-2";
    first.week = 1;
    first.status = "결석";
    Attendance second = first;
    second.week = 16;
    second.status = "공결";
    Attendance third = first;
    third.studentId = "25103006";
    third.status = "지각";
    const std::vector<Attendance> records = {first, second, third};
    {
        FileIO io(dir.str());
        CHECK(io.saveAttendance(records));
    }
    CHECK(readFile(dir.path() / "attendance.csv").rfind("studentId,courseCode,semester,week,status\n", 0) == 0);

    FileIO io(dir.str());
    std::vector<Attendance> loaded;
    CHECK(io.loadAttendance(loaded));
    CHECK_EQ(loaded.size(), records.size());
    for (size_t i = 0; i < records.size() && i < loaded.size(); i++) {
        CHECK(loaded[i].studentId == records[i].studentId && loaded[i].courseCode == records[i].courseCode &&
              loaded[i].semester == records[i].semester && loaded[i].week == records[i].week &&
              loaded[i].status == records[i].status);
    }

    // 다시 저장하면 이전 파일이 .bak 으로 남음
    CHECK(io.saveAttendance({first}));
    CHECK(std::filesystem::exists(dir.path() / "attendance.csv.bak"));
}

TEST_CASE(fileIO_attendance_skip_broken_lines) {
    TempDir dir;
    {
        std::ofstream out(dir.path() / "attendance.csv", std::ios::binary);
        out << "studentId,courseCode,semester,week,status\n"
            << "25103008,109201,2026-2,1,결석\n"
            << "25103008,109201,2026-2,abc,출석\n"  // 숫자 아님
            << "25103008,109201,2026-2,17,출석\n"   // 주차 범위 밖
            << "25103008,109201,2026-2,2,조퇴\n"    // 알 수 없는 상태
            << "25103008,109201,2026-2,1,출석\n"    // 같은 주차 중복
            << "2510300,109201,2026-2,3,출석\n"     // 학번 7자리
            << "25103008,109201,2026-2\n";          // 칸 부족
    }

    FileIO io(dir.str());
    std::vector<Attendance> records;
    CHECK(io.loadAttendance(records));
    CHECK_EQ(records.size(), static_cast<size_t>(1));
    CHECK(!records.empty() && records[0].week == 1 && records[0].status == "결석");

    // 파일이 없으면 빈 목록
    FileIO missing((dir.path() / "missing").string());
    CHECK(missing.loadAttendance(records));
    CHECK(records.empty());
}

// ----- 평가 기준 개편 (#66): grades.csv letter 칸, grading_modes.csv -----

TEST_CASE(fileIO_grades_read_old_four_columns_and_skip_bad_letter) {
    TempDir dir;
    {
        std::ofstream out(dir.path() / "grades.csv", std::ios::binary);
        out << "studentId,courseCode,semester,score\n"  // 예전 4칸 헤더
            << "24000001,100101,2026-1,87.5\n";
    }
    FileIO io(dir.str());
    std::vector<Grade> grades;
    CHECK(io.loadGrades(grades));
    CHECK(grades.size() == 1 && grades[0].score == 87.5 && grades[0].assignedLetter.empty());

    {
        std::ofstream out(dir.path() / "grades.csv", std::ios::binary);
        out << "studentId,courseCode,semester,score,letter\n"
            << "24000001,100101,2026-1,87.5,\n"     // letter 없음 = 점수 기준
            << "24000002,100101,2026-1,79.35,C+\n"
            << "24000003,100101,2026-1,70,E\n"      // 9개 등급이 아님
            << "24000004,100101,2026-1,70,a+\n"     // 대소문자 다름
            << "24000005,100101,2026-1,70,F\n";
    }
    CHECK(io.loadGrades(grades));
    CHECK_EQ(grades.size(), static_cast<size_t>(3));
    CHECK(grades.size() == 3 && grades[0].assignedLetter.empty() && grades[1].assignedLetter == "C+" &&
          grades[2].studentId == "24000005" && grades[2].assignedLetter == "F");
}

TEST_CASE(fileIO_grading_modes_roundtrip_and_skip_broken_lines) {
    TempDir dir;
    GradingMode absolute;
    absolute.courseCode = "109201";
    absolute.semester = "2026-2";
    absolute.mode = kGradingAbsolute;
    GradingMode relative = absolute;
    relative.courseCode = "109001";
    relative.mode = kGradingRelative;
    {
        FileIO io(dir.str());
        CHECK(io.saveGradingModes({absolute, relative}));
    }
    CHECK(readFile(dir.path() / "grading_modes.csv") ==
          "courseCode,semester,mode\n109201,2026-2,absolute\n109001,2026-2,relative\n");

    FileIO io(dir.str());
    std::vector<GradingMode> modes;
    CHECK(io.loadGradingModes(modes));
    CHECK(modes.size() == 2 && modes[0].courseCode == "109201" && modes[0].mode == kGradingAbsolute &&
          modes[1].courseCode == "109001" && modes[1].mode == kGradingRelative);

    // 다시 저장하면 이전 파일이 .bak 으로 남음
    CHECK(io.saveGradingModes({absolute}));
    CHECK(std::filesystem::exists(dir.path() / "grading_modes.csv.bak"));

    {
        std::ofstream out(dir.path() / "grading_modes.csv", std::ios::binary);
        out << "courseCode,semester,mode\n"
            << "109201,2026-2,absolute\n"
            << "109201,2026-2,relative\n"   // 같은 (과목, 학기) 중복
            << "109001,2026-2,rel\n"        // 모르는 방식
            << "10900,2026-2,absolute\n"    // 과목코드 5자리
            << "109001,2026-9,absolute\n"   // 학기 형식
            << "109001,2026-2\n";           // 칸 부족
    }
    CHECK(io.loadGradingModes(modes));
    CHECK(modes.size() == 1 && modes[0].mode == kGradingAbsolute);

    // 파일이 없으면 빈 목록
    FileIO missing((dir.path() / "missing").string());
    CHECK(missing.loadGradingModes(modes));
    CHECK(modes.empty());
}
