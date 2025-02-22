#include "stats.h"
#include "common.h"

#include <unordered_set>

double median(std::vector<double> u)
{
  if (u.size() % 2 == 0) {
    const auto median_it1 = u.begin() + u.size() / 2 - 1;
    const auto median_it2 = u.begin() + u.size() / 2;

    std::nth_element(u.begin(), median_it1, u.end());
    const auto e1 = *median_it1;

    std::nth_element(u.begin(), median_it2, u.end());
    const auto e2 = *median_it2;

    return (e1 + e2) / 2;
  } else {
    const auto median_it = u.begin() + u.size() / 2;
    std::nth_element(u.begin(), median_it, u.end());
    return *median_it;
  }
}

std::vector<int> rank(const std::vector<double>& v_temp)
{
  std::vector<std::pair<double, int>> v_sort(v_temp.size());

  for (int i = 0; i < v_sort.size(); ++i) {
    v_sort[i] = std::make_pair(v_temp[i], i);
  }

  sort(v_sort.begin(), v_sort.end());

  std::vector<int> result(v_temp.size());

  // N.B. Stata's rank starts at 1, not 0, so the "+1" is added here.
  for (int i = 0; i < v_sort.size(); ++i) {
    result[v_sort[i].second] = i + 1;
  }
  return result;
}

std::vector<double> remove_value(const std::vector<double>& vec, double target)
{
  std::vector<double> cleanedVec;
  for (const double& val : vec) {
    if (val != target) {
      cleanedVec.push_back(val);
    }
  }
  return cleanedVec;
}

double correlation(const std::vector<double>& y1, const std::vector<double>& y2)
{
  Eigen::Map<const Eigen::ArrayXd> y1Map(y1.data(), y1.size());
  Eigen::Map<const Eigen::ArrayXd> y2Map(y2.data(), y2.size());

  const Eigen::ArrayXd y1Cent = y1Map - y1Map.mean();
  ;
  const Eigen::ArrayXd y2Cent = y2Map - y2Map.mean();

  return (y1Cent * y2Cent).sum() / (std::sqrt((y1Cent * y1Cent).sum()) * std::sqrt((y2Cent * y2Cent).sum()));
}

double mean_absolute_error(const std::vector<double>& y1, const std::vector<double>& y2)
{
  Eigen::Map<const Eigen::ArrayXd> y1Map(y1.data(), y1.size());
  Eigen::Map<const Eigen::ArrayXd> y2Map(y2.data(), y2.size());
  double mae = (y1Map - y2Map).abs().mean();
  if (mae < 1e-8) {
    return 0;
  } else {
    return mae;
  }
}

double standard_deviation(const std::vector<double>& vec)
{
  Eigen::Map<const Eigen::ArrayXd> map(vec.data(), vec.size());
  const Eigen::ArrayXd centered = map - map.mean();
  return std::sqrt((centered * centered).sum() / (centered.size() - 1));
}

double default_missing_distance(const std::vector<double>& x)
{
  const double PI = 3.141592653589793238463;
  auto xObserved = remove_value(x, MISSING_SENTINEL);
  double xSD = standard_deviation(xObserved);
  return 2 / sqrt(PI) * xSD;
}

double default_dt_weight(const std::vector<double>& dts, const std::vector<double>& x)
{
  auto xObserved = remove_value(x, MISSING_SENTINEL);
  double xSD = standard_deviation(xObserved);

  auto dtObserved = remove_value(dts, MISSING_SENTINEL);
  double dtSD = standard_deviation(dtObserved);

  if (dtSD == 0.0) {
    return -1;
  } else {
    return xSD / dtSD;
  }
}

Metric guess_appropriate_metric(std::vector<double> data, int targetSample = 100)
{
  std::unordered_set<double> uniqueValues;

  int sampleSize = 0;
  for (int i = 0; i < data.size() && sampleSize < targetSample; i++) {
    if (data[i] != MISSING_SENTINEL) {
      sampleSize += 1;
      uniqueValues.insert(data[i]);
    }
  }

  if (uniqueValues.size() <= 10) {
    // The data is likely binary or categorical, calculate the indicator function for two values being identical
    return Metric::CheckSame;
  } else {
    // The data is likely continuous, just take differences between the values
    return Metric::Diff;
  }
}