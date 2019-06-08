#ifndef WF_OPENGL_PRIV_HPP
#define WF_OPENGL_PRIV_HPP

#include <opengl.hpp>
#include <output.hpp>

namespace OpenGL
{
/** Initialize OpenGL helper functions */
void init();
/** Destroy the default GL program and resources */
void fini();
/** Indicate we have started repainting the given output */
void bind_output(wf::output_t *output);
/** Indicate the output frame has been finished */
void unbind_output(wf::output_t *output);
}

#endif /* end of include guard: WF_OPENGL_PRIV_HPP */
