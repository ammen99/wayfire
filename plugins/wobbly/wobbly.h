/**************************************************************************
 *
 * Copyright 2014 Scott Moreau <oreaus@gmail.com>
 * All Rights Reserved.
 *
 **************************************************************************/

#include <stdio.h>

#include <GLES2/gl2.h>

#define MINIMAL_FRICTION 0.1
#define MAXIMAL_FRICTION 10.0
#define MINIMAL_SPRING_K 0.1
#define MAXIMAL_SPRING_K 10.0
#define WOBBLY_MASS 15.0

double wobbly_settings_get_friction();
double wobbly_settings_get_spring_k();

struct wobbly_surface
{
   void *ww;
   int x, y, width, height;
   int x_cells, y_cells;
   int grabbed, synced;
   int vertex_count;

   GLfloat *v, *uv;
};

struct wobbly_rect
{
    float tlx, tly;
    float brx, bry;
};

int  wobbly_init(struct wobbly_surface *surface);
void wobbly_fini(struct wobbly_surface *surface);
void wobbly_set_top_anchor(struct wobbly_surface *surface,
    int x, int y, int w, int h);

void wobbly_grab_notify(struct wobbly_surface *surface, int x, int y);
void wobbly_slight_wobble(struct wobbly_surface *surface);
void wobbly_ungrab_notify(struct wobbly_surface *surface);

void wobbly_scale(struct wobbly_surface *surface, double dx, double dy);
void wobbly_resize(struct wobbly_surface *surface, int width, int height);
void wobbly_move_notify(struct wobbly_surface *surface, int x, int y);
void wobbly_prepare_paint(struct wobbly_surface *surface, int msSinceLastPaint);
void wobbly_done_paint(struct wobbly_surface *surface);
void wobbly_add_geometry(struct wobbly_surface *surface);
struct wobbly_rect wobbly_boundingbox(struct wobbly_surface *surface);

void wobbly_force_geometry(struct wobbly_surface *surface,
    int x, int y, int w, int h);
void wobbly_unenforce_geometry(struct wobbly_surface *surface);

void wobbly_translate(struct wobbly_surface *surface, int dx, int dy);
