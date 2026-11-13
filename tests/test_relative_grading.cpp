// 상대평가 등급 산출 (relative_grading.h): A0 이상 30%, B0 이상 70%, 평균 3.50 이하
//   주의: CHECK_EQ(..., f({..})[0]) 처럼 임시 컨테이너 원소를 바로 넘기면 댕글링 참조로 멈춤(#77)
//   → 결과를 지역 변수에 먼저 담고 비교
#include <string>
#include <vector>

#include "check.h"
#include "grade.h"
#include "relative_grading.h"

namespace {

// scores 의 결과가 expected 와 같은지 (크기와 원소별)
void checkLetters(const std::vector<double>& scores, const std::vector<std::string>& expected) {
    const std::vector<std::string> actual = RelativeGrading::assignLetters(scores);
    CHECK_EQ(actual.size(), expected.size());
    if (actual.size() != expected.size()) return;
    for (size_t i = 0; i < expected.size(); i++) {
        CHECK_EQ(actual[i], expected[i]);
    }
}

double averageOf(const std::vector<std::string>& letters) {
    double sum = 0.0;
    for (const std::string& letter : letters) sum += letterToPoint(letter);
    return letters.empty() ? 0.0 : sum / static_cast<double>(letters.size());
}

}  // namespace

TEST_CASE(relativeGrading_empty) {
    const std::vector<std::string> result = RelativeGrading::assignLetters({});
    CHECK(result.empty());
}

TEST_CASE(relativeGrading_ten_distinct_scores) {
    const std::vector<double> scores = {100, 98, 96, 94, 92, 90, 88, 86, 84, 82};
    checkLetters(scores, {"A+", "A+", "A+", "B+", "B+", "B+", "B+", "C+", "C+", "C+"});
    const std::vector<std::string> result = RelativeGrading::assignLetters(scores);
    CHECK_NEAR(averageOf(result), 3.50, 1e-9);  // 정확히 3.50, 더 내리지 않음
}

TEST_CASE(relativeGrading_tied_groups_move_together) {
    // A 묶음 4명은 상한 3 을 넘어 함께 내려가고, B0 묶음 6명은 4+6 > 7 이라 함께 C+
    checkLetters({95, 95, 95, 95, 80, 80, 80, 80, 80, 80},
                 {"B+", "B+", "B+", "B+", "C+", "C+", "C+", "C+", "C+", "C+"});
}

TEST_CASE(relativeGrading_all_tied) {
    checkLetters(std::vector<double>(10, 96.0), std::vector<std::string>(10, "C+"));
}

TEST_CASE(relativeGrading_small_n) {
    checkLetters({89}, {"C+"});                  // n=1: capA 0, capAB 0
    checkLetters({89, 79.35}, {"B+", "C+"});     // n=2: capA 0, capAB 1
    checkLetters({100, 50}, {"B+", "F"});
    checkLetters({82, 81, 50}, {"B0", "B0", "F"});
}

TEST_CASE(relativeGrading_input_order_does_not_matter) {
    checkLetters({84, 100, 92}, {"C+", "B+", "B+"});  // n=3: capA 0, capAB 2
}

TEST_CASE(relativeGrading_never_raises_and_higher_score_not_lower_letter) {
    const std::vector<double> scores = {55, 97, 91, 91, 72, 88, 66, 100, 79, 83, 60, 94};
    const std::vector<std::string> result = RelativeGrading::assignLetters(scores);
    CHECK_EQ(result.size(), scores.size());
    if (result.size() != scores.size()) return;
    for (size_t i = 0; i < scores.size(); i++) {
        CHECK(letterToScore(result[i]) <= letterToScore(scoreToLetter(scores[i])));  // 올리지 않음
        for (size_t j = 0; j < scores.size(); j++) {
            if (scores[i] > scores[j]) {
                CHECK(letterToScore(result[i]) >= letterToScore(result[j]));
            }
            if (scores[i] == scores[j]) {
                CHECK_EQ(result[i], result[j]);
            }
        }
    }
    CHECK(averageOf(result) <= 3.50 + 1e-9);
}