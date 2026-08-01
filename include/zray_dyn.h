// ZRay: portable compiler-assisted memory traffic characterization.
// Interfaces and data structures for the ZRay runtime.
//
// Authors: Hayden Coffey
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

#ifndef ZRAY_DYN_H
#define ZRAY_DYN_H

#ifdef __cplusplus
  #define EXPORT_C extern "C"
#else
  #define EXPORT_C
#endif

#ifdef __cplusplus
#include <cstdlib>
#include <cctype>
#include <iostream>
#include <map>
#include <xray/xray_log_interface.h>
#include <xray/xray_interface.h>
#include <utility>
#include <vector>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <unistd.h>
#include <time.h>
#include <mutex>
#include <cmath>
#include "zray_data.h"

void read_pd_sets(std::vector<std::pair<std::string, zray::ProfileData> > & Counts,
        std::vector<std::pair<std::string, zray::ProfileData> > & IndirectProfiles);

#endif

EXPORT_C size_t return_counter_element(int index);
EXPORT_C void print_counter_element(int index);
EXPORT_C void print_counter_array();
EXPORT_C void zray_finalize();

#endif
