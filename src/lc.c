/*
 * Copyright (C) 2024 Martin Kepplinger-Novaković <martink@posteo.de>
 *
 * SPDX-License-Identifier: GPL-3.0
 *
 * Graphical touchscreen calibration tool. This creates the values
 * for libinput's udev property, the defaults are
 * ENV{LIBINPUT_CALIBRATION_MATRIX}="1 0 0 0 1 0"
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <unistd.h>

#include "config.h"

#include "fbutils.h"
#include "lc.h"

#define CROSS_BOUND_DIST	50

static int palette[] = {
	0x000000, 0xffe080, 0xffffff, 0xe0c0a0, 0xff0000, 0x00ff00
};
#define NR_COLORS (sizeof(palette) / sizeof(palette[0]))

/* [inactive] border fill text [active] border fill text */
static int button_palette[6] = {
	1, 4, 2,
	1, 5, 0
};

void button_draw(struct ts_button *button)
{
	int s = (button->flags & BUTTON_ACTIVE) ? 3 : 0;

	rect(button->x, button->y, button->x + button->w,
	     button->y + button->h, button_palette[s]);
	fillrect(button->x + 1, button->y + 1,
		 button->x + button->w - 2,
		 button->y + button->h - 2, button_palette[s + 1]);
	put_string_center(button->x + button->w / 2,
			  button->y + button->h / 2,
			  button->text, button_palette[s + 2]);
}

int button_handle(struct ts_button *button, int x, int y, unsigned int p)
{
	int inside = (x >= button->x) && (y >= button->y) &&
		     (x < button->x + button->w) &&
		     (y < button->y + button->h);

	if (p > 0) {
		if (inside) {
			if (!(button->flags & BUTTON_ACTIVE)) {
				button->flags |= BUTTON_ACTIVE;
				button_draw(button);
			}
		} else if (button->flags & BUTTON_ACTIVE) {
			button->flags &= ~BUTTON_ACTIVE;
			button_draw(button);
		}
	} else if (button->flags & BUTTON_ACTIVE) {
		button->flags &= ~BUTTON_ACTIVE;
		button_draw(button);
		return 1;
	}

	return 0;
}

static int sort_by_x(const void *a, const void *b)
{
	return (((struct ts_calib_sample *)a)->x - ((struct ts_calib_sample *)b)->x);
}

static int sort_by_y(const void *a, const void *b)
{
	return (((struct ts_calib_sample *)a)->y - ((struct ts_calib_sample *)b)->y);
}

/* Waits for the screen to be touched, averages x and y sample
 * coordinates until the end of contact
 */
void getxy(struct tsdev *ts, int *x, int *y)
{
#define MAX_SAMPLES 128
	struct ts_calib_sample samp[MAX_SAMPLES];
	int index, middle;
	int ret;

	/* Now collect up to MAX_SAMPLES touches into the samp array. */
	index = 0;

	while (1) {
		if (index == MAX_SAMPLES - 1)
			break;

		ret = ts_read_raw(ts, &samp[index], 1);
		if (ret < 0) {
			perror("ts_read_raw");
			close_framebuffer();
			exit(1);
		}

		if (samp[index].tracking_id == -1)
			break;

		memcpy(&samp[index + 1], &samp[index], sizeof(struct ts_calib_sample));
		index++;
	}

	printf("Took %d samples...\n", index);

	/*
	 * At this point, we have samples in indices zero to (index-1)
	 * which means that we have (index) number of samples.  We want
	 * to calculate the median of the samples so that wild outliers
	 * don't skew the result.  First off, let's assume that arrays
	 * are one-based instead of zero-based.  If this were the case
	 * and index was odd, we would need sample number ((index+1)/2)
	 * of a sorted array; if index was even, we would need the
	 * average of sample number (index/2) and sample number
	 * ((index/2)+1).  To turn this into something useful for the
	 * real world, we just need to subtract one off of the sample
	 * numbers.  So for when index is odd, we need sample number
	 * (((index+1)/2)-1).  Due to integer division truncation, we
	 * can simplify this to just (index/2).  When index is even, we
	 * need the average of sample number ((index/2)-1) and sample
	 * number (index/2).  Calculate (index/2) now and we'll handle
	 * the even odd stuff after we sort.
	 */
	middle = index/2;
	if (x) {
		qsort(samp, index, sizeof(struct ts_calib_sample), sort_by_x);
		if (index & 1)
			*x = samp[middle].x;
		else
			*x = (samp[middle-1].x + samp[middle].x) / 2;
	}
	if (y) {
		qsort(samp, index, sizeof(struct ts_calib_sample), sort_by_y);
		if (index & 1)
			*y = samp[middle].y;
		else
			*y = (samp[middle-1].y + samp[middle].y) / 2;
	}
}
static void sig(int sig)
{
	close_framebuffer();
	fflush(stderr);
	printf("signal %d caught\n", sig);
	fflush(stdout);
	exit(1);
}

