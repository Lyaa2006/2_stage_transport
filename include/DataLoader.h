#pragma once

#include "ProblemData.h"

#include <string>

class DataLoader {
public:
    ProblemData loadFromCsvDir(const std::string &dirPath) const;
};
