#include <benchmark/benchmark.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "driver.h"
#include "edm.h"

#include <Eigen/SVD>
typedef Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> MatrixView;

std::vector<size_t> minindex(const std::vector<double>& v, int k);

#include <array>

// Inputs generated by 'perf-test.do' script
std::array<std::string, 3> tests = {
  "logmapsmall.h5", // "edm explore x, e(10)" on 200 obs of logistic map
  "logmaplarge.h5", // "edm xmap x y, theta(0.2) algorithm(smap)" on ~50k obs of logistic map
  "affectsmall.h5"  // "edm xmap PA NA, dt e(10) k(-1) force alg(smap)" on ~5k obs of affect data
};

void get_distances(int Mp_i, smap_opts_t opts, const std::vector<double>& y, const MatrixView& M, const MatrixView& Mp)
{
  int validDistances = 0;
  std::vector<double> d(M.rows());
  auto b = Mp.row(Mp_i);

  for (int i = 0; i < M.rows(); i++) {
    double dist = 0.;
    bool missing = false;
    int numMissingDims = 0;
    for (int j = 0; j < M.cols(); j++) {
      if ((M(i, j) == MISSING) || (b(j) == MISSING)) {
        if (opts.missingdistance == 0) {
          missing = true;
          break;
        }
        numMissingDims += 1;
      } else {
        dist += (M(i, j) - b(j)) * (M(i, j) - b(j));
      }
    }
    // If the distance between M_i and b is 0 before handling missing values,
    // then keep it at 0. Otherwise, add in the correct number of missingdistance's.
    if (dist != 0) {
      dist += numMissingDims * opts.missingdistance * opts.missingdistance;
    }

    if (missing || dist == 0.) {
      d[i] = MISSING;
    } else {
      d[i] = dist;
      validDistances += 1;
    }
  }
}

static void bm_get_distances(benchmark::State& state)
{
  std::string input = tests[state.range(0)];
  state.SetLabel(input);

  edm_inputs_t vars = read_dumpfile(input);

  MatrixView M((double*)vars.M.flat.data(), vars.M.rows, vars.M.cols);
  MatrixView Mp((double*)vars.Mp.flat.data(), vars.Mp.rows, vars.Mp.cols);

  int Mp_i = 0;
  for (auto _ : state) {
    get_distances(Mp_i, vars.opts, vars.y, M, Mp);
    Mp_i = (Mp_i + 1) % vars.Mp.rows;
  }
}

BENCHMARK(bm_get_distances)->DenseRange(0, tests.size() - 1)->Unit(benchmark::kMillisecond);

static void bm_nearest_neighbours(benchmark::State& state)
{
  std::string input = tests[state.range(0)];
  state.SetLabel(input);

  edm_inputs_t vars = read_dumpfile(input);

  MatrixView M((double*)vars.M.flat.data(), vars.M.rows, vars.M.cols);
  MatrixView Mp((double*)vars.Mp.flat.data(), vars.Mp.rows, vars.Mp.cols);

  int Mp_i = 0;
  smap_opts_t opts = vars.opts;

  int validDistances = 0;
  std::vector<double> d(M.rows());
  auto b = Mp.row(Mp_i);

  for (int i = 0; i < M.rows(); i++) {
    double dist = 0.;
    bool missing = false;
    int numMissingDims = 0;
    for (int j = 0; j < M.cols(); j++) {
      if ((M(i, j) == MISSING) || (b(j) == MISSING)) {
        if (opts.missingdistance == 0) {
          missing = true;
          break;
        }
        numMissingDims += 1;
      } else {
        dist += (M(i, j) - b(j)) * (M(i, j) - b(j));
      }
    }
    // If the distance between M_i and b is 0 before handling missing values,
    // then keep it at 0. Otherwise, add in the correct number of missingdistance's.
    if (dist != 0) {
      dist += numMissingDims * opts.missingdistance * opts.missingdistance;
    }

    if (missing || dist == 0.) {
      d[i] = MISSING;
    } else {
      d[i] = dist;
      validDistances += 1;
    }
  }

  int l = opts.l;

  for (auto _ : state) {
    std::vector<size_t> ind = minindex(d, l);
  }
}