static unsigned int getticks()
{
	static struct timeval ticks = {0};
	static unsigned int val = 0;

	gettimeofday(&ticks, NULL);
	val = ticks.tv_sec * 1000;
	val += ticks.tv_usec / 1000;

	return val;
}

static void get_sample(struct tsdev *ts, calibration *cal,
		       int index, int x, int y, char *name, short redo)
{
	static int last_x = -1, last_y;

	if (redo) {
		last_x = -1;
		last_y = 0;
	}

	if (last_x != -1) {
#define NR_STEPS 10
		int dx = ((x - last_x) << 16) / NR_STEPS;
		int dy = ((y - last_y) << 16) / NR_STEPS;
		int i;

		last_x <<= 16;
		last_y <<= 16;
		for (i = 0; i < NR_STEPS; i++) {
			put_cross(last_x >> 16, last_y >> 16, 2 | XORMODE);
			usleep(1000);
			put_cross(last_x >> 16, last_y >> 16, 2 | XORMODE);
			last_x += dx;
			last_y += dy;
		}
	}

	put_cross(x, y, 2 | XORMODE);
	getxy(ts, &cal->x[index], &cal->y[index]);
	put_cross(x, y, 2 | XORMODE);

	last_x = cal->xfb[index] = x;
	last_y = cal->yfb[index] = y;

	printf("%s : X = %4d Y = %4d\n", name, cal->x[index], cal->y[index]);
}

static void clearbuf(struct tsdev *ts)
{
	int fd = ts->fd;
	fd_set fdset;
	struct timeval tv;
	int nfds;
	struct ts_calib_sample sample;

	while (1) {
		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);

		tv.tv_sec = 0;
		tv.tv_usec = 0;

		nfds = select(fd + 1, &fdset, NULL, NULL, &tv);
		if (nfds == 0)
			break;

		if (ts_read_raw(ts, &sample, 1) < 0) {
			perror("ts_read_raw");
			exit(1);
		}
	}
}

