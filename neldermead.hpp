#pragma once

#include <vector>
#include <functional>

int nelmin(std::function<double(std::vector<double> &x)> fn,
           const int n,
           std::vector<double> &start,
           std::vector<double> &xmin,
           double& ynewlo,
           const double reqmin,
           const std::vector<double> &step,
           const int konvge,
           const int kcount,
           int &icount,
           int &numres);