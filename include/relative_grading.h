// 상대평가 등급 산출: A0 이상 30% 이하, B0 이상 70% 이하, 최대 허용 평점 평균 3.50
//   구현: #67 (src/relative_grading.cpp)
#pragma once

#include <string>
#include <vector>

namespace RelativeGrading {
inline constexpr double kMaxARatio = 0.30;    // A0 이상 30% 이하
inline constexpr double kMaxABRatio = 0.70;   // B0 이상 70% 이하
inline constexpr double kMaxAverage = 3.50;   // 최대 허용 평점 평균

// scores[i] 의 등급을 같은 순서로 돌려줌 (알고리즘: #67)
std::vector<std::string> assignLetters(const std::vector<double>& scores);
}  // namespace RelativeGrading
