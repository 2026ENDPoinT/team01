// 상대평가 등급 산출
//   구현: #67 (지금은 스텁: 각 점수의 scoreToLetter 를 그대로 돌려줌)
#include "relative_grading.h"

#include "grade.h"

namespace RelativeGrading {

std::vector<std::string> assignLetters(const std::vector<double>& scores) {
    std::vector<std::string> letters;
    letters.reserve(scores.size());
    for (double score : scores) {
        letters.push_back(scoreToLetter(score));
    }
    return letters;
}

}  // namespace RelativeGrading
