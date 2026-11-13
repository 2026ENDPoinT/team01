#include "file_io.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "account.h"

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
//   headers: 맨 앞이 지금 형식의 헤더, 그 뒤는 예전 형식의 헤더 (칸 수가 다른 예전 파일도 읽기 위해)
//   - 파일이 없으면 rows 는 비어 있고 true (처음 실행할 때)
//   - 첫 줄이 headers 중 하나와 같으면 건너뜀
//   - 칸 개수가 headers 의 칸 수 어느 것과도 다른 줄은 경고 후 건너뜀
//   - 파일은 있는데 열 수 없으면 false
bool readCsv(const std::string& path, const std::vector<std::string>& headers, std::vector<CsvRow>& rows) {
    rows.clear();
    if (!fs::exists(path)) {
        return true;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "오류: " << path << " 파일을 열 수 없습니다." << std::endl;
        return false;
    }

    std::vector<size_t> columnCounts;
    for (const std::string& header : headers) {
        columnCounts.push_back(splitLine(header).size());
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
        if (lineNumber == 1 && std::find(headers.begin(), headers.end(), line) != headers.end()) {
            continue;
        }

        std::vector<std::string> fields = splitLine(line);
        if (std::find(columnCounts.begin(), columnCounts.end(), fields.size()) == columnCounts.end()) {
            std::cerr << "경고: " << path << " " << lineNumber << "번째 줄 무시 (칸 " << fields.size()
                      << "개, " << columnCounts.front() << "개여야 함)" << std::endl;
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

// 게시물 본문의 줄바꿈을 한 줄로 저장하기 위한 변환: \ → \\, 줄바꿈 → 두 글자 \n, \r 은 버림
std::string encodeLineBreaks(const std::string& text) {
    std::string result;
    for (char c : text) {
        if (c == '\\') {
            result += "\\\\";
        } else if (c == '\n') {
            result += "\\n";
        } else if (c != '\r') {
            result += c;
        }
    }
    return result;
}

// encodeLineBreaks 의 반대: \n → 줄바꿈, \\ → \, 그 밖의 \x 는 그대로 둠
std::string decodeLineBreaks(const std::string& text) {
    std::string result;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == 'n' || text[i + 1] == '\\')) {
            result += text[i + 1] == 'n' ? '\n' : '\\';
            i++;
        } else {
            result += text[i];
        }
    }
    return result;
}

}  // namespace