int main(int argc, char **argv)
{
	struct tsdev *ts;
	calibration cal = {
		.x = { 0 },
		.y = { 0 },
	};
	char cal_buffer[256];
	char *calfile = NULL;
	unsigned int i, len;
	unsigned int tick = 0;
	/* TODO find sane default: */
	unsigned int min_interval = 0;

	signal(SIGSEGV, sig);
	signal(SIGINT, sig);
	signal(SIGTERM, sig);

	while (1) {
		const struct option long_options[] = {
			{ "help",         no_argument,       NULL, 'h' },
			{ "rotate",       required_argument, NULL, 'r' },
			{ "version",      no_argument,       NULL, 'v' },
			{ "min_interval", required_argument, NULL, 't' },
			{ "timeout",      required_argument, NULL, 's' },
		};

		int option_index = 0;
		int c = getopt_long(argc, argv, "hvr:t:s:", long_options, &option_index);

		errno = 0;
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			/* TODO help text */
			return 0;

		case 'v':
			/* TODO version */
			printf("libinput_calibrator alpha-stage development\n");
			return 0;

		case 'r':
			/* extern in fbutils.h */
			rotation = atoi(optarg);
			if (rotation < 0 || rotation > 3) {
				return 0;
			}
			break;

		case 's':
			/* TODO timeout */
			break;

		case 't':
			min_interval = atoi(optarg);
			if (min_interval > 10000) {
				fprintf(stderr, "Minimum interval too long\n");
				return 0;
			}
			break;

		default:
			return 0;
		}

		if (errno) {
			char str[9];
			sprintf(str, "option ?");
			str[7] = c & 0xff;
			perror(str);
		}
	}

	ts = ts_setup(NULL, 0);
	if (!ts) {
		perror("ts_setup");
		exit(1);
	}

	if (open_framebuffer()) {
		close_framebuffer();
		close(ts->fd);
		exit(1);
	}

	/* TODO do away with the global vars */
	ts->res_x = xres;
	ts->res_y = yres;

	for (i = 0; i < NR_COLORS; i++)
		setcolor(i, palette[i]);

	put_string_center(xres / 2, yres / 4,
			  "Touchscreen calibration utility", 1);
	put_string_center(xres / 2, yres / 4 + 20,
			  "Touch crosshair to calibrate", 2);

	printf("framebuffer: xres = %d, yres = %d\n", ts->res_x, ts->res_y);
	printf("input:       xres = %d, yres = %d\n", ts->input_res_x, ts->input_res_y);

	if ((abs(ts->input_res_x - ts->res_x) <= 1) &&
	    (abs(ts->input_res_y - ts->res_y) <= 1)) {
		printf("Your touchscreen might not need any calibration!\n");
	}

	/* Clear the buffer */
	clearbuf(ts);

	/* ignore rotation for calibration. only save it.*/
	int rotation_temp = rotation;
	int xres_temp = xres;
	int yres_temp = yres;
	rotation = 0;
	xres = xres_orig;
	yres = yres_orig;

	short redo = 0;

redocalibration:
	tick = getticks();
	get_sample(ts, &cal, 0, CROSS_BOUND_DIST,        CROSS_BOUND_DIST,        "Top left", redo);
	redo = 0;
	if (getticks() - tick < min_interval) {
		redo = 1;
//	#ifdef DEBUG
		printf("ts_calibrate: time before touch press < %dms. restarting.\n",
			min_interval);
//	#endif
		goto redocalibration;
	}
	clearbuf(ts);

	tick = getticks();
	get_sample(ts, &cal, 1, xres - CROSS_BOUND_DIST, CROSS_BOUND_DIST,        "Top right", redo);
	if (getticks() - tick < min_interval) {
		redo = 1;
//	#ifdef DEBUG
		printf("ts_calibrate: time before touch press < %dms. restarting.\n",
			min_interval);
//	#endif
		goto redocalibration;
	}
	clearbuf(ts);

	tick = getticks();
	get_sample(ts, &cal, 2, xres - CROSS_BOUND_DIST, yres - CROSS_BOUND_DIST, "Bot right", redo);
	if (getticks() - tick < min_interval) {
		redo = 1;
//	#ifdef DEBUG
		printf("ts_calibrate: time before touch press < %dms. restarting.\n",
			min_interval);
//	#endif
		goto redocalibration;
	}
	clearbuf(ts);

	tick = getticks();
	get_sample(ts, &cal, 3, CROSS_BOUND_DIST,        yres - CROSS_BOUND_DIST, "Bot left", redo);
	if (getticks() - tick < min_interval) {
		redo = 1;
//	#ifdef DEBUG
		printf("ts_calibrate: time before touch press < %dms. restarting.\n",
			min_interval);
//	#endif
		goto redocalibration;
	}
	clearbuf(ts);

	tick = getticks();
	get_sample(ts, &cal, 4, xres_orig / 2,  yres_orig / 2,  "Center", redo);
	if (getticks() - tick < min_interval) {
		redo = 1;
//	#ifdef DEBUG
		printf("ts_calibrate: time before touch press < %dms. restarting.\n",
			min_interval);
//	#endif
		goto redocalibration;
	}

	rotation = rotation_temp;
	xres = xres_temp;
	yres = yres_temp;

	if (perform_calibration (&cal)) {
		printf("Calibration constants: ");
		for (i = 0; i < 7; i++)
			printf("%d ", cal.a[i]);
		printf("\n");
		i = 0;
	} else {
		printf("Calibration failed.\n");
		i = -1;
	}

	fillrect(0, 0, xres - 1, yres - 1, 0);
	close_framebuffer();
	close(ts->fd);
	return i;
}
