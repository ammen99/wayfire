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
    using Loader = std::function<GLuint(const char*, ulong&, ulong&)>;
    using Writer = std::function<void(const char *name, uint8_t *pixels, ulong, ulong)>;
    namespace {
        std::unordered_map<std::string, Loader> loaders;
        std::unordered_map<std::string, Writer> writers;
    }

    /* All backend functions are taken from the internet.
     * If you want to be credited, contact me */

    GLuint texture_from_png(const char *file_name, ulong& width, ulong& height) {
        png_byte header[8];

        FILE *fp = fopen(file_name, "rb");
        if (!fp) {
            return TEXTURE_LOAD_ERROR;
        }

        fread(header, 1, 8, fp);

        int is_png = !png_sig_cmp(header, 0, 8);
        if (!is_png) {
            fclose(fp);
            return TEXTURE_LOAD_ERROR;
        }

        png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
                NULL, NULL);
        if (!png_ptr) {
            fclose(fp);
            return (TEXTURE_LOAD_ERROR);
        }

        //create png info struct
        png_infop info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr) {
            png_destroy_read_struct(&png_ptr, (png_infopp) NULL, (png_infopp) NULL);
            fclose(fp);
            return (TEXTURE_LOAD_ERROR);
        }

        png_infop end_info = png_create_info_struct(png_ptr);
        if (!end_info) {
            png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp) NULL);
            fclose(fp);
            return (TEXTURE_LOAD_ERROR);
        }

        if (setjmp(png_jmpbuf(png_ptr))) {
            png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
            fclose(fp);
            return (TEXTURE_LOAD_ERROR);
        }

        png_init_io(png_ptr, fp);
        png_set_sig_bytes(png_ptr, 8);
        png_read_info(png_ptr, info_ptr);

        int bit_depth, color_type;
        png_uint_32 twidth, theight;
        png_get_IHDR(png_ptr, info_ptr, &twidth, &theight, &bit_depth, &color_type,
                NULL, NULL, NULL);

        width = twidth;
        height = theight;

        png_read_update_info(png_ptr, info_ptr);

        int rowbytes = png_get_rowbytes(png_ptr, info_ptr);
        png_byte *image_data = new png_byte[rowbytes * height];
        if (!image_data) {
            png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
            fclose(fp);
            return TEXTURE_LOAD_ERROR;
        }

        png_bytep *row_pointers = new png_bytep[height];
        if (!row_pointers) {
            png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
            delete[] image_data;
            fclose(fp);
            return TEXTURE_LOAD_ERROR;
        }

        for (size_t i = 0; i < height; ++i)
            row_pointers[i] = image_data + i * rowbytes;

        png_read_image(png_ptr, row_pointers);

        GLuint texture;
        GL_CALL(glGenTextures(1, &texture));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture));
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*) image_data));

        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        delete[] image_data;
        delete[] row_pointers;
        fclose(fp);

        return texture;
    }

    void texture_to_png(const char *name, uint8_t *pixels, int w, int h) {
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

    GLuint texture_from_jpeg(const char* FileName, unsigned long &x, unsigned long &y) {
        unsigned int texture_id;
        unsigned long data_size;
        int channels;
        unsigned int type;
        unsigned char * rowptr[1];
        unsigned char * jdata;
        struct jpeg_decompress_struct infot;
        struct jpeg_error_mgr err;

        std::FILE* file = fopen(FileName, "rb");
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
        type = GL_RGB;
        if(channels == 4) type = GL_RGBA;
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

    GLuint load_from_file(std::string name, ulong &w, ulong &h) {
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

    void write_to_file(std::string name, uint8_t *pixels, int w, int h, std::string type) {
        auto it = writers.find(type);

        if (it == writers.end()) {
            errio << "IMG: unsupported writer backend" << std::endl;
        } else {
            it->second(name.c_str(), pixels, w, h);
        }
    }

    void init() {
        loaders["png"] = Loader(texture_from_png);
        loaders["jpg"] = Loader(texture_from_jpeg);
        writers["png"] = Writer(texture_to_png);
    }
}
