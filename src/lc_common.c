/*
 * Copyright (C) 2024 Martin Kepplinger-Novaković
 *
 * SPDX-License-Identifier: GPL-3.0
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lc.h"

#define HYPATIA_IMPLEMENTATION
#define HYP_NO_C_MATH
#include "hypatia.h"

/* for old kernel headers */
#ifndef INPUT_PROP_MAX
# define INPUT_PROP_MAX			0x1f
#endif
#ifndef INPUT_PROP_DIRECT
# define INPUT_PROP_DIRECT		0x01
#endif

#ifndef SYN_MAX /* < 3.12 kernel headers */
# define SYN_MAX 0xf
#endif
#ifndef SYN_CNT
# define SYN_CNT (SYN_MAX+1)
#endif

#ifndef ABS_MT_SLOT /* < 2.6.36 kernel headers */
# define ABS_MT_SLOT             0x2f    /* MT slot being modified */
#endif

#ifndef ABS_MT_PRESSURE /* < 2.6.33 kernel headers */
# define ABS_MT_PRESSURE         0x3a    /* Pressure on contact area */
#endif

#ifndef ABS_MT_DISTANCE /* < 2.6.38 kernel headers */
# define ABS_MT_DISTANCE         0x3b    /* Contact hover distance */
#endif

#ifndef ABS_MT_TOOL_X /* < 3.6 kernel headers */
# define ABS_MT_TOOL_X           0x3c    /* Center X tool position */
# define ABS_MT_TOOL_Y           0x3d    /* Center Y tool position */
#endif

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define BIT(nr)                 (1UL << (nr))
#define BIT_MASK(nr)            (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)            ((nr) / BITS_PER_LONG)
#define BITS_PER_BYTE           8
#define BITS_PER_LONG           (sizeof(long) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

#define DEV_INPUT_EVENT "/dev/input"
#define EVENT_DEV_NAME "event"

