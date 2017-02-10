#ifndef IMG_HPP_
#define IMG_HPP_

#include "commonincludes.hpp"
#include <GLES2/gl2.h>

#define ulong unsigned long

namespace image_io {
    /* Function that returns GL texture from the given files using the
     * appropriate decoder(currently jpeg or png)
     * Returns -1 on failure */
    GLuint load_from_file(std::string name, ulong& x, ulong& y);

    /* Function that saves the given pixels(in rgba format) to a (currently) png file */
    void write_to_file(std::string name, uint8_t *pixels, int w, int h, std::string type);

    /* Initializes all backends, called at startup */
    void init();
}
#endif /* end of include guard: IMG_HPP_ */
