#ifndef COMMON_INCLUDES
#define COMMON_INCLUDES

#ifdef YCM
#define private public
#endif

#include "config.h"

#include <fstream>

namespace wf_debug {
    extern std::ofstream logfile;
}

#if WAYFIRE_DEBUG_ENABLED
#define debug_output_if if(1)
#else
#define debug_output_if if(0)
#endif

#define debug debug_output_if wf_debug::logfile << "[DD] "

#define info  wf_debug::logfile  << "[II] "
#define errio wf_debug::logfile  << "[EE] "

#endif
