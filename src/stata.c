/* Suppress Windows problems with sprintf etc. functions. */
#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include "edm.h"
#include "stplugin.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DUMP_INPUT
#include <hdf5.h>
#include <hdf5_hl.h>
#endif

ST_retcode print_error(ST_retcode rc)
{
  char temps[500];

  switch (rc) {
    case MALLOC_ERROR:
      sprintf(temps, "Insufficient memory\n");
      break;
    case NOT_IMPLEMENTED:
      sprintf(temps, "Method is not yet implemented\n");
      break;
    case INSUFFICIENT_UNIQUE:
      sprintf(temps, "Insufficient number of unique observations, consider "
                     "tweaking the values of E, k or use -force- option\n");
      break;
    case INVALID_ALGORITHM:
      sprintf(temps, "Invalid algorithm argument\n");
      break;
  }

  if (rc != SUCCESS) {
    SF_error(temps);
  }

  return rc;
}

/*
 * Count the number of rows that aren't being filtered out
 * by Stata's 'if' or 'in' expressions.
 */
static int num_if_in_rows()
{
  int num = 0;
  for (ST_int i = SF_in1(); i <= SF_in2(); i++) {
    if (SF_ifobs(i)) {
      num += 1;
    }
  }
  return num;
}

/*
 * Read in columns from Stata (i.e. what Stata calls variables).
 *
 * Starting from column number 'j0', read in 'numCols' of columns.
 * The result is stored in the 'out' variable, and the column sum in 'outSum'.
 *
 * If 'filter' is not NULL, we consider each row 'i' only if 'filter[i]'
 * evaluates to true. To allocate properly the correct amount, pass in
 * the 'numFiltered' argument which is the total number of rows which are
 * true in the filter.
 */
static ST_retcode stata_columns_filtered(const ST_double* filter, int numFiltered, ST_int j0, int numCols, double** out,
                                         double* outSum)
{
  // Allocate space for the matrix of data from Stata
  int numRows = (filter == NULL) ? num_if_in_rows() : numFiltered;
  double* M = (double*)malloc(sizeof(double) * numRows * numCols);
  if (M == NULL) {
    return print_error(MALLOC_ERROR);
  }

  int ind = 0; // Flattened index of M matrix
  ST_retcode rc = 0;
  ST_double value = 0;
  ST_double sum = 0;

  int r = 0; // Count each row that isn't filtered by Stata 'if'
  for (ST_int i = SF_in1(); i <= SF_in2(); i++) {
    if (SF_ifobs(i)) {                   // Skip rows according to Stata's 'if'
      if (filter == NULL || filter[r]) { // Skip rows given our own filter
        for (ST_int j = j0; j < j0 + numCols; j++) {
          rc = SF_vdata(j, i, &value);
          if (rc) {
            free(M);
            return rc;
          }

          // Set missing values to MISSING
          if (!SF_is_missing(value)) {
            M[ind] = value;
            sum += value;
          } else {
            M[ind] = MISSING;
          }
          ind += 1;
        }
      }
      r += 1;
    }
  }

  *out = M;
  if (outSum != NULL) {
    *outSum = sum;
  }

  return SUCCESS;
}

/*
 * Write data to columns in Stata (i.e. what Stata calls variables).
 *
 * Starting from column number 'j0', write 'numCols' of columns.
 * The data being written is in the 'toSave' parameter, which is a
 * flattened row-major array.
 *
 * If 'filter' is not NULL, we consider each row 'i' only if 'filter[i]'
 * evaluates to true.
 */
static ST_retcode write_stata_columns_filtered(const ST_double* filter, ST_int j0, int numCols, const double* toSave)
{
  int ind = 0; // Index of y vector
  ST_retcode rc = 0;
  ST_double value = 0;

  int r = 0; // Count each row that isn't filtered by Stata 'if'
  for (ST_int i = SF_in1(); i <= SF_in2(); i++) {
    if (SF_ifobs(i)) {                   // Skip rows according to Stata's 'if'
      if (filter == NULL || filter[r]) { // Skip rows given our own filter
        for (ST_int j = j0; j < j0 + numCols; j++) {
          // Convert MISSING back to Stata's missing value
          value = (toSave[ind] == MISSING) ? SV_missval : toSave[ind];
          rc = SF_vstore(j, i, value);
          if (rc) {
            return rc;
          }
          ind += 1;
        }
      }
      r += 1;
    }
  }

  return SUCCESS;
}

static ST_retcode stata_column_filtered(const ST_double* filter, int numFiltered, ST_int j, double** out,
                                        double* outSum)
{
  return stata_columns_filtered(filter, numFiltered, j, 1, out, outSum);
}

static ST_retcode stata_column(ST_int j, double** out, double* outSum)
{
  return stata_columns_filtered(NULL, -1, j, 1, out, outSum);
}

static ST_retcode write_stata_column_filtered(const ST_double* filter, ST_int j, const double* toSave)
{
  return write_stata_columns_filtered(filter, j, 1, toSave);
}

