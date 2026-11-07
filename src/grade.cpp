#include "grade.h"

bool Grade::isValid() const {
    // TODO(A)
    return false;
}

std::string Grade::letter() const {
    return scoreToLetter(score);
}

std::string scoreToLetter(double score) {
    // TODO(A)
    (void)score;
    return "F";
}

double letterToPoint(const std::string& letter) {
    // TODO(A)
    (void)letter;
    return 0.0;
}
