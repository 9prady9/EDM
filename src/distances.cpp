
#include "distances.h"
#include "EMD.h"

#define EIGEN_NO_DEBUG
#define EIGEN_DONT_PARALLELIZE
#include <Eigen/Dense>

DistanceIndexPairs lp_distances(int Mp_i, const Options& opts, const Manifold& M, const Manifold& Mp,
                                std::vector<int> inpInds)
{
  std::vector<int> inds;
  std::vector<double> dists;

  // Compare every observation in the M manifold to the
  // Mp_i'th observation in the Mp manifold.
  for (int i : inpInds) {
    // Calculate the distance between M[i] and Mp[Mp_i]
    double dist_i = 0.0;

    // If we have panel data and the M[i] / Mp[Mp_j] observations come from different panels
    // then add the user-supplied penalty/distance for the mismatch.
    if (opts.panelMode && opts.idw > 0) {
      dist_i += opts.idw * (M.panel(i) != Mp.panel(Mp_i));
    }

    for (int j = 0; j < M.E_actual(); j++) {
      // Get the sub-distance between M[i,j] and Mp[Mp_i, j]
      double dist_ij;

      // If either of these values is missing, the distance from
      // M[i,j] to Mp[Mp_i, j] is opts.missingdistance.
      // However, if the user doesn't specify this, then the entire
      // M[i] to Mp[Mp_i] distance is set as missing.
      if ((M(i, j) == MISSING) || (Mp(Mp_i, j) == MISSING)) {
        if (opts.missingdistance == 0) {
          dist_i = MISSING;
          break;
        } else {
          dist_ij = opts.missingdistance;
        }
      } else { // Neither M[i,j] nor Mp[Mp_i, j] is missing.
        // How do we compare them? Do we treat them like continuous values and subtract them,
        // or treat them like unordered categorical variables and just check if they're the same?
        if (opts.metrics[j] == Metric::Diff) {
          dist_ij = M(i, j) - Mp(Mp_i, j);
        } else { // Metric::CheckSame
          dist_ij = (M(i, j) != Mp(Mp_i, j));
        }
      }

      if (opts.distance == Distance::MeanAbsoluteError) {
        dist_i += abs(dist_ij) / M.E_actual();
      } else { // Distance::Euclidean
        dist_i += dist_ij * dist_ij;
      }
    }

    if (dist_i != 0 && dist_i != MISSING) {
      if (opts.distance == Distance::MeanAbsoluteError) {
        dists.push_back(dist_i);
      } else { // Distance::Euclidean
        dists.push_back(sqrt(dist_i));
      }
      inds.push_back(i);
    }
  }

  return { inds, dists };
}

// This function compares the M(i,.) multivariate time series to the Mp(j,.) multivariate time series.
// The M(i,.) observation has data for E consecutive time points (e.g. time(i), time(i+1), ..., time(i+E-1)) and
// the Mp(j,.) observation corresponds to E consecutive time points (e.g. time(j), time(j+1), ..., time(j+E-1)).
// At each time instant we observe n >= 1 pieces of data.
// These may either be continuous data or unordered categorical data.
//
// The Wasserstein distance (using the 'curve-matching' strategy) is equivalent to the (minimum) cost of turning
// the first time series into the second time series. In a simple example, say E = 2 and n = 1, and
//         M(i,.) = [ 1, 2 ] and Mp(j,.) = [ 2, 2 ].
// To turn M(i,.) into Mp(j,.) the first element needs to be increased by 1, so the overall cost is
//         Wasserstein( M(i,.), Mp(j,.) ) = 1.
// The distance can also reorder the points, so for example say
//         M(i,.) = [ 1, 100 ] and Mp(j,.) = [ 100, 1 ].
// If we just change the 1 to 100 and the 100 to 1 then the cost of each is 99 + 99 = 198.
// However, Wasserstein can instead reorder these points at a cost of
//         Wasserstein( M(i,.), Mp(j,.) ) = 2 * gamma * (time(1)-time(2))
// so if the observations occur on a regular grid so time(i) = i then the distance will just be 2 * gamma.
//
// The return value of this function is a matrix which shows the pairwise costs associated to each
// potential Wasserstein solution. E.g. the (n,m) element of the returned matrix shows the cost
// of turning the individual point M(i, n) into Mp(j, m).
//
// When there are missing values in one or other observation, we can either ignore this time period
// and compute the Wasserstein for the mismatched regime where M(i,.) is of size len_i and Mp(j,.) is
// of size len_j, where len_i != len_j is possible. Alternatively, we can fill in the affected elements
// of the cost matrix with some user-supplied 'missingDistance' value and then len_i == len_j is upheld.
std::unique_ptr<double[]> wasserstein_cost_matrix(const Manifold& M, const Manifold& Mp, int i, int j, double gamma,
                                                  const Options& opts, int& len_i, int& len_j)
{
  // The M(i,.) observation will be stored as one flat vector of length M.E_actual():
  // - the first M.E() observations will the lagged version of the main time series
  // - the next M.E() observations will be the lagged 'dt' time series (if it is included, i.e., if M.E_dt() > 0)
  // - the next n * M.E() observations will be the n lagged extra variables,
  //   so in total that is M.E_lagged_extras() = n * M.E() observations
  // - the remaining M.E_actual() - M.E() - M.E_dt() - M.E_lagged_extras() are the unlagged extras and the distance
  //   between those two vectors forms a kind of minimum distance which is added to the time-series curve matching
  //   Wasserstein distance.

  bool skipMissing = (opts.missingdistance == 0);

  auto M_i = M.laggedObsMap(i);
  auto Mp_j = Mp.laggedObsMap(j);

  auto M_i_missing = (M_i.array() == M.missing()).colwise().any();
  auto Mp_j_missing = (Mp_j.array() == Mp.missing()).colwise().any();

  if (skipMissing) {
    len_i = M.E() - M_i_missing.sum();
    len_j = Mp.E() - Mp_j_missing.sum();
  } else {
    len_i = M.E();
    len_j = Mp.E();
  }

  int timeSeriesDim = M_i.rows();

  double unlaggedDist = 0.0;
  int numUnlaggedExtras = M.E_extras() - M.E_lagged_extras();
  for (int e = 0; e < numUnlaggedExtras; e++) {
    double x = M.unlagged_extras(i, e), y = Mp.unlagged_extras(j, e);
    bool eitherMissing = (x == M.missing()) || (y == M.missing());

    if (eitherMissing) {
      unlaggedDist += opts.missingdistance;
    } else {
      if (opts.metrics[timeSeriesDim + e] == Metric::Diff) {
        unlaggedDist += abs(x - y);
      } else {
        unlaggedDist += x != y;
      }
    }
  }

  auto flatCostMatrix = std::make_unique<double[]>(len_i * len_j);
  std::fill_n(flatCostMatrix.get(), len_i * len_j, unlaggedDist);
  Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> costMatrix(flatCostMatrix.get(),
                                                                                                len_i, len_j);

  for (int k = 0; k < timeSeriesDim; k++) {
    int n = 0;
    for (int nn = 0; nn < M_i.cols(); nn++) {
      int m = 0;
      for (int mm = nn; mm < Mp_j.cols(); mm++) {
        if (skipMissing && (M_i_missing[nn] || Mp_j_missing[mm])) {
          continue;
        }
        double dist;
        bool eitherMissing = M_i_missing[nn] || Mp_j_missing[mm];

        if (eitherMissing) {
          dist = opts.missingdistance;
        } else {
          if (opts.metrics[k] == Metric::Diff) {
            dist = abs(M_i(k, nn) - Mp_j(k, mm));
          } else {
            dist = M_i(k, nn) != Mp_j(k, mm);
          }
        }

        // For the first-time around, also add the time differences into the cost matrix.
        if (k == 0) {
          // TODO: Add the time variable into here.
          dist += gamma * abs(nn - mm);
        }

        costMatrix(n, m) += dist;

        // As the overall cost matrix is symmetric, just mirror the off-diagonal
        // entries into the other half.
        if (n != m) {
          costMatrix(m, n) += dist;
        }

        m += 1;
      }

      n += 1;
    }
  }
  return flatCostMatrix;
}