void print_debug_info(int argc, char* argv[], ST_double theta, char* algorithm, bool force_compute,
                      ST_double missingdistance, ST_int mani, ST_int count_train_set, ST_int count_predict_set,
                      bool pmani_flag, ST_int pmani, ST_int l, bool save_mode, ST_int varssv, ST_int nthreads)
{
  char temps[500];

  /* header of the plugin */
  SF_display("\n====================\n");
  SF_display("Start of the plugin\n\n");

  /* overview of variables and arguments passed and observations in sample */
  sprintf(temps, "number of vars & obs = %i, %i\n", SF_nvars(), SF_nobs());
  SF_display(temps);
  sprintf(temps, "first and last obs in sample = %i, %i\n\n", SF_in1(), SF_in2());
  SF_display(temps);

  for (int i = 0; i < argc; i++) {
    sprintf(temps, "arg %i: %s\n", i, argv[i]);
    SF_display(temps);
  }
  SF_display("\n");

  sprintf(temps, "theta = %6.4f\n\n", theta);
  SF_display(temps);
  sprintf(temps, "algorithm = %s\n\n", algorithm);
  SF_display(temps);
  sprintf(temps, "force compute = %i\n\n", force_compute);
  SF_display(temps);
  sprintf(temps, "missing distance = %f\n\n", missingdistance);
  SF_display(temps);
  sprintf(temps, "number of variables in manifold = %i\n\n", mani);
  SF_display(temps);
  sprintf(temps, "train set obs: %i\n", count_train_set);
  SF_display(temps);
  sprintf(temps, "predict set obs: %i\n\n", count_predict_set);
  SF_display(temps);
  sprintf(temps, "p_manifold flag = %i\n", pmani_flag);
  SF_display(temps);

  if (pmani_flag) {
    sprintf(temps, "number of variables in p_manifold = %i\n", pmani);
    SF_display(temps);
  }
  SF_display("\n");

  sprintf(temps, "l = %i\n\n", l);
  SF_display(temps);

  if (save_mode) {
    sprintf(temps, "columns in smap coefficents = %i\n", varssv);
    SF_display(temps);
  }

  sprintf(temps, "save_mode = %i\n\n", save_mode);
  SF_display(temps);

  sprintf(temps, "Requested %s OpenMP threads\n", argv[9]);
  SF_display(temps);
  sprintf(temps, "Using %i OpenMP threads\n\n", nthreads);
  SF_display(temps);
}

