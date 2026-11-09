#include "file_io.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// CSV 도우미 함수 (이 파일 안에서만 사용)
// ---------------------------------------------------------------------------
namespace {

// 값에 쉼표나 큰따옴표가 있으면 "..." 로 감싸고, 안의 " 는 "" 로 바꿈
//   예) 홍길동        → 홍길동
//       컴퓨터공학,AI → "컴퓨터공학,AI"
std::string escapeField(const std::string& value) {
    if (value.find(',') == std::string::npos && value.find('"') == std::string::npos) {
        return value;
    }
    std::string result = "\"";
    for (char c : value) {
        if (c == '"') {
            result += "\"\"";
        } else {
            result += c;
        }
    }
    result += "\"";
    return result;
}

// 값 여러 개를 쉼표로 이어서 한 줄로 만듦 (끝에 줄바꿈 포함)
std::string joinLine(const std::vector<std::string>& fields) {
    std::string line;
    for (size_t i = 0; i < fields.size(); i++) {
        if (i > 0) {
            line += ',';
        }
        line += escapeField(fields[i]);
    }
    line += '\n';
    return line;
}

// CSV 한 줄을 쉼표 기준으로 나눔. escapeField 의 반대
//   "..." 안의 쉼표는 나누지 않고, "" 는 " 하나로 바꿈
std::vector<std::string> splitLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                current += '"';  // "" → "
                i++;
            } else if (c == '"') {
                inQuotes = false;  // 닫는 따옴표
            } else {
                current += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;  // 여는 따옴표
            } else if (c == ',') {
                fields.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
    }
    fields.push_back(current);
    return fields;
}

