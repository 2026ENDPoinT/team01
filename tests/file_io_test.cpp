// FileIO 만 따로 실행해 보는 테스트
//   - CLion 실행 대상에서 FileIOTest 를 고르고 Run / Debug
//   - 궁금한 줄에 중단점을 찍고 Debug 로 실행하면 변수 값을 한 줄씩 볼 수 있음
//   - 테스트 데이터는 빌드 폴더 안 test_data/ 에 만들어짐 (진짜 data/ 는 건드리지 않음)
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "file_io.h"

namespace fs = std::filesystem;

int failCount = 0;

// 조건이 맞으면 [통과], 틀리면 [실패] 출력
void check(bool condition, const std::string& message) {
    if (condition) {
        std::cout << "[통과] " << message << std::endl;
    } else {
        std::cout << "[실패] " << message << std::endl;
        failCount++;
    }
}

// 파일 내용을 통째로 문자열로 읽음 (CSV 가 실제로 어떻게 저장됐는지 보기 위해)
std::string readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::string content;
    std::string line;
    while (std::getline(in, line)) {
        content += line + "\n";
    }
    return content;
}

int main() {
    const std::string dir = TEST_DATA_DIR;  // CMakeLists.txt 에서 넘겨준 경로
    fs::remove_all(dir);                    // 이전 실행 결과 지우고 새로 시작
    FileIO io(dir);

    // 1. 파일이 없을 때 불러오기 → 빈 목록, true
    {
        std::vector<Student> students;
        bool ok = io.loadStudents(students);
        check(ok && students.empty(), "파일이 없으면 빈 목록으로 시작");
    }

    // 2. 쉼표, 큰따옴표가 든 값 저장 → 다시 읽기
    {
        std::vector<Student> students = {
            {"24101234", "홍길동", "컴퓨터공학,AI", 2},  // 쉼표
            {"24101235", "Kim \"K\"", "수학", 1},        // 큰따옴표
        };
        check(io.saveStudents(students), "학생 저장");

        std::cout << "--- students.csv 내용 ---\n"
                  << readWholeFile(dir + "/students.csv") << "-------------------------" << std::endl;

        std::vector<Student> loaded;
        check(io.loadStudents(loaded), "학생 불러오기");
        check(loaded.size() == 2, "학생 2명");
        if (loaded.size() == 2) {
            check(loaded[0].major == "컴퓨터공학,AI", "쉼표가 든 값이 그대로 돌아옴");
            check(loaded[1].name == "Kim \"K\"", "큰따옴표가 든 값이 그대로 돌아옴");
            check(loaded[0].year == 2, "학년(숫자) 그대로 돌아옴");
        }
    }

    // 3. 두 번째 저장부터 .bak 백업이 생기고, .tmp 는 남지 않음
    {
        std::vector<Student> students = {{"24101234", "홍길동", "컴퓨터공학", 3}};
        check(io.saveStudents(students), "학생 다시 저장");
        check(fs::exists(dir + "/students.csv.bak"), ".bak 백업 파일 생김");
        check(!fs::exists(dir + "/students.csv.tmp"), ".tmp 임시 파일은 남지 않음");
    }

    // 4. 과목 저장 → 다시 읽기
    {
        std::vector<Course> courses = {{"CS101", "자료구조", 3, "이교수"}};
        check(io.saveCourses(courses), "과목 저장");

        std::vector<Course> loaded;
        check(io.loadCourses(loaded) && loaded.size() == 1, "과목 1개 불러오기");
        if (loaded.size() == 1) {
            check(loaded[0].name == "자료구조" && loaded[0].credit == 3, "과목 내용 그대로 돌아옴");
        }
    }

    // 5. 깨진 줄이 섞여 있으면 그 줄만 건너뜀 (경고 메시지가 출력되는 게 정상)
    {
        std::vector<Grade> grades = {{"24101234", "CS101", "2026-2", 87.5}};
        check(io.saveGrades(grades), "성적 저장");

        // 저장된 파일 끝에 일부러 이상한 줄을 붙임
        std::ofstream out(dir + "/grades.csv", std::ios::app | std::ios::binary);
        out << "bad,line\n";                        // 칸이 2개뿐
        out << "24101235,CS101,2026-2,abc\n";       // 점수가 숫자가 아님
        out << "24101236,CS101,2026-2,70\r\n";      // Windows 줄바꿈 (정상으로 읽혀야 함)
        out.close();

        std::vector<Grade> loaded;
        check(io.loadGrades(loaded), "성적 불러오기");
        check(loaded.size() == 2, "깨진 줄 2개는 빼고 2개만 읽음");
        if (loaded.size() == 2) {
            check(loaded[0].score == 87.5, "소수점 점수 그대로 돌아옴");
            check(loaded[1].score == 70, "\\r\\n 줄도 정상으로 읽음");
        }
    }

    std::cout << std::endl;
    if (failCount == 0) {
        std::cout << "모든 테스트 통과" << std::endl;
        return 0;
    }
    std::cout << failCount << "개 실패" << std::endl;
    return 1;
}