/*
Example call to the plugin:

local myvars ``manifold'' `co_mapping' `x_f' `x_p' `train_set' `predict_set' `overlap' `vars_save'

unab vars : ``manifold''
local mani `: word count `vars''

local pmani_flag = 0

local vsave_flag = 0

plugin call smap_block_mdap `myvars', `j' `lib_size' "`algorithm'" "`force'" `missingdistance' `mani' `pmani_flag'
`vsave_flag'
*/
STDLL stata_call(int argc, char* argv[])
{
  bool force_compute, pmani_flag, save_mode;
  char* algorithm;
  ST_retcode rc;
  ST_int mani, pmani, Mpcol, l, varssv, nthreads;
  ST_int count_train_set, count_predict_set;
  ST_double theta, missingdistance;
  ST_double *train_use, *predict_use, *y, *S, *ystar;

  ST_int verbosity = atoi(argv[10]);

  theta = atof(argv[0]); /* contains value of theta = first argument */

  /* allocation of string variable algorithm based on third argument */
  algorithm = argv[2];

  /* allocation of variable force_compute based on fourth argument */
  force_compute = (strcmp(argv[3], "force") == 0);

  /* allocation of variable missingdistance based on fifth argument */
  missingdistance = atof(argv[4]);

  /* allocation of number of columns in manifold */
  mani = atoi(argv[5]);

  /* allocation of train_use, predict_use and S (prev. skip_obs) variables */
  ST_double sum;

  ST_int stataVarNum = mani + 3;
  rc = stata_column(stataVarNum, &train_use, &sum);
  if (rc) {
    return print_error(rc);
  }
  count_train_set = (int)sum;

  stataVarNum = mani + 4;
  rc = stata_column(stataVarNum, &predict_use, &sum);
  if (rc) {
    return print_error(rc);
  }
  count_predict_set = (int)sum;

  stataVarNum = mani + 5;
  rc = stata_column_filtered(predict_use, count_predict_set, stataVarNum, &S, NULL);
  if (rc) {
    return print_error(rc);
  }

  /* allocation of matrices M and y */
  stataVarNum = 1;
  ST_double* flat_M = NULL;
  rc = stata_columns_filtered(train_use, count_train_set, stataVarNum, mani, &flat_M, NULL);
  if (rc) {
    return print_error(rc);
  }

  stataVarNum = mani + 1;
  rc = stata_column_filtered(train_use, count_train_set, stataVarNum, &y, NULL);
  if (rc) {
    return print_error(rc);
  }

  /* allocation of matrices Mp, S, ystar */
  pmani_flag = atoi(argv[6]); /* contains the flag for p_manifold */
  pmani = 0;
  if (pmani_flag) {
    pmani = atoi(argv[8]); /* contains the number of columns in p_manifold */
    Mpcol = pmani;
  } else {
    Mpcol = mani;
  }

  ST_double* flat_Mp = NULL;
  if (pmani_flag) {
    Mpcol = pmani;
    stataVarNum = mani + 6;
  } else {
    Mpcol = mani;
    stataVarNum = 1;
  }
  rc = stata_columns_filtered(predict_use, count_predict_set, stataVarNum, Mpcol, &flat_Mp, NULL);
  if (rc) {
    return rc;
  }

  l = atoi(argv[1]); /* contains l */
  if (l <= 0) {
    l = mani + 1;
  }

  save_mode = atoi(argv[7]); /* contains the flag for vars_save */

  double* flat_Bi_map = NULL;

  if (save_mode) {          /* flag savesmap is ON */
    varssv = atoi(argv[8]); /* contains the number of columns
                               in smap coefficents */
    flat_Bi_map = malloc(sizeof(ST_double) * count_predict_set * varssv);
    if (flat_Bi_map == NULL) {
      return print_error(MALLOC_ERROR);
    }
  } else { /* flag savesmap is OFF */
    varssv = 0;
  }

  ystar = (ST_double*)malloc(sizeof(ST_double) * count_predict_set);
  if (ystar == NULL) {
    return print_error(MALLOC_ERROR);
  }

#ifdef DUMP_INPUT
  // Here we want to dump the input so we can use it without stata for
  // debugging and profiling purposes.
  if (argc >= 12) {
    hid_t fid = H5Fcreate(argv[11], H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

    H5LTset_attribute_int(fid, "/", "count_train_set", &count_train_set, 1);
    H5LTset_attribute_int(fid, "/", "count_predict_set", &count_predict_set, 1);
    H5LTset_attribute_int(fid, "/", "Mpcol", &Mpcol, 1);
    H5LTset_attribute_int(fid, "/", "mani", &mani, 1);

    H5LTmake_dataset_double(fid, "y", 1, (hsize_t[]){ count_train_set }, y);

    H5LTset_attribute_int(fid, "/", "l", &l, 1);
    H5LTset_attribute_double(fid, "/", "theta", &theta, 1);

    H5LTmake_dataset_double(fid, "S", 1, (hsize_t[]){ count_predict_set }, S);

    H5LTset_attribute_string(fid, "/", "algorithm", algorithm);
    H5LTset_attribute_int(fid, "/", "save_mode", (int*)&save_mode, 1);
    H5LTset_attribute_int(fid, "/", "force_compute", (int*)&force_compute, 1);
    H5LTset_attribute_int(fid, "/", "varssv", &varssv, 1);
    H5LTset_attribute_double(fid, "/", "missingdistance", &missingdistance, 1);

    H5LTmake_dataset_double(fid, "flat_Mp", 1, (hsize_t[]){ count_predict_set * Mpcol }, flat_Mp);
    H5LTmake_dataset_double(fid, "flat_M", 1, (hsize_t[]){ count_train_set * mani }, flat_M);

    H5Fclose(fid);
  }
#endif

  /* setting the number of OpenMP threads */
  nthreads = atoi(argv[9]);
  nthreads = nthreads <= 0 ? omp_get_num_procs() : nthreads;

  // Find the number of threads Stata was already using, so we can reset to this later.
  int originalNumThreads;
#pragma omp parallel
  {
    originalNumThreads = omp_get_num_threads();
  }
  omp_set_num_threads(nthreads);

  // Ask OpenMP how many threads it's using, in case it ignored our request in the previous line.
#pragma omp parallel
  {
    nthreads = omp_get_num_threads();
  }

  if (verbosity > 0) {
    print_debug_info(argc, argv, theta, algorithm, force_compute, missingdistance, mani, count_train_set,
                     count_predict_set, pmani_flag, pmani, l, save_mode, varssv, nthreads);
  }

  rc = mf_smap_loop(count_predict_set, count_train_set, mani, Mpcol, flat_M, flat_Mp, y, l, theta, S, algorithm,
                    save_mode, varssv, force_compute, missingdistance, ystar, flat_Bi_map);

  omp_set_num_threads(originalNumThreads);

  /* If there are no errors, return the value of ystar (and smap coefficients) to Stata */
  if (rc == SUCCESS) {
    stataVarNum = mani + 2;
    rc = write_stata_column_filtered(predict_use, stataVarNum, ystar);

    if (rc == SUCCESS && save_mode) {
      stataVarNum = mani + 5 + 1 + (int)pmani_flag * pmani;
      rc = write_stata_columns_filtered(predict_use, stataVarNum, varssv, flat_Bi_map);
    }
  }

  // If mf_smap_loop gave an error, or saving the output to Stata gave an error,
  // then print it out now before the 'end of the plugin' footer.
  print_error(rc);

  /* deallocation of matrices and arrays before exiting the plugin */
  free(train_use);
  free(predict_use);
  free(S);
  free(flat_M);
  free(y);
  free(flat_Mp);
  if (save_mode) {
    free(flat_Bi_map);
  }
  free(ystar);

  /* footer of the plugin */
  if (verbosity > 0) {
    SF_display("\nEnd of the plugin\n");
    SF_display("====================\n\n");
  }

  return rc;
}