double approx_wasserstein(double* C, int len_i, int len_j, double eps, double stopErr)
{
  Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> costMatrix(C, len_i, len_j);

  double r = 1.0 / len_i;
  double c = 1.0 / len_j;

  Eigen::MatrixXd K = Eigen::exp(-costMatrix.array() / eps);
  Eigen::MatrixXd Kp = len_i * K.array();

  Eigen::VectorXd u = Eigen::VectorXd::Ones(len_i) / len_i;
  Eigen::VectorXd v = Eigen::VectorXd::Ones(len_j) / len_j;

  int maxIter = 10000;
  for (int iter = 0; iter < maxIter; iter++) {

    v = c / (K.transpose() * u).array();
    u = 1.0 / (Kp * v).array();

    if (iter % 10 == 0) {
      // Compute right marginal (diag(u) K diag(v))^T1
      Eigen::VectorXd tempColSums = (u.asDiagonal() * K * v.asDiagonal()).colwise().sum();
      double L2err = (tempColSums.array() - c).matrix().norm();
      if (L2err < stopErr) {
        break;
      }
    }
  }

  Eigen::MatrixXd transportPlan = u.asDiagonal() * K * v.asDiagonal();
  double dist = (transportPlan.array() * costMatrix.array()).sum();
  return dist;
}

double wasserstein(double* C, int len_i, int len_j)
{
  // Create vectors which are just 1/len_i and 1/len_j of length len_i and len_j.
  auto w_1 = std::make_unique<double[]>(len_i);
  auto w_2 = std::make_unique<double[]>(len_j);
  for (int i = 0; i < len_i; i++) {
    w_1[i] = 1.0 / len_i;
  }
  for (int i = 0; i < len_j; i++) {
    w_2[i] = 1.0 / len_j;
  }

  int maxIter = 10000;
  double cost;
  EMD_wrap(len_i, len_j, w_1.get(), w_2.get(), C, nullptr, nullptr, nullptr, &cost, maxIter);
  return cost;
}

DistanceIndexPairs wasserstein_distances(int Mp_i, const Options& opts, const Manifold& M, const Manifold& Mp,
                                         std::vector<int> inpInds)
{
  std::vector<int> inds;
  std::vector<double> dists;

  // PJL: A test fails if using Mp.range() / Mp.time_range() here.
  double gamma = M.range() / M.time_range() * opts.aspectRatio;

  // Compare every observation in the M manifold to the
  // Mp_i'th observation in the Mp manifold.
  for (int i : inpInds) {
    int len_i, len_j;
    auto C = wasserstein_cost_matrix(M, Mp, i, Mp_i, gamma, opts, len_i, len_j);

    if (len_i > 0 && len_j > 0) {
      double dist_i = wasserstein(C.get(), len_i, len_j);
      if (dist_i != 0) {
        dists.push_back(dist_i);
        inds.push_back(i);
      }
    }
  }

  return { inds, dists };
}