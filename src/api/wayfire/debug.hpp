#ifndef DEBUG_HPP
#define DEBUG_HPP

#ifndef WAYFIRE_PLUGIN
    #include "config.h"
#endif

#define nonull(x) ((x) ? (x) : ("nil"))
#include <wayfire/util/log.hpp>

namespace wf
{
/**
 * Print the current stacktrace at runtime.
 *
 * @param fast_mode If fast_mode is true, the stacktrace will be generated
 *   using the fastest possible method. However, this means that not all
 *   information will be printed (for ex., line numbers may be missing).
 */
void print_trace(bool fast_mode);
}

/* ------------------- Miscallaneous helpers for debugging ------------------ */
#include <ostream>
#include <glm/glm.hpp>
#include <wayfire/geometry.hpp>

std::ostream& operator <<(std::ostream& out, const glm::mat4& mat);
wf::pointf_t operator *(const glm::mat4& m, const wf::pointf_t& p);
wf::pointf_t operator *(const glm::mat4& m, const wf::point_t& p);

#endif
