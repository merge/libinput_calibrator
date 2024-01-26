#ifndef _TSCALIBRATE_H
#define _TSCALIBRATE_H
/*
 * Copyright (C) 2024 Martin Kepplinger-Novaković
 *
 * SPDX-License-Identifier: GPL-3.0
 */

#ifdef __FreeBSD__
# include <dev/evdev/input.h>
#endif

#if defined (__linux__)
# include <linux/input.h>
#endif

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define BLUE    "\033[34m"
#define YELLOW  "\033[33m"

struct ts_button {
	int x, y, w, h;
	char *text;
	int flags;
#define BUTTON_ACTIVE 0x00000001
};

void button_draw(struct ts_button *button);
int button_handle(struct ts_button *button, int x, int y, unsigned int pressure);

struct ts_calib_sample {
	/* ABS_MT_* event codes. linux' include/uapi/linux/input-event-codes.h
	 * has the definitions.
	 */
	int		x;
	int		y;
	unsigned int	pressure;
	int		slot;
	int		tracking_id;

	int		tool_type;
	int		tool_x;
	int		tool_y;
	unsigned int	touch_major;
	unsigned int	width_major;
	unsigned int	touch_minor;
	unsigned int	width_minor;
	int		orientation;
	int		distance;
	int		blob_id;
	short		btn_touch;
	struct timeval	tv;

	int		space[10];
};

struct tsdev {
	int fd;
	char *eventpath;
	unsigned int res_x;
	unsigned int res_y;
	int input_res_x;
	int input_res_y;
	int rotation;

	struct ts_calib_sample samp_last;
};

/* TODO 4x only */
typedef struct {
	int x[5], xfb[5];
	int y[5], yfb[5];
	int a[7];
} calibration;

void getxy(struct tsdev *ts, int *x, int *y);
int perform_calibration(calibration *cal);
struct tsdev *ts_setup(const char *dev_name, int nonblock);
int ts_read_raw(struct tsdev *ts, struct ts_calib_sample *samp, int nr);

#endif /* _TSCALIBRATE_H */