// 값 여러 개를 쉼표로 이어서 한 줄로 만듦 (끝에 줄바꿈 포함)
std::string joinCsvLine(const std::vector<std::string>& fields) {
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

// ---------------------------------------------------------------------------
// FileIO
// ---------------------------------------------------------------------------

FileIO::FileIO(const std::string& dataDir) : dataDir(dataDir) {}

// students.csv : id,name,major,year

bool FileIO::saveStudents(const std::vector<Student>& students) {
    std::string content = "id,name,major,year\n";
    for (const Student& s : students) {
        content += joinCsvLine({s.id, s.name, s.major, std::to_string(s.year)});
    }
    return atomicWrite(dataDir + "/students.csv", content);
}

bool FileIO::loadStudents(std::vector<Student>& students) {
    const std::string path = dataDir + "/students.csv";
    std::vector<CsvRow> rows;
    if (!readCsv(path, {"id,name,major,year"}, rows)) {
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

// courses.csv : code,name,credit,professor,category,area,schedule,language,capacity,remote
//   예전 형식(code,name,credit,professor) 파일도 읽고, 없는 칸은 Course 기본값으로 채움 → 다음 저장 때 새 형식으로 바뀜

const char* const kCoursesHeader = "code,name,credit,professor,category,area,schedule,language,capacity,remote";
const char* const kCoursesLegacyHeader = "code,name,credit,professor";

bool FileIO::saveCourses(const std::vector<Course>& courses) {
    std::string content = std::string(kCoursesHeader) + "\n";
    for (const Course& c : courses) {
        content += joinCsvLine({c.code, c.name, std::to_string(c.credit), c.professor, c.category, c.area,
                                c.schedule, c.language, std::to_string(c.capacity), c.remote ? "Y" : "N"});
    }
    return atomicWrite(dataDir + "/courses.csv", content);
}

bool FileIO::loadCourses(std::vector<Course>& courses) {
    const std::string path = dataDir + "/courses.csv";
    std::vector<CsvRow> rows;
    if (!readCsv(path, {kCoursesHeader, kCoursesLegacyHeader}, rows)) {
        return false;
    }

    courses.clear();
    for (const CsvRow& row : rows) {
        const std::vector<std::string>& f = row.fields;
        Course c;
        c.code = f[0];
        c.name = f[1];
        if (!parseInt(f[2], c.credit)) {
            warnBadNumber(path, row.lineNumber, f[2]);
            continue;
        }
        c.professor = f[3];
        if (f.size() > 4) {
            c.category = f[4];
            c.area = f[5];
            c.schedule = f[6];
            c.language = f[7];
            if (!parseInt(f[8], c.capacity)) {
                warnBadNumber(path, row.lineNumber, f[8]);
                continue;
            }
            c.remote = f[9] == "Y";
        }
        courses.push_back(c);
    }
    return true;
}

// grades.csv : studentId,courseCode,semester,score

bool FileIO::saveGrades(const std::vector<Grade>& grades) {
    std::string content = "studentId,courseCode,semester,score\n";
    for (const Grade& g : grades) {
        content += joinCsvLine({g.studentId, g.courseCode, g.semester, doubleToString(g.score)});
    }
    return atomicWrite(dataDir + "/grades.csv", content);
}

bool FileIO::loadGrades(std::vector<Grade>& grades) {
    const std::string path = dataDir + "/grades.csv";
    std::vector<CsvRow> rows;
    if (!readCsv(path, {"studentId,courseCode,semester,score"}, rows)) {
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

// enrollments.csv : studentId,courseCode,semester

const char* const kEnrollmentsHeader = "studentId,courseCode,semester";

bool FileIO::saveEnrollments(const std::vector<Enrollment>& enrollments) {
    std::string content = std::string(kEnrollmentsHeader) + "\n";
    for (const Enrollment& e : enrollments) {
        content += joinCsvLine({e.studentId, e.courseCode, e.semester});
    }
    return atomicWrite(dataDir + "/enrollments.csv", content);
}

bool FileIO::loadEnrollments(std::vector<Enrollment>& enrollments) {
    std::vector<CsvRow> rows;
    if (!readCsv(dataDir + "/enrollments.csv", {kEnrollmentsHeader}, rows)) {
        return false;
    }

    enrollments.clear();
    for (const CsvRow& row : rows) {
        enrollments.push_back({row.fields[0], row.fields[1], row.fields[2]});
    }
    return true;
}

// settings.csv : key,value (지금은 enrollmentSemester 하나. 값이 "" 이면 수강신청 기간이 아님)

bool FileIO::saveSettings(const std::string& enrollmentSemester) {
    std::string content = "key,value\n";
    content += joinCsvLine({"enrollmentSemester", enrollmentSemester});
    return atomicWrite(dataDir + "/settings.csv", content);
}

bool FileIO::loadSettings(std::string& enrollmentSemester) {
    std::vector<CsvRow> rows;
    if (!readCsv(dataDir + "/settings.csv", {"key,value"}, rows)) {
        return false;
    }

    enrollmentSemester.clear();
    for (const CsvRow& row : rows) {
        if (row.fields[0] == "enrollmentSemester") {
            enrollmentSemester = row.fields[1];
        }
    }
    return true;
}

// accounts.csv : username,role,name,studentId,salt,passwordHash

const char* const kAccountsHeader = "username,role,name,studentId,salt,passwordHash";

bool FileIO::saveAccounts(const std::vector<Account>& accounts) {
    std::string content = std::string(kAccountsHeader) + "\n";
    for (const Account& a : accounts) {
        content += joinCsvLine({a.username, a.role, a.name, a.studentId, a.salt, a.passwordHash});
    }
    return atomicWrite(dataDir + "/accounts.csv", content);
}

bool FileIO::loadAccounts(std::vector<Account>& accounts) {
    const std::string path = dataDir + "/accounts.csv";
    std::vector<CsvRow> rows;
    if (!readCsv(path, {kAccountsHeader}, rows)) {
        return false;
    }

    accounts.clear();
    for (const CsvRow& row : rows) {
        const std::vector<std::string>& f = row.fields;
        if (f[0].empty() || !isValidRole(f[1])) {
            std::cerr << "경고: " << path << " " << row.lineNumber << "번째 줄 무시 (아이디나 역할이 올바르지 않음)"
                      << std::endl;
            continue;
        }
        accounts.push_back({f[0], f[1], f[2], f[3], f[4], f[5]});
    }
    return true;
}

// course_posts.csv : id,courseCode,semester,category,week,title,body,link,dueDate,author,createdAt

const char* const kPostsHeader = "id,courseCode,semester,category,week,title,body,link,dueDate,author,createdAt";

bool FileIO::savePosts(const std::vector<CoursePost>& posts) {
    std::string content = std::string(kPostsHeader) + "\n";
    for (const CoursePost& p : posts) {
        content += joinCsvLine({std::to_string(p.id), p.courseCode, p.semester, p.category, std::to_string(p.week),
                                p.title, encodeLineBreaks(p.body), p.link, p.dueDate, p.author, p.createdAt});
    }
    return atomicWrite(dataDir + "/course_posts.csv", content);
}

bool FileIO::loadPosts(std::vector<CoursePost>& posts) {
    const std::string path = dataDir + "/course_posts.csv";
    std::vector<CsvRow> rows;
    if (!readCsv(path, {kPostsHeader}, rows)) {
        return false;
    }

    posts.clear();
    for (const CsvRow& row : rows) {
        const std::vector<std::string>& f = row.fields;
        CoursePost post;
        if (!parseInt(f[0], post.id)) {
            warnBadNumber(path, row.lineNumber, f[0]);
            continue;
        }
        if (!parseInt(f[4], post.week)) {
            warnBadNumber(path, row.lineNumber, f[4]);
            continue;
        }
        post.courseCode = f[1];
        post.semester = f[2];
        post.category = f[3];
        post.title = f[5];
        post.body = decodeLineBreaks(f[6]);
        post.link = f[7];
        post.dueDate = f[8];
        post.author = f[9];
        post.createdAt = f[10];

        const bool duplicateId = std::any_of(posts.begin(), posts.end(), [&](const CoursePost& other) {
            return other.id == post.id;
        });
        if (post.id < 1 || !post.isValid() || duplicateId) {
            std::cerr << "경고: " << path << " " << row.lineNumber << "번째 줄 무시 (게시물 값이 올바르지 않음)"
                      << std::endl;
            continue;
        }
        posts.push_back(post);
    }
    return true;
}

// assessments.csv : courseCode,semester,name,weight

const char* const kAssessmentsHeader = "courseCode,semester,name,weight";

bool FileIO::saveAssessments(const std::vector<Assessment>& items) {
    std::string content = std::string(kAssessmentsHeader) + "\n";
    for (const Assessment& a : items) {
        content += joinCsvLine({a.courseCode, a.semester, a.name, std::to_string(a.weight)});
    }
    return atomicWrite(dataDir + "/assessments.csv", content);
}

bool FileIO::loadAssessments(std::vector<Assessment>& items) {
    const std::string path = dataDir + "/assessments.csv";
    std::vector<CsvRow> rows;
    if (!readCsv(path, {kAssessmentsHeader}, rows)) {
        return false;
    }

    items.clear();
    for (const CsvRow& row : rows) {
        const std::vector<std::string>& f = row.fields;
        Assessment a;
        a.courseCode = f[0];
        a.semester = f[1];
        a.name = f[2];
        if (!parseInt(f[3], a.weight)) {
            warnBadNumber(path, row.lineNumber, f[3]);
            continue;
        }

        const bool duplicate = std::any_of(items.begin(), items.end(), [&](const Assessment& other) {
            return other.courseCode == a.courseCode && other.semester == a.semester && other.name == a.name;
        });
        if (!a.isValid() || duplicate) {
            std::cerr << "경고: " << path << " " << row.lineNumber << "번째 줄 무시 (평가 항목 값이 올바르지 않음)"
                      << std::endl;
            continue;
        }
        items.push_back(a);
    }
    return true;
}

// assessment_scores.csv : studentId,courseCode,semester,name,score

const char* const kAssessmentScoresHeader = "studentId,courseCode,semester,name,score";

bool FileIO::saveAssessmentScores(const std::vector<AssessmentScore>& scores) {
    std::string content = std::string(kAssessmentScoresHeader) + "\n";
    for (const AssessmentScore& s : scores) {
        content += joinCsvLine({s.studentId, s.courseCode, s.semester, s.name, doubleToString(s.score)});
    }
    return atomicWrite(dataDir + "/assessment_scores.csv", content);
}

bool FileIO::loadAssessmentScores(std::vector<AssessmentScore>& scores) {
    const std::string path = dataDir + "/assessment_scores.csv";
    std::vector<CsvRow> rows;
    if (!readCsv(path, {kAssessmentScoresHeader}, rows)) {
        return false;
    }

    scores.clear();
    for (const CsvRow& row : rows) {
        const std::vector<std::string>& f = row.fields;
        AssessmentScore s;
        s.studentId = f[0];
        s.courseCode = f[1];
        s.semester = f[2];
        s.name = f[3];
        if (!parseDouble(f[4], s.score)) {
            warnBadNumber(path, row.lineNumber, f[4]);
            continue;
        }

        const bool duplicate = std::any_of(scores.begin(), scores.end(), [&](const AssessmentScore& other) {
            return other.studentId == s.studentId && other.courseCode == s.courseCode &&
                   other.semester == s.semester && other.name == s.name;
        });
        if (!s.isValid() || duplicate) {
            std::cerr << "경고: " << path << " " << row.lineNumber << "번째 줄 무시 (항목 점수 값이 올바르지 않음)"
                      << std::endl;
            continue;
        }
        scores.push_back(s);
    }
    return true;
}

// grading_modes.csv : courseCode,semester,mode
//   구현: #66 (지금은 스텁)
bool FileIO::saveGradingModes(const std::vector<GradingMode>&) {
    return true;
}

bool FileIO::loadGradingModes(std::vector<GradingMode>& modes) {
    modes.clear();
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