BENCHMARK(bm_nearest_neighbours)->DenseRange(0, tests.size() - 1);

static void bm_simplex(benchmark::State& state)
{
  std::string input = tests[state.range(0)];
  state.SetLabel(input);

  edm_inputs_t vars = read_dumpfile(input);

  MatrixView M((double*)vars.M.flat.data(), vars.M.rows, vars.M.cols);
  MatrixView Mp((double*)vars.Mp.flat.data(), vars.Mp.rows, vars.Mp.cols);

  int Mp_i = 0;
  smap_opts_t opts = vars.opts;
  std::vector<double> y = vars.y;

  int validDistances = 0;
  std::vector<double> d(M.rows());
  auto b = Mp.row(Mp_i);

  for (int i = 0; i < M.rows(); i++) {
    double dist = 0.;
    bool missing = false;
    int numMissingDims = 0;
    for (int j = 0; j < M.cols(); j++) {
      if ((M(i, j) == MISSING) || (b(j) == MISSING)) {
        if (opts.missingdistance == 0) {
          missing = true;
          break;
        }
        numMissingDims += 1;
      } else {
        dist += (M(i, j) - b(j)) * (M(i, j) - b(j));
      }
    }
    // If the distance between M_i and b is 0 before handling missing values,
    // then keep it at 0. Otherwise, add in the correct number of missingdistance's.
    if (dist != 0) {
      dist += numMissingDims * opts.missingdistance * opts.missingdistance;
    }

    if (missing || dist == 0.) {
      d[i] = MISSING;
    } else {
      d[i] = dist;
      validDistances += 1;
    }
  }

  int l = opts.l;

  std::vector<size_t> ind = minindex(d, l);

  double d_base = d[ind[0]];
  std::vector<double> w(l);

  double sumw = 0., r = 0.;

  for (auto _ : state) {
    for (int j = 0; j < l; j++) {
      /* TO BE ADDED: benchmark pow(expression,0.5) vs sqrt(expression) */
      /* w[j] = exp(-theta*pow((d[ind[j]] / d_base),(0.5))); */
      w[j] = exp(-opts.theta * sqrt(d[ind[j]] / d_base));
      sumw = sumw + w[j];
    }
    for (int j = 0; j < l; j++) {
      r = r + y[ind[j]] * (w[j] / sumw);
    }
  }
}

BENCHMARK(bm_simplex)->DenseRange(0, tests.size() - 1);

