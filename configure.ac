#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT([libinput_calibrator], [0.1+], [martink@posteo.de], [libinput_calibrator], [https://github.com/merge/libinput_calibrator])
AM_INIT_AUTOMAKE([dist-xz subdir-objects])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)]) # use silent rules where available - automake 1.11
AC_CONFIG_SRCDIR([src/lc.c])
AC_CONFIG_HEADERS(config.h)

PACKAGE_DESCRIPTION="libingput touchscreen matrix generator for udev"
AC_SUBST(PACKAGE_DESCRIPTION)

if test "x${enable_dependency_tracking}" = "x"; then
	enable_dependency_tracking=no
fi

# Check for host os
AC_CANONICAL_HOST
build_linux=no
build_unknown=no
case "${host_os}" in
	linux*)
		build_linux=yes
		;;
	*)
		build_unknown=yes
		;;
esac
AM_CONDITIONAL([LINUX], [test "$build_linux" = "yes"])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
LT_INIT

# Checks for header files.
AC_FUNC_ALLOCA
AC_CHECK_HEADERS([fcntl.h limits.h stdlib.h string.h sys/ioctl.h sys/time.h unistd.h stdint.h sys/types.h errno.h dirent.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MMAP
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_STAT
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([gettimeofday memmove memset munmap select strcasecmp strchr strdup strtoul strtol strsep])
AM_CONDITIONAL(HAVE_STRSEP, test x$HAVE_STRSEP = xyes)

AC_MSG_CHECKING([whether to enable debugging])
AC_ARG_ENABLE(debug,
	AS_HELP_STRING(--enable-debug
		Enable debug messages from filters (default=no)), 
	,
	[enable_debug="no"])
AC_MSG_RESULT($enable_debug)
if test "$enable_debug" = "yes"; then
	DEBUGFLAGS="-g -ggdb -DDEBUG"
else
	DEBUGFLAGS=""
fi
AC_SUBST(DEBUGFLAGS)

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
