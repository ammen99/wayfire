#include "img.hpp"
#include "opengl.hpp"
#include <png.h>
#include <stdint.h>
#include <jpeglib.h>
#include <jerror.h>
#include <cstdio>
#include <iostream>
#include <unordered_map>

#define TEXTURE_LOAD_ERROR 0

namespace image_io {
    using Loader = std::function<GLuint(const char *, ulong&, ulong&)>;
    using Writer = std::function<void(const char *name, uint8_t *pixels, ulong, ulong)>;
    namespace {
        std::unordered_map<std::string, Loader> loaders;
        std::unordered_map<std::string, Writer> writers;
    }

    /* All backend functions are taken from the internet.
     * If you want to be credited, contact me */

    GLuint texture_from_png(const char *filename, ulong& w, ulong& h)
    {
        FILE *fp = fopen(filename, "rb");
        int width, height;
        png_byte color_type;
        png_byte bit_depth;
        png_bytep *row_pointers;


        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if(!png)
            return TEXTURE_LOAD_ERROR;;

        png_infop infos = png_create_info_struct(png);
        if(!infos)
            return TEXTURE_LOAD_ERROR;

        if(setjmp(png_jmpbuf(png)))
            return TEXTURE_LOAD_ERROR;

        png_init_io(png, fp);
        png_read_info(png, infos);

        width      = png_get_image_width(png, infos);
        height     = png_get_image_height(png, infos);
        color_type = png_get_color_type(png, infos);
        bit_depth  = png_get_bit_depth(png, infos);

        w = width;
        h = height;

        // Read any color_type into 8bit depth, RGBA format.
        // See http://www.libpng.org/pub/png/libpng-manual.txt

        if(bit_depth == 16)
            png_set_strip_16(png);

        if(color_type == PNG_COLOR_TYPE_PALETTE)
            png_set_palette_to_rgb(png);

        // PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
        if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
            png_set_expand_gray_1_2_4_to_8(png);

        if(png_get_valid(png, infos, PNG_INFO_tRNS))
            png_set_tRNS_to_alpha(png);

        // These color_type don't have an alpha channel then fill it with 0xff.
        if(color_type == PNG_COLOR_TYPE_RGB ||
                color_type == PNG_COLOR_TYPE_GRAY ||
                color_type == PNG_COLOR_TYPE_PALETTE)
            png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

        if(color_type == PNG_COLOR_TYPE_GRAY ||
                color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(png);

        png_read_update_info(png, infos);

        row_pointers = new png_bytep[height];
        png_byte *data = new png_byte[height * png_get_rowbytes(png, infos)];

        for(int i = 0; i < height; i++)
        {
            row_pointers[i] = data + i * png_get_rowbytes(png, infos);
        }

        png_read_image(png, row_pointers);

        GLuint texture;
        GL_CALL(glGenTextures(1, &texture));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture));

        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid *) data));

        png_destroy_read_struct(&png, &infos, NULL);
        delete[] row_pointers;

        fclose(fp);

        return texture;
    }

    void texture_to_png(const char *name, uint8_t *pixels, int w, int h)
    {
        png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (!png)
            return;

        png_infop infot = png_create_info_struct(png);
        if (!infot) {
            png_destroy_write_struct(&png, &infot);
            return;
        }

        FILE *fp = fopen(name, "wb");
        if (!fp) {
            png_destroy_write_struct(&png, &infot);
            return;
        }

        png_init_io(png, fp);
        png_set_IHDR(png, infot, w, h, 8 /* depth */, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        png_colorp palette = (png_colorp)png_malloc(png, PNG_MAX_PALETTE_LENGTH * sizeof(png_color));
        if (!palette) {
            fclose(fp);
            png_destroy_write_struct(&png, &infot);
            return;
        }
        png_set_PLTE(png, infot, palette, PNG_MAX_PALETTE_LENGTH);
        png_write_info(png, infot);
        png_set_packing(png);

        png_bytepp rows = (png_bytepp)png_malloc(png, h * sizeof(png_bytep));
        for (int i = 0; i < h; ++i)
            rows[i] = (png_bytep)(pixels + (h - i) * w * 4);

        png_write_image(png, rows);
        png_write_end(png, infot);
        png_free(png, palette);
        png_destroy_write_struct(&png, &infot);

        fclose(fp);
        delete[] rows;
    }

    GLuint texture_from_jpeg(const char *FileName, unsigned long& x, unsigned long& y)
    {
        unsigned int texture_id;
        unsigned long data_size;
        int channels;
        unsigned char *rowptr[1];
        unsigned char *jdata;
        struct jpeg_decompress_struct infot;
        struct jpeg_error_mgr err;

        std::FILE *file = fopen(FileName, "rb");
        infot.err = jpeg_std_error(& err);
        jpeg_create_decompress(&infot);

        if(!file) {
            errio << "Error reading JPEG file" << FileName << std::endl;
            return 0;
        }

        jpeg_stdio_src(&infot, file);
        jpeg_read_header(&infot, TRUE);
        jpeg_start_decompress(&infot);

        x = infot.output_width;
        y = infot.output_height;
        channels = infot.num_components;
        data_size = x * y * 3;

        jdata = new unsigned char[data_size];
        while (infot.output_scanline < infot.output_height) {
            rowptr[0] = (unsigned char *)jdata +  3* infot.output_width * infot.output_scanline;
            jpeg_read_scanlines(&infot, rowptr, 1);
        }

        jpeg_finish_decompress(&infot);

        GL_CALL(glGenTextures(1,&texture_id));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture_id));
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, x, y, 0, GL_RGB, GL_UNSIGNED_BYTE, jdata));

        fclose(file);
        delete[] jdata;
        return texture_id;
    }

    GLuint load_from_file(std::string name, ulong& w, ulong& h)
    {
        int len = name.length();
        if (len < 4 || name[len - 4] != '.') {
            errio << "load_from_file() called with file without extension or with invalid extension!\n";
            return -1;
        }

        auto ext = name.substr(len - 3, 3);
        for (int i = 0; i < 3; i++)
            ext[i] = std::tolower(ext[i]);

        auto it = loaders.find(ext);
        if (it == loaders.end()) {
            errio << "load_from_file() called with unsupported extension " << ext << std::endl;
            return -1;
        } else {
            return it->second(name.c_str(), w, h);
        }
    }

    void write_to_file(std::string name, uint8_t *pixels, int w, int h, std::string type)
    {
        auto it = writers.find(type);

        if (it == writers.end()) {
            errio << "IMG: unsupported writer backend" << std::endl;
        } else {
            it->second(name.c_str(), pixels, w, h);
        }
    }

    void init()
    {
        debug << "ImageIO init" << std::endl;
        loaders["png"] = Loader(texture_from_png);
        loaders["jpg"] = Loader(texture_from_jpeg);
        writers["png"] = Writer(texture_to_png);
    }
}
