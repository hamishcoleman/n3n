/*
 * Copyright (C) 2024 Hamish Coleman
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Basic definitions needed for any windows compile
 *
 */

#ifndef _WIN32_DEFS_H_
#define _WIN32_DEFS_H_

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#define WIN32_LEAN_AND_MEAN

#ifndef _WIN64
/* needs to be defined before winsock gets included */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x501

const char *fill_inet_ntop (int, const void *, char *, int);
int fill_inet_pton (int af, const char *restrict src, void *restrict dst);
#define inet_ntop fill_inet_ntop
#define inet_pton fill_inet_pton

void fill_timersub (struct timeval *a, struct timeval *b, struct timeval *res);
#define timersub fill_timersub
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

extern void destroyWin32 ();

#endif
