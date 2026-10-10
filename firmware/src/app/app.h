/* SPDX-License-Identifier: GPL-3.0-only */
/* Mocky app: the project (what is saved besides the pictures) and the app's entry points. Included
 * by the unity build after gfx.c (drawing helpers in scope) and by the host simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"

#ifndef OM_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define OM_VERSION "DEV"
#endif
#define PROJ_MAGIC 0x4B434F4Du           /* "MOCK" */
#define PROJ_FORMAT 1u

typedef struct {
    uint32_t magic, format;
    uint16_t cur;                        /* the picture on the screen (gallery index) */
    uint8_t show_s;                      /* slideshow: seconds per picture */
    uint8_t rsv[13];
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
void ui_say(const char *a, const char *b);
