#include "statistics.h"
#include "retake.h"
#include <map>
#include <utility>
#include "semester.h"
#include <algorithm>
#include <set>

namespace Statistics {

// TODO(C): 아래 전부 구현

namespace {

// 학점 가중 합 (평점×학점 합, 학점 합)
struct GpaSum {
    double points = 0.0;
    double credits = 0.0;
    double majorPoints = 0.0;
    double majorCredits = 0.0;
    double earned = 0.0;

    void add(const GpaSum& other) {
        points += other.points;
        credits += other.credits;
        majorPoints += other.majorPoints;
        majorCredits += other.majorCredits;
        earned += other.earned;
    }
};

// 전공과목 = 이수구분이 전공필수 또는 전공선택
bool isMajorCourse(const Course& course) {
    return course.category == "전공필수" || course.category == "전공선택";
}

// grades 를 받는 본체: studentGrades = 그 학생의 성적, allGrades = officialLetter 가 보는 전체 성적
std::map<std::string, GpaSum> sumBySemester(const GradeManager& gm,
                                            const std::vector<Grade>& studentGrades,
                                            const std::vector<Grade>& allGrades) {
    const std::vector<Grade> grades = Retake::activeGrades(studentGrades);
    std::map<std::string, GpaSum> sums;
        for (const Grade& g : grades) {
        const Course* course = gm.findCourse(g.courseCode);
        if (course == nullptr) continue;
        const std::string letter = Retake::officialLetter(g, allGrades);
        GpaSum& sum = sums[g.semester];  // P/F 만 있는 학기도 목록에 나오게 먼저 만듦
        if (course->isPassFail()) {
            // 평점·계산 학점·전공 GPA 에서 제외, 취득 학점에는 P 만 (NP 는 0)
            if (letter == kLetterPass) sum.earned += course->credit;
            continue;
        }
        const double weighted = letterToPoint(letter) * course->credit;
        sum.points += weighted;
        sum.credits += course->credit;
        if (letter != "F" && letter != "R") sum.earned += course->credit;
        if (isMajorCourse(*course)) {
            sum.majorPoints += weighted;
            sum.majorCredits += course->credit;
        }
    }
    return sums;
}

std::map<std::string, GpaSum> sumBySemester(const GradeManager& gm, const std::string& studentId) {
    return sumBySemester(gm, gm.getGradesByStudent(studentId), gm.getGrades());
}

double toGpa(const GpaSum& sum) {
    return sum.credits == 0 ? 0.0 : sum.points / sum.credits;
}

double toMajorGpa(const GpaSum& sum) {
    return sum.majorCredits == 0 ? 0.0 : sum.majorPoints / sum.majorCredits;
}

GpaSummary toSummary(const std::map<std::string, GpaSum>& sums) {
    GpaSummary summary;
    GpaSum total;
    for (const auto& [sem, sum] : sums) {  // map 이라 학기 오름차순
        SemesterSummary s;
        s.semester = sem;
        s.gpa = toGpa(sum);
        s.credits = static_cast<int>(sum.credits);
        s.majorGpa = toMajorGpa(sum);
        s.majorCredits = static_cast<int>(sum.majorCredits);
        summary.semesters.push_back(s);
        s.earnedCredits = static_cast<int>(sum.earned);
        total.add(sum);
    }
    summary.gpa = toGpa(total);
    summary.credits = static_cast<int>(total.credits);
    summary.majorGpa = toMajorGpa(total);
    summary.majorCredits = static_cast<int>(total.majorCredits);
    summary.earnedCredits = static_cast<int>(total.earned);
    return summary;
}

}  // namespace

double calcGPA(const GradeManager& gm, const std::string& studentId, const std::string& semester) {
    const std::map<std::string, GpaSum> sums = sumBySemester(gm, studentId);
    GpaSum total;
    for (const auto& [sem, sum] : sums) {
        if (semester.empty() || sem == semester) {
            total.points += sum.points;
            total.credits += sum.credits;
        }
    }
    return toGpa(total);
}

GpaSummary gpaSummary(const GradeManager& gm, const std::string& studentId) {
    return toSummary(sumBySemester(gm, studentId));
}

CourseStats courseStats(const GradeManager& gm, const std::string& courseCode) {
    CourseStats stats;
    stats.courseCode = courseCode;

        // P/F 과목은 점수(P = 100, NP = 0)가 의미 없으므로 점수 통계에서 제외 → 성적 없는 과목처럼 count = 0
    const Course* course = gm.findCourse(courseCode);
    const bool passFail = course != nullptr && course->isPassFail();
    auto grades = passFail ? std::vector<Grade>{} : Retake::activeGrades(gm.getGradesByCourse(courseCode));
    if(grades.empty()){
        stats.count=0;
        stats.average=0;
        stats.max=0;
        stats.min=0;
    }
    else{
        double scoreSum=0;
        bool isFirst=true;

        for(int i=0; i<grades.size(); ++i){
                
            if(isFirst){
                stats.max=stats.min=grades[i].score;
                isFirst=false;
            }

            ++stats.count;
            scoreSum+=grades[i].score;

            if(grades[i].score>stats.max)
                stats.max=grades[i].score;
            if(grades[i].score<stats.min)
                stats.min=grades[i].score;
        
        }

        stats.average = scoreSum / stats.count;
    }

    // 수강신청이 있는 가장 늦은 학기와 그 학기 신청 인원 (학기 형식이 고정이라 문자열 비교 = 시간 순서)
    for (const Enrollment& e : gm.getEnrollments()) {
        if (e.courseCode != courseCode || e.semester.empty()) continue;
        if (e.semester > stats.latestSemester) {
            stats.latestSemester = e.semester;
            stats.latestEnrolled = 1;
        } else if (e.semester == stats.latestSemester) {
            stats.latestEnrolled++;
        }
    }

    return stats;
}

namespace {

// 학기 → (연도, N). isValidSemester 를 통과한 값만 넘김
std::pair<int, int> splitSemester(const std::string& semester) {
    return {std::stoi(semester.substr(0, 4)), semester[5] - '0'};
}

// (연도, N) → "YYYY-N" (연도는 4자리로 0 채움)
std::string makeSemester(int year, int term) {
    std::string text = std::to_string(year);
    while (text.size() < 4) text.insert(text.begin(), '0');
    return text + "-" + std::to_string(term);
}

}  // namespace
namespace {

bool isSimLetter(const std::string& letter) {
    static const char* const kLetters[] = {"A+", "A0", "B+", "B0", "C+", "C0", "D+", "D0", "F"};
    return std::any_of(std::begin(kLetters), std::end(kLetters),
                       [&](const char* l) { return letter == l; });
}

RetakeSimulation simFail(RetakeError error, const std::string& message) {
    RetakeSimulation result;
    result.error = error;
    result.message = message;
    return result;
}

}  // namespace

RetakeSimulation simulateRetake(const GradeManager& gm, const std::string& studentId,
                                const std::string& currentSemester,
                                const std::vector<RetakeItem>& items) {
    const std::vector<Grade> original = gm.getGrades();
    std::vector<Grade> simulated = original;  // 복사본만 고침, gm 은 건드리지 않음
    std::set<std::string> seen;
    RetakeSimulation result;

    for (const RetakeItem& item : items) {
        if (!isSimLetter(item.letter)) {
            return simFail(RetakeError::BadLetter,
                           "지원하지 않는 성적 등급입니다 (A+ A0 B+ B0 C+ C0 D+ D0 F)");
        }
        if (gm.findCourse(item.courseCode) == nullptr) {
            return simFail(RetakeError::NoCourse, "없는 과목입니다: " + item.courseCode);
        }
        if (gm.findCourse(item.courseCode)->isPassFail()) {
            return simFail(RetakeError::PassFail, "P/F 과목은 재수강 시뮬레이션 대상이 아닙니다: " + item.courseCode);
        }
        if (!seen.insert(item.courseCode).second) {
            return simFail(RetakeError::Duplicate, "같은 과목이 두 번 들어 있습니다: " + item.courseCode);
        }

        // 이 학생이 이 과목에서 받은 가장 늦은 학기 성적
        const Grade* latest = nullptr;
        for (const Grade& g : original) {
            if (g.studentId != studentId || g.courseCode != item.courseCode) continue;
            if (latest == nullptr || semester::isPast(latest->semester, g.semester)) latest = &g;
        }
        if (latest == nullptr) {
            return simFail(RetakeError::NoGrade, "성적이 없는 과목입니다: " + item.courseCode);
        }
        if (semester::isPast(currentSemester, latest->semester)) {
            return simFail(RetakeError::FutureGrade, "현재 학기(" + currentSemester +
                           ")보다 늦은 학기 성적이 있는 과목입니다: " + item.courseCode);
        }

        RetakeItemResult r;
        r.courseCode = item.courseCode;
        r.previousLetter = Retake::officialLetter(*latest, original);

        // 현재 학기에 이미 성적이 있으면 대체
        simulated.erase(std::remove_if(simulated.begin(), simulated.end(), [&](const Grade& g) {
            return g.studentId == studentId && g.courseCode == item.courseCode &&
                   g.semester == currentSemester;
        }), simulated.end());
        Grade virt;
        virt.studentId = studentId;
        virt.courseCode = item.courseCode;
        virt.semester = currentSemester;
        virt.score = letterToScore(item.letter);
        simulated.push_back(virt);
        r.appliedLetter = Retake::officialLetter(simulated.back(), simulated);  // 재수강이면 A+ → A0
        result.items.push_back(r);
    }

    std::vector<Grade> mine;
    for (const Grade& g : simulated) {
        if (g.studentId == studentId) mine.push_back(g);
    }
    result.before = gpaSummary(gm, studentId);
    result.after = toSummary(sumBySemester(gm, mine, simulated));
    return result;
}

std::vector<WarningEntry> academicWarnings(const GradeManager& gm, const std::string& studentId) {
    std::vector<WarningEntry> result;

    // 시작: 이 학생의 가장 이른 수강신청 학기
    std::string first;
    for (const Enrollment& e : gm.getEnrollments()) {
        if (e.studentId != studentId || !isValidSemester(e.semester)) continue;
        if (first.empty() || e.semester < first) first = e.semester;
    }
    // 끝: 전체 성적 중 가장 늦은 정규학기
    std::string last;
    for (const Grade& g : gm.getGrades()) {
        if (isRegularSemester(g.semester) && g.semester > last) last = g.semester;
    }
    if (first.empty() || last.empty()) return result;

    // 시작이 계절학기(3·4)면 문자열 순서로 그 다음 정규학기인 다음 해 1학기부터
    std::pair<int, int> current = splitSemester(first);
    if (current.second > 2) {
        current = {current.first + 1, 1};
    }
    const std::pair<int, int> end = splitSemester(last);

    // 정규학기를 연도·N(1 → 2) 순으로 차례로 판단 (문자열 대신 정수로 비교해 연도 자릿수 문제를 피함)
    for (; current <= end; current = current.second == 1 ? std::make_pair(current.first, 2)
                                                         : std::make_pair(current.first + 1, 1)) {
        const std::string semester = makeSemester(current.first, current.second);

        int credits = 0;  // 그 학기 신청 학점 합계 (없는 과목은 0)
        for (const Enrollment& e : gm.getEnrollments()) {
            if (e.studentId != studentId || e.semester != semester) continue;
            const Course* course = gm.findCourse(e.courseCode);
            if (course != nullptr) credits += course->credit;
        }
        if (credits < 1) {
            WarningEntry entry;
            entry.semester = semester;
            entry.reason = "수강신청 미이행";
            entry.credits = credits;
            result.push_back(entry);
            continue;
        }

        // 원래 등급(재수강 규칙 미적용)으로 학점 가중 평점, 삭제된 성적도 포함
        double pointSum = 0.0;
        int creditSum = 0;
        for (const Grade& g : gm.getGrades()) {
            if (g.studentId != studentId || g.semester != semester) continue;
            const Course* course = gm.findCourse(g.courseCode);
            if (course == nullptr || course->isPassFail()) continue;  // P/F 는 학사경고 평점에서 제외
            pointSum += letterToPoint(g.letter()) * course->credit;
            creditSum += course->credit;
        }
        if (creditSum == 0) continue;  // 그 학기 성적이 아직 없음

        const double gpa = pointSum / creditSum;
        if (gpa < 1.75 - 1e-9) {  // 정확히 1.75 는 경고 아님 (부동소수 오차 고려)
            WarningEntry entry;
            entry.semester = semester;
            entry.reason = "평점평균 1.75 미만";
            entry.hasGpa = true;
            entry.gpa = gpa;
            entry.credits = credits;
            result.push_back(entry);
        }
    }
    return result;
}

}  // namespace Statistics