static void bm_smap(benchmark::State& state)
{
  std::string input = tests[state.range(0)];
  state.SetLabel(input);

  edm_inputs_t vars = read_dumpfile(input);

  MatrixView M((double*)vars.M.flat.data(), vars.M.rows, vars.M.cols);
  MatrixView Mp((double*)vars.Mp.flat.data(), vars.Mp.rows, vars.Mp.cols);

  int Mp_i = 0;
  smap_opts_t opts = vars.opts;
  std::vector<double> y = vars.y;

  int validDistances = 0;
  std::vector<double> d(M.rows());
  auto b = Mp.row(Mp_i);

  for (int i = 0; i < M.rows(); i++) {
    double dist = 0.;
    bool missing = false;
    int numMissingDims = 0;
    for (int j = 0; j < M.cols(); j++) {
      if ((M(i, j) == MISSING) || (b(j) == MISSING)) {
        if (opts.missingdistance == 0) {
          missing = true;
          break;
        }
        numMissingDims += 1;
      } else {
        dist += (M(i, j) - b(j)) * (M(i, j) - b(j));
      }
    }
    // If the distance between M_i and b is 0 before handling missing values,
    // then keep it at 0. Otherwise, add in the correct number of missingdistance's.
    if (dist != 0) {
      dist += numMissingDims * opts.missingdistance * opts.missingdistance;
    }

    if (missing || dist == 0.) {
      d[i] = MISSING;
    } else {
      d[i] = dist;
      validDistances += 1;
    }
  }

  int l = opts.l;

  std::vector<size_t> ind = minindex(d, l);

  double d_base = d[ind[0]];
  std::vector<double> w(l);
  double sumw = 0., r = 0.;

  for (auto _ : state) {
    Eigen::MatrixXd X_ls(l, M.cols());
    std::vector<double> y_ls(l), w_ls(l);

    double mean_w = 0.;
    for (int j = 0; j < l; j++) {
      /* TO BE ADDED: benchmark pow(expression,0.5) vs sqrt(expression) */
      /* w[j] = pow(d[ind[j]],0.5); */
      w[j] = sqrt(d[ind[j]]);
      mean_w = mean_w + w[j];
    }
    mean_w = mean_w / (double)l;
    for (int j = 0; j < l; j++) {
      w[j] = exp(-opts.theta * (w[j] / mean_w));
    }

    int rowc = -1;
    for (int j = 0; j < l; j++) {
      if (y[ind[j]] == MISSING) {
        continue;
      }
      bool anyMissing = false;
      for (int i = 0; i < M.cols(); i++) {
        if (M(ind[j], i) == MISSING) {
          anyMissing = true;
          break;
        }
      }
      if (anyMissing) {
        continue;
      }
      rowc++;

      y_ls[rowc] = y[ind[j]] * w[j];
      w_ls[rowc] = w[j];
      for (int i = 0; i < M.cols(); i++) {
        X_ls(rowc, i) = M(ind[j], i) * w[j];
      }
    }
    if (rowc == -1) {
      continue;
    }

    // Pull out the first 'rowc+1' elements of the y_ls vector and
    // concatenate the column vector 'w' with 'X_ls', keeping only
    // the first 'rowc+1' rows.
    Eigen::VectorXd y_ls_cj(rowc + 1);
    Eigen::MatrixXd X_ls_cj(rowc + 1, M.cols() + 1);

    for (int i = 0; i < rowc + 1; i++) {
      y_ls_cj(i) = y_ls[i];
      X_ls_cj(i, 0) = w_ls[i];
      for (int j = 1; j < X_ls.cols() + 1; j++) {
        X_ls_cj(i, j) = X_ls(i, j - 1);
      }
    }

    Eigen::BDCSVD<Eigen::MatrixXd> svd(X_ls_cj, Eigen::ComputeThinU | Eigen::ComputeThinV);
    Eigen::VectorXd ics = svd.solve(y_ls_cj);

    r = ics(0);
    for (int j = 1; j < M.cols() + 1; j++) {
      if (b(j - 1) != MISSING) {
        r += b(j - 1) * ics(j);
      }
    }
  }
}

BENCHMARK(bm_smap)->DenseRange(0, tests.size() - 1);

std::array<int, 4> nthreads = { 1, 2, 4, 8 };

static void bm_mf_smap_loop(benchmark::State& state)
{
  int testNum = ((int)state.range(0)) / ((int)nthreads.size());
  int threads = nthreads[state.range(0) % nthreads.size()];

  std::string input = tests[testNum];
  state.SetLabel(fmt::format("{} ({} threads)", input, threads));

  edm_inputs_t vars = read_dumpfile(input);

  vars.nthreads = threads;

  for (auto _ : state)
    smap_res_t res = mf_smap_loop(vars.opts, vars.y, vars.M, vars.Mp, vars.nthreads);
}

BENCHMARK(bm_mf_smap_loop)
  ->DenseRange(0, tests.size() * nthreads.size() - 1)
  ->MeasureProcessCPUTime()
  ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();