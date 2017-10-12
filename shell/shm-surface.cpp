#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include <cairo.h>

#include "config.h"
#include "window.hpp"

struct rectangle
{
	int x;
	int y;
	int width;
	int height;
};

struct shm_pool
{
	wl_shm_pool *pool;
	size_t size;
	size_t used;
	void *data;
};

struct shm_surface_data
{
	wl_buffer *buffer;
	shm_pool *pool;
};

struct shm_window : wayfire_window
{
	rectangle rect;
};

const cairo_user_data_key_t shm_surface_data_key = {0};

 wl_buffer* get_buffer_from_cairo_surface(cairo_surface_t *surface)
{
	auto data = static_cast<shm_surface_data*>
        (cairo_surface_get_user_data(surface, &shm_surface_data_key));

	return data->buffer;
}

void shm_pool_destroy(shm_pool *pool);

void shm_surface_data_destroy(void *p)
{
	auto data = static_cast<shm_surface_data*> (p);
	wl_buffer_destroy(data->buffer);
	if (data->pool)
		shm_pool_destroy(data->pool);

    delete data;
}

int set_cloexec_or_close(int fd)
{
	long flags;

	if (fd == -1)
		return -1;

	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		goto err;

	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
		goto err;

	return fd;

err:
	close(fd);
	return -1;
}

int create_tmpfile_cloexec(char *tmpname)
{
	int fd;
	fd = mkstemp(tmpname);
	if (fd >= 0)
    {
		fd = set_cloexec_or_close(fd);
		unlink(tmpname);
	}

	return fd;
}

int os_create_anonymous_file(off_t size)
{
	static const char templ[] = "/wayfire-shared-XXXXXX";
	const char *path;
	char *name;
	int fd;
	int ret;

	path = getenv("XDG_RUNTIME_DIR");
	if (!path)
    {
		errno = ENOENT;
		return -1;
	}

    name = new char[strlen(path) + sizeof(templ)];

	strcpy(name, path);
	strcat(name, templ);

	fd = create_tmpfile_cloexec(name);

    delete[] name;

	if (fd < 0)
		return -1;

	ret = posix_fallocate(fd, 0, size);
	if (ret != 0)
    {
		close(fd);
		errno = ret;
		return -1;
	}
	return fd;
}


wl_shm_pool* make_shm_pool(wl_shm *shm, int size, void **data)
{
    wl_shm_pool *pool;
    int fd;

    fd = os_create_anonymous_file(size);
    if (fd < 0) {
        std::cerr << "creating a buffer file for " << size << " failed" << std::endl;
        return NULL;
    }

    *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (*data == MAP_FAILED) {
        std::cerr << "mmap failed" << std::endl;
        close(fd);
        return NULL;
    }

    pool = wl_shm_create_pool(shm, fd, size);

    close(fd);

    return pool;
}

shm_pool * shm_pool_create( wl_shm *shm, size_t size)
{
    auto pool = new shm_pool;

    if (!pool)
        return NULL;

    pool->pool = make_shm_pool(shm, size, &pool->data);
    if (!pool->pool) {
        delete pool;
        return NULL;
    }

    pool->size = size;
    pool->used = 0;

    return pool;
}

    static void *
shm_pool_allocate( shm_pool *pool, size_t size, int *offset)
{
    if (pool->used + size > pool->size)
        return NULL;

    *offset = pool->used;
    pool->used += size;

    return (char *) pool->data + *offset;
}

/* destroy the pool. this does not unmap the memory though */
void shm_pool_destroy( shm_pool *pool)
{
    munmap(pool->data, pool->size);
    wl_shm_pool_destroy(pool->pool);
    delete pool;
}

#define target_fmt CAIRO_FORMAT_ARGB32

inline int data_length_for_shm_surface(rectangle *rect)
{
    return cairo_format_stride_for_width(target_fmt, rect->width) * rect->height;
}

cairo_surface_t * create_shm_surface_from_pool(void *none, rectangle *rectangle, shm_pool *pool)
{
    cairo_surface_t *surface;

    auto data = new shm_surface_data;
    if (data == NULL)
        return NULL;

    int stride = cairo_format_stride_for_width (target_fmt, rectangle->width);
    int length = stride * rectangle->height;
    data->pool = NULL;

    int offset;
    void *map = shm_pool_allocate(pool, length, &offset);

    if (!map) {
        free(data);
        return NULL;
    }

    surface = cairo_image_surface_create_for_data ((unsigned char*) map,
            target_fmt,
            rectangle->width,
            rectangle->height,
            stride);

    cairo_surface_set_user_data(surface, &shm_surface_data_key,
            data, shm_surface_data_destroy);

    data->buffer = wl_shm_pool_create_buffer(pool->pool, offset,
            rectangle->width,
            rectangle->height,
            stride, WL_SHM_FORMAT_ARGB8888);

    return surface;
}

wayfire_window* create_window(uint32_t w, uint32_t h)
{
    shm_surface_data *data;
    shm_pool *pool;

    shm_window *window = new shm_window;

    window->rect.x = 0;
    window->rect.y = 0;
    window->rect.width = w;
    window->rect.height = h;

	window->surface = wl_compositor_create_surface(display.compositor);
    wl_surface_set_user_data(window->surface, window);

	window->shell_surface = wl_shell_get_shell_surface(display.shell, window->surface);
	wl_shell_surface_add_listener(window->shell_surface, &shell_surface_listener, window);
	wl_shell_surface_set_toplevel(window->shell_surface);

    pool = shm_pool_create(display.shm, data_length_for_shm_surface(&window->rect));
    if (!pool)
        return NULL;

    window->cairo_surface =
        create_shm_surface_from_pool(display.shm, &window->rect, pool);

    if (!window->cairo_surface)
    {
        shm_pool_destroy(pool);
        return NULL;
    }

    /* make sure we destroy the pool when the surface is destroyed */
    data = (shm_surface_data*) cairo_surface_get_user_data(window->cairo_surface, &shm_surface_data_key);
    data->pool = pool;

    return window;
}

void set_active_window(wayfire_window *w)
{
}

void backend_delete_window (wayfire_window *w)
{
    auto window = static_cast<shm_window*> (w);
    delete window;
}

void damage_commit_window(wayfire_window *w)
{
    auto window = static_cast<shm_window*> (w);

    wl_surface_attach(window->surface, get_buffer_from_cairo_surface(window->cairo_surface),0,0);
    wl_surface_damage(window->surface, window->rect.x, 
            window->rect.y,
            window->rect.width,
            window->rect.height);
    wl_surface_commit(window->surface);
}

bool setup_backend()
{
    return true;
}

void finish_backend() {}
