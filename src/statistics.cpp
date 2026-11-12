#include "statistics.h"
#include "retake.h"
#include <algorithm>
#include <utility>

namespace Statistics {

// TODO(C): 아래 전부 구현

double calcGPA(const GradeManager& gm, const std::string& studentId, const std::string& semester) {
    auto grades = Retake::activeGrades(gm.getGradesByStudent(studentId));
    double sum1=0, sum2=0;
    const Course* course;

    //학점 계산, Course 못 찾은 경우 해당 과목 제외하고 계산
    for(int i=0; i<grades.size(); ++i){
        if(semester.empty() || grades[i].semester == semester){
            course = gm.findCourse(grades[i].courseCode);
            if(course != nullptr){
                sum1+=letterToPoint(Retake::officialLetter(grades[i], gm.getGrades())) * course->credit;
                sum2+=course->credit;
            }

        }
    }

    if(sum2==0){
        return 0.0;
    }
    else{
        return sum1/sum2;
    }

}

CourseStats courseStats(const GradeManager& gm, const std::string& courseCode) {
    CourseStats stats;
    stats.courseCode = courseCode;

    auto grades = Retake::activeGrades(gm.getGradesByCourse(courseCode));
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

std::vector<RankEntry> ranking(const GradeManager& gm, const std::string& semester) {
    std::vector<RankEntry> rankEntryVec;
    auto students = gm.getStudents();

    //rankEntry에 한 학생 정보 등록 --> rankEntryVec에 추가
    for(size_t i=0; i<students.size(); ++i){
        RankEntry rankEntry;
        rankEntry.rank=0;
        rankEntry.studentId = students[i].id;
        rankEntry.name = students[i].name;
        rankEntry.gpa = calcGPA(gm, students[i].id, semester);

        rankEntryVec.push_back(rankEntry);
    }

    //gpa 기준으로 벡터 정렬
    std::sort(rankEntryVec.begin(), rankEntryVec.end(), 
    [](const RankEntry &a, const RankEntry &b){
        return a.gpa>b.gpa;
    });

    //ex
    // 0        1       2       3       4       5       index
    // 1        2       3       4       5       6       index+1

    // 1        1       3       4       4       6       rank
    // 4.5      4.5     4.0     3.5     3.5     3.0

    //아무것도 없으면 실행X -> 빈 벡터 반환
    if(!rankEntryVec.empty()){
        rankEntryVec[0].rank=1;
    }

    for(int i=1; i<rankEntryVec.size(); ++i){
        if(rankEntryVec[i-1].gpa==rankEntryVec[i].gpa){
            rankEntryVec[i].rank=rankEntryVec[i-1].rank;
        }
        else{
            rankEntryVec[i].rank=i+1;
        }
    }

    return rankEntryVec;
    //return {};
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
            if (course == nullptr) continue;
            pointSum += letterToPoint(scoreToLetter(g.score)) * course->credit;
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