static int check_fd(struct tsdev *ts)
{
	int version;
	long evbit[BITS_TO_LONGS(EV_CNT)];
	long absbit[BITS_TO_LONGS(ABS_CNT)];
	long keybit[BITS_TO_LONGS(KEY_CNT)];
	long synbit[BITS_TO_LONGS(SYN_CNT)];
	int mt = 0;
	struct input_absinfo abs_info;

	if (ioctl(ts->fd, EVIOCGVERSION, &version) < 0) {
		fprintf(stderr,
			"Selected device is not a Linux input event device\n");
		return -1;
	}

	/* support version EV_VERSION 0x010000 and 0x010001
	 * this check causes more troubles than it solves here
	 */
	if (version < EV_VERSION)
		fprintf(stderr,
			"Warning: Selected device uses a different version of the event protocol\n");

	if ((ioctl(ts->fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0) ||
		!(evbit[BIT_WORD(EV_ABS)] & BIT_MASK(EV_ABS))) {
		fprintf(stderr,
			"Selected device is not a touchscreen (must support ABS event type)\n");
		return -1;
	}

	if ((ioctl(ts->fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit)) < 0 ||
	    !(absbit[BIT_WORD(ABS_X)] & BIT_MASK(ABS_X)) ||
	    !(absbit[BIT_WORD(ABS_Y)] & BIT_MASK(ABS_Y))) {
		if (!(absbit[BIT_WORD(ABS_MT_POSITION_X)] & BIT_MASK(ABS_MT_POSITION_X)) ||
		    !(absbit[BIT_WORD(ABS_MT_POSITION_Y)] & BIT_MASK(ABS_MT_POSITION_Y))) {
			fprintf(stderr,
				"Selected device is not a touchscreen (must support ABS_X/Y or ABS_MT_POSITION_X/Y events)\n");
			return -1;
		}
	}

	/* Remember whether we have a multitouch device. We need to know for ABS_X,
	 * ABS_Y and ABS_PRESSURE data.
	 */
	if ((absbit[BIT_WORD(ABS_MT_POSITION_X)] & BIT_MASK(ABS_MT_POSITION_X)) &&
	    (absbit[BIT_WORD(ABS_MT_POSITION_Y)] & BIT_MASK(ABS_MT_POSITION_Y)))
		mt = 1;

	if (evbit[BIT_WORD(EV_KEY)] & BIT_MASK(EV_KEY)) {
		if (ioctl(ts->fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) {
			fprintf(stderr, "ioctl EVIOCGBIT error)\n");
			return -1;
		}

		/* for multitouch type B, tracking id is enough for pen down/up. type A
		 * has pen down/up through the list of (empty) SYN_MT_REPORT
		 * only for singletouch we need BTN_TOUCH or BTN_LEFT
		 */
		if (!(keybit[BIT_WORD(BTN_TOUCH)] & BIT_MASK(BTN_TOUCH) ||
		      keybit[BIT_WORD(BTN_LEFT)] & BIT_MASK(BTN_LEFT)) && !mt) {
			fprintf(stderr,
				"Selected device is not a touchscreen (missing BTN_TOUCH or BTN_LEFT)\n");
			return -1;
		}
	}

	if (!(evbit[BIT_WORD(EV_SYN)] & BIT_MASK(EV_SYN)))
		fprintf(stderr, "WARNING: EV_SYN not available)\n");

	if ((ioctl(ts->fd, EVIOCGBIT(EV_SYN, sizeof(synbit)), synbit)) == -1)
		fprintf(stderr, "ioctl error\n");

	/* remember whether we have a multitouch type A device */
	if (mt &&
	    !(absbit[BIT_WORD(ABS_MT_SLOT)] & BIT_MASK(ABS_MT_SLOT)) &&
	    !(absbit[BIT_WORD(ABS_MT_TRACKING_ID)] & BIT_MASK(ABS_MT_TRACKING_ID))) {
		printf("We have a multitouch type A device. Currently not supported.\n");
	}

	if (mt) {
		if (ioctl(ts->fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_info) < 0) {
			fprintf(stderr, "EVIOCGABS error\n");
		}
		ts->input_res_x = abs_info.maximum - abs_info.minimum;

		if (ioctl(ts->fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_info) < 0) {
			fprintf(stderr, "EVIOCGABS error\n");
		}
		ts->input_res_y = abs_info.maximum - abs_info.minimum;
	}

	return ts->fd;
}

struct tsdev *ts_open(const char *name, int nonblock)
{
	struct tsdev *ts;
	int flags = O_RDWR;

//#ifdef DEBUG
	printf("libinput_calibrator: trying to open %s\n", name);
//#endif

	if (nonblock) {
	#ifndef WIN32
		flags |= O_NONBLOCK;
	#endif
	}

	ts = malloc(sizeof(struct tsdev));
	if (!ts)
		return NULL;

	memset(ts, 0, sizeof(struct tsdev));

	ts->eventpath = strdup(name);
	if (!ts->eventpath)
		goto free;

/* XXX todo */
#if 0
	if (ts_open_restricted) {
		ts->fd = ts_open_restricted(name, flags, NULL);
		if (ts->fd == -1)
			goto free;

		return ts;
	}
#endif

	ts->fd = open(name, flags);
	/*
	 * Try again in case file is simply not writable
	 * It will do for most drivers
	 */
	if (ts->fd == -1 && errno == EACCES) {
	#ifndef WIN32
		flags = nonblock ? (O_RDONLY | O_NONBLOCK) : O_RDONLY;
	#else
		flags = O_RDONLY;
	#endif
		ts->fd = open(name, flags);
	}
	if (ts->fd == -1)
		goto free;

	if (check_fd(ts) < 0)
		goto free;

	return ts;

free:
	free(ts->eventpath);
	free(ts);

	return NULL;
}
static int is_event_device(const struct dirent *dir)
{
	return strncmp(EVENT_DEV_NAME, dir->d_name, 5) == 0;
}

static char *scan_devices(void)
{
	struct dirent **namelist;
	int i, ndev;
	char *filename = NULL;
	long propbit[BITS_TO_LONGS(INPUT_PROP_MAX)] = {0};

#ifdef DEBUG
	printf("scanning for devices in %s\n", DEV_INPUT_EVENT);
#endif

	ndev = scandir(DEV_INPUT_EVENT, &namelist, is_event_device, alphasort);
	if (ndev <= 0)
		return NULL;

	for (i = 0; i < ndev; i++) {
		char fname[512];
		int fd = -1;

		snprintf(fname, sizeof(fname),
			 "%s/%s", DEV_INPUT_EVENT, namelist[i]->d_name);
		fd = open(fname, O_RDONLY);
		if (fd < 0)
			continue;

		if ((ioctl(fd, EVIOCGPROP(sizeof(propbit)), propbit) < 0) ||
			!(propbit[BIT_WORD(INPUT_PROP_DIRECT)] &
				  BIT_MASK(INPUT_PROP_DIRECT))) {
			close(fd);
			continue;
		} else {
			close(fd);
			filename = malloc(strlen(DEV_INPUT_EVENT) +
					  strlen(EVENT_DEV_NAME) +
					  12);
			if (!filename)
				break;

			sprintf(filename, "%s/%s%d",
				DEV_INPUT_EVENT, EVENT_DEV_NAME,
				i);
			break;
		}
	}

	for (i = 0; i < ndev; ++i)
		free(namelist[i]);

	free(namelist);

	return filename;
}

static const char * const ts_name_default[] = {
//		"/dev/input/ts",
		"/dev/input/touchscreen",
		NULL
};

struct tsdev *ts_setup(const char *dev_name, int nonblock)
{
	const char * const *defname;
	struct tsdev *ts = NULL;
#if defined (__linux__)
	char *fname = NULL;
#endif /* __linux__ */

	dev_name = dev_name ? dev_name : getenv("TS_DEVICE");

	if (dev_name != NULL) {
		ts = ts_open(dev_name, nonblock);
	} else {
		defname = &ts_name_default[0];
		while (*defname != NULL) {
			ts = ts_open(*defname, nonblock);
			if (ts != NULL)
				break;

			++defname;
		}
	}

#if defined (__linux__)
	if (!ts) {
		fname = scan_devices();
		if (!fname)
			return NULL;

		ts = ts_open(fname, nonblock);
		free(fname);
	}
#endif /* __linux__ */

	return ts;
}

static int ts_input_read(struct tsdev *ts, struct ts_calib_sample *samp, int nr)
{
	struct input_event ev;
	int ret = 0;
	int total = 0;

	while (total < nr) {
		ret = read(ts->fd, &ev, sizeof(struct input_event));
		if (ret < (int)sizeof(struct input_event)) {
			total = -1;
			break;
		}

		switch (ev.type) {
		case EV_SYN:
			if (ev.code == SYN_REPORT) {
				/* Fill out a new complete event */
				samp->tv.tv_sec = ev.input_event_sec;
				samp->tv.tv_usec = ev.input_event_usec;
				samp++;
				total++;
			} else if (ev.code == SYN_DROPPED) {
				fprintf(stderr,
					"libinput_calibrator: SYN_DROPPED\n");
			}
			break;
		case EV_ABS:
			switch (ev.code) {
			case ABS_X:
				samp->x = ev.value;
				break;
			case ABS_Y:
				samp->y = ev.value;
				break;
			case ABS_MT_POSITION_X:
				samp->x = ev.value;
				break;
			case ABS_MT_POSITION_Y:
				samp->y = ev.value;
				break;
			case ABS_PRESSURE:
				samp->pressure = ev.value;
				break;
			case ABS_MT_PRESSURE:
				samp->pressure = ev.value;
				break;
			case ABS_MT_SLOT:
				if (samp->slot && samp->slot != ev.value)
					printf("WARN: switching slot from %d to %d\n",
						samp->slot, ev.value);
				samp->slot = ev.value;
				break;
			case ABS_MT_TOUCH_MAJOR:
				samp->touch_major = ev.value;
				break;
			case ABS_MT_TOUCH_MINOR:
				samp->touch_minor = ev.value;
				break;
			case ABS_MT_WIDTH_MAJOR:
				samp->width_major = ev.value;
				break;
			case ABS_MT_WIDTH_MINOR:
				samp->width_minor = ev.value;
				break;
			case ABS_MT_ORIENTATION:
				samp->orientation = ev.value;
				break;
			case ABS_MT_DISTANCE:
				samp->distance = ev.value;
				break;
			case ABS_MT_TOOL_TYPE:
				samp->tool_type = ev.value;
				break;
			case ABS_MT_BLOB_ID:
				samp->blob_id = ev.value;
				break;
			case ABS_MT_TOOL_X:
				samp->tool_x = ev.value;
				break;
			case ABS_MT_TOOL_Y:
				samp->tool_y = ev.value;
				break;
			case ABS_MT_TRACKING_ID:
				samp->tracking_id = ev.value;
				printf("got new tid: %d. get rid of zeroes...\n", ev.value);
				break;
			}
			break;
		}
	}
	return total;
}

int ts_read_raw(struct tsdev *ts, struct ts_calib_sample *samp, int nr)
{
//#ifdef DEBUG
	int i;
//#endif
	int result = ts_input_read(ts, samp, nr);

//#ifdef DEBUG
	for (i = 0; i < result; i++) {
		fprintf(stderr,
			"TS_READ_RAW: x = %d, y = %d, pressure = %d, tid = %d, slot = %d\n",
			samp->x, samp->y, samp->pressure, samp->tracking_id, samp->slot);

		samp++;
	}
//#endif
	return result;
}

int perform_calibration(calibration *cal)
{
	struct matrix3 coeff_tmp, tmi, tm, ts, coeff;
	const float xl = cal->xfb[0];
	const float xr = cal->xfb[1];
	const float yu = cal->yfb[0];
	const float yl = cal->yfb[3];

	/* skip LR */
	tm.c00 = cal->x[UL];	tm.c10 = cal->x[UR];	tm.c20 = cal->x[LL];
	tm.c01 = cal->y[UL];	tm.c11 = cal->y[UR];	tm.c21 = cal->y[LL];
	tm.c02 = 1;		tm.c12 = 1;		tm.c22 = 1;

	ts.c00 = xl;		ts.c10 = xr;		ts.c20 = xl;
	ts.c01 = yu;		ts.c11 = yu;		ts.c21 = yl;
	ts.c02 = 1;		ts.c12 = 1;		ts.c22 = 1;

	matrix3_set(&tmi, &tm);
	matrix3_invert(&tmi);

	matrix3_set(&coeff, &ts);
	printf("verify c00 changes before multiply: %f\n", coeff.c00);
	matrix3_multiply(&coeff, &tmi);
	printf("verify c00 changes after multiply: %f\n", coeff.c00);

	/* skip UL*/
}
