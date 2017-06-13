#ifndef COMMON_INCLUDES
#define COMMON_INCLUDES

#ifdef YCM
#define private public
#endif

#include <fstream>
extern std::ofstream file_info, file_null;
extern std::ofstream* file_debug;

#define debug (*file_debug) << "[DD] "
#define info  file_info  << "[II] "
#define errio file_info  << "[EE] "

#include "config.h"
#define USE_GLES3 1

#endif
