/*
 * Copyright 2024 Martin Kepplinger-Novaković
 *
 * SPDX-License-Identifier: GPL-3.0
 */

#ifndef _FBUTILS_H
#define _FBUTILS_H

#include <stdint.h>

/* This constant, being ORed with the color index tells the library
 * to draw in exclusive-or mode (that is, drawing the same second time
 * in the same place will remove the element leaving the background intact).
 */
#define XORMODE	0x80000000

extern uint32_t xres, yres;
extern uint32_t xres_orig, yres_orig;
extern int8_t rotation;
extern int8_t alternative_cross;

int open_framebuffer(void);
void close_framebuffer(void);
void setcolor(unsigned colidx, unsigned value);
void put_cross(int x, int y, unsigned colidx);
void put_string(int x, int y, char *s, unsigned colidx);
void put_string_center(int x, int y, char *s, unsigned colidx);
void pixel(int x, int y, unsigned colidx);
void line(int x1, int y1, int x2, int y2, unsigned colidx);
void rect(int x1, int y1, int x2, int y2, unsigned colidx);
void fillrect(int x1, int y1, int x2, int y2, unsigned colidx);

#endif /* _FBUTILS_H */
