/*
 * src/video/circle/SDL_circlevideo.h
 *
 * SDL3 video backend for Circle bare-metal (RPi 3B AArch32).
 */
#ifndef SDL_circlevideo_h_
#define SDL_circlevideo_h_

#include "SDL_internal.h"
#include "video/SDL_sysvideo.h"

/* Driver name */
#define CIRCLEVID_DRIVER_NAME "circle"

/* Bootstrap declaration */
extern VideoBootStrap CIRCLE_bootstrap;

/* C++ glue: framebuffer operations exported as extern "C" */
extern int  circle_fb_init(unsigned w, unsigned h, unsigned depth);
extern void circle_fb_quit(void);
extern unsigned circle_fb_get_width(void);
extern unsigned circle_fb_get_height(void);
extern unsigned circle_fb_get_pitch(void);
extern unsigned circle_fb_get_depth(void);
extern void    *circle_fb_get_buffer(void);
extern void     circle_fb_update(void);

#endif /* SDL_circlevideo_h_ */
