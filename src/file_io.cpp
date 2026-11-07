#include "file_io.h"

FileIO::FileIO(const std::string& dataDir) : dataDir(dataDir) {}

// TODO(조장): 아래 전부 구현

bool FileIO::saveStudents(const std::vector<Student>&) { return false; }
bool FileIO::loadStudents(std::vector<Student>&) { return false; }

bool FileIO::saveCourses(const std::vector<Course>&) { return false; }
bool FileIO::loadCourses(std::vector<Course>&) { return false; }

bool FileIO::saveGrades(const std::vector<Grade>&) { return false; }
bool FileIO::loadGrades(std::vector<Grade>&) { return false; }

bool FileIO::atomicWrite(const std::string&, const std::string&) { return false; }
void FileIO::backup(const std::string&) {}
