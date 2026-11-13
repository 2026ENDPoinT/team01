// 상대평가 등급 산출: 교수자가 설정한 A+, A0, B+, B0, C+, F 비율 적용
//   구현: #67 (src/relative_grading.cpp)
#pragma once

#include <string>
#include <vector>

namespace RelativeGrading {
inline constexpr double kMaxARatio = 0.30;    // A0 이상 30% 이하
inline constexpr double kMaxABRatio = 0.70;   // B0 이상 70% 이하
inline constexpr double kMaxAverage = 3.50;   // 최대 허용 평점 평균

// 비율 순서: A+, A0, B+, B0, C+, F. 비율 합계는 100이어야 함.
std::vector<int> defaultRatios();
bool validRatios(const std::vector<int>& ratios);
std::vector<std::string> assignLetters(const std::vector<double>& scores,
                                       const std::vector<int>& ratios);
std::vector<std::string> assignLetters(const std::vector<double>& scores);
}  // namespace RelativeGrading
