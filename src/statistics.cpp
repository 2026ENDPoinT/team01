#include "statistics.h"
#include "retake.h"
#include <algorithm>

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

}  // namespace Statistics