// 문자열 → 정수. 숫자가 아니면 false (std::stoi 는 "12abc" 도 12 로 읽으므로 끝까지 읽었는지 확인)
bool parseInt(const std::string& text, int& out) {
    try {
        size_t used = 0;
        out = std::stoi(text, &used);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

// 문자열 → 실수. parseInt 와 같은 방식
bool parseDouble(const std::string& text, double& out) {
    try {
        size_t used = 0;
        out = std::stod(text, &used);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

// double 을 문자열로 (87.5 → "87.5", 90 → "90")
std::string doubleToString(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

// CSV 파일에서 한 줄씩 읽어서 나눈 결과
struct CsvRow {
    int lineNumber;                   // 경고 메시지에 쓸 줄 번호 (1부터)
    std::vector<std::string> fields;  // 쉼표로 나눈 값들
};

// path 의 CSV 를 읽어서 rows 에 담음
//   - 파일이 없으면 rows 는 비어 있고 true (처음 실행할 때)
//   - 첫 줄이 header 와 같으면 건너뜀
//   - 칸 개수가 columnCount 와 다른 줄은 경고 후 건너뜀
//   - 파일은 있는데 열 수 없으면 false
bool readCsv(const std::string& path, const std::string& header, size_t columnCount,
             std::vector<CsvRow>& rows) {
    rows.clear();
    if (!fs::exists(path)) {
        return true;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "오류: " << path << " 파일을 열 수 없습니다." << std::endl;
        return false;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        lineNumber++;

        // Windows 에서 만든 파일은 줄 끝에 \r 이 남아 있음
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // 엑셀로 저장한 UTF-8 파일은 맨 앞에 BOM(EF BB BF) 3바이트가 붙어 있음
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }

        if (line.empty()) {
            continue;
        }
        if (lineNumber == 1 && line == header) {
            continue;
        }

        std::vector<std::string> fields = splitLine(line);
        if (fields.size() != columnCount) {
            std::cerr << "경고: " << path << " " << lineNumber << "번째 줄 무시 (칸 " << fields.size()
                      << "개, " << columnCount << "개여야 함)" << std::endl;
            continue;
        }
        rows.push_back({lineNumber, fields});
    }
    return true;
}

// 숫자로 바꿀 수 없는 값이 있을 때 경고
void warnBadNumber(const std::string& path, int lineNumber, const std::string& value) {
    std::cerr << "경고: " << path << " " << lineNumber << "번째 줄 무시 (숫자가 아님: \"" << value
              << "\")" << std::endl;
}

}  // namespace

// ---------------------------------------------------------------------------
// FileIO
// ---------------------------------------------------------------------------

FileIO::FileIO(const std::string& dataDir) : dataDir(dataDir) {}

// students.csv : id,name,major,year

bool FileIO::saveStudents(const std::vector<Student>& students) {
    std::string content = "id,name,major,year\n";
    for (const Student& s : students) {
        content += joinLine({s.id, s.name, s.major, std::to_string(s.year)});
    }
    return atomicWrite(dataDir + "/students.csv", content);
}

bool FileIO::loadStudents(std::vector<Student>& students) {
    const std::string path = dataDir + "/students.csv";
    std::vector<CsvRow> rows;
    if (!readCsv(path, "id,name,major,year", 4, rows)) {
        return false;
    }

    students.clear();
    for (const CsvRow& row : rows) {
        Student s;
        s.id = row.fields[0];
        s.name = row.fields[1];
        s.major = row.fields[2];
        if (!parseInt(row.fields[3], s.year)) {
            warnBadNumber(path, row.lineNumber, row.fields[3]);
            continue;
        }
        students.push_back(s);
    }
    return true;
}

// courses.csv : code,name,credit,professor

bool FileIO::saveCourses(const std::vector<Course>& courses) {
    std::string content = "code,name,credit,professor\n";
    for (const Course& c : courses) {
        content += joinLine({c.code, c.name, std::to_string(c.credit), c.professor});
    }
    return atomicWrite(dataDir + "/courses.csv", content);
}

bool FileIO::loadCourses(std::vector<Course>& courses) {
    const std::string path = dataDir + "/courses.csv";
    std::vector<CsvRow> rows;
    if (!readCsv(path, "code,name,credit,professor", 4, rows)) {
        return false;
    }

    courses.clear();
    for (const CsvRow& row : rows) {
        Course c;
        c.code = row.fields[0];
        c.name = row.fields[1];
        if (!parseInt(row.fields[2], c.credit)) {
            warnBadNumber(path, row.lineNumber, row.fields[2]);
            continue;
        }
        c.professor = row.fields[3];
        courses.push_back(c);
    }
    return true;
}

// grades.csv : studentId,courseCode,semester,score

bool FileIO::saveGrades(const std::vector<Grade>& grades) {
    std::string content = "studentId,courseCode,semester,score\n";
    for (const Grade& g : grades) {
        content += joinLine({g.studentId, g.courseCode, g.semester, doubleToString(g.score)});
    }
    return atomicWrite(dataDir + "/grades.csv", content);
}

bool FileIO::loadGrades(std::vector<Grade>& grades) {
    const std::string path = dataDir + "/grades.csv";
    std::vector<CsvRow> rows;
    if (!readCsv(path, "studentId,courseCode,semester,score", 4, rows)) {
        return false;
    }

    grades.clear();
    for (const CsvRow& row : rows) {
        Grade g;
        g.studentId = row.fields[0];
        g.courseCode = row.fields[1];
        g.semester = row.fields[2];
        if (!parseDouble(row.fields[3], g.score)) {
            warnBadNumber(path, row.lineNumber, row.fields[3]);
            continue;
        }
        grades.push_back(g);
    }
    return true;
}

// 저장 순서: 백업 → path.tmp 에 쓰기 → path.tmp 를 path 로 이름 바꾸기
//   쓰는 도중에 프로그램이 꺼져도 원래 path 는 그대로 남아 있음
bool FileIO::atomicWrite(const std::string& path, const std::string& content) {
    std::error_code ec;  // filesystem 함수가 예외 대신 여기에 오류를 담음
    fs::create_directories(dataDir, ec);  // data/ 폴더가 없으면 만듦

    backup(path);

    const std::string tmpPath = path + ".tmp";
    {
        // binary: Windows 에서도 줄바꿈을 \r\n 으로 바꾸지 않고 \n 그대로 저장
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cerr << "오류: " << tmpPath << " 파일을 만들 수 없습니다." << std::endl;
            return false;
        }
        out << content;
        out.close();
        if (!out) {
            std::cerr << "오류: " << tmpPath << " 파일 쓰기에 실패했습니다." << std::endl;
            return false;
        }
    }

    fs::rename(tmpPath, path, ec);
    if (ec) {
        // 일부 Windows 컴파일러(MinGW)는 대상 파일이 이미 있으면 rename 이 실패함
        // → .bak 백업은 이미 했으므로 원본을 지우고 한 번 더 시도
        fs::remove(path, ec);
        fs::rename(tmpPath, path, ec);
        if (ec) {
            std::cerr << "오류: " << path << " 저장 실패 (" << ec.message() << ")" << std::endl;
            return false;
        }
    }
    return true;
}

void FileIO::backup(const std::string& path) {
    if (!fs::exists(path)) {
        return;  // 처음 저장할 때는 백업할 파일이 없음
    }
    std::error_code ec;
    fs::copy_file(path, path + ".bak", fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "경고: " << path << " 백업 실패 (" << ec.message() << ")" << std::endl;
    }
}
