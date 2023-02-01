/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <math.h>

#include <pulse/pulseaudio.h>

#include "lvgl/lvgl.h"
#include "audio.h"
#include "meter.h"
#include "dsp.h"

#define AUDIO_RATE_MS   100

static pa_threaded_mainloop *mloop;
static pa_mainloop_api      *mlapi;
static pa_context           *ctx;

static pa_stream            *play_stm;
static char                 *play_device_id;

static pa_stream            *capture_stm;
static char                 *capture_device_id;

static void on_state_change(pa_context *c, void *userdata) {
    pa_threaded_mainloop_signal(mloop, 0);
}

static void on_dev_sink(pa_context *c, const pa_sink_info *info, int eol, void *udata) {
    if (eol != 0) {
        pa_threaded_mainloop_signal(mloop, 0);
        return;
    }

    play_device_id = info->name;
    LV_LOG_INFO("Sink %s", info->name);
}

static void on_dev_source(pa_context *c, const pa_source_info *info, int eol, void *udata) {
    if (eol != 0) {
        pa_threaded_mainloop_signal(mloop, 0);
        return;
    }

    capture_device_id = info->name;
    LV_LOG_INFO("Source %s", info->name);
}

static float phase = 0;

static void write_callback(pa_stream *s, size_t nbytes, void *udata) {
    int16_t *buf = NULL;

    int res = pa_stream_begin_write(play_stm, &buf, &nbytes);
    
    if (res != 0 || buf == NULL) {
        return;
    }
    
    pa_stream_write(play_stm, buf, nbytes, NULL, 0, PA_SEEK_RELATIVE);
}

static void read_callback(pa_stream *s, size_t nbytes, void *udata) {
   int16_t *buf = NULL;
        
   pa_stream_peek(capture_stm, &buf, &nbytes);

   pa_stream_drop(capture_stm);
}

void audio_init() {
    pthread_t thread;

    mloop = pa_threaded_mainloop_new();
    pa_threaded_mainloop_start(mloop);
    
    mlapi = pa_threaded_mainloop_get_api(mloop);
    ctx = pa_context_new(mlapi, "X6100 GUI");

    pa_threaded_mainloop_lock(mloop);
    pa_context_set_state_callback(ctx, on_state_change, NULL);
    pa_context_connect(ctx, NULL, 0, NULL);
    pa_threaded_mainloop_unlock(mloop);
    
    while (PA_CONTEXT_READY != pa_context_get_state(ctx))  {
        pa_threaded_mainloop_wait(mloop);
    }
    
    LV_LOG_INFO("Conected");
    
    pa_operation    *op;
    pa_buffer_attr  attr;

    pa_sample_spec  spec = {
        .format = PA_SAMPLE_S16NE,
        .rate = 11025,
        .channels = 1
    };

    memset(&attr, 0xff, sizeof(attr));
    attr.fragsize = attr.tlength = pa_usec_to_bytes(AUDIO_RATE_MS * PA_USEC_PER_MSEC, &spec);

    /* Play */
    
    op = pa_context_get_sink_info_list(ctx, on_dev_sink, NULL);
    
    play_stm = pa_stream_new(ctx, "X6100 GUI Play", &spec, NULL);

    pa_threaded_mainloop_lock(mloop);
    pa_stream_set_write_callback(play_stm, write_callback, NULL);
    pa_stream_connect_playback(play_stm, play_device_id, &attr, PA_STREAM_ADJUST_LATENCY, NULL, NULL);
    pa_threaded_mainloop_unlock(mloop);

    /* Capture */
    
    op = pa_context_get_source_info_list(ctx, on_dev_source, NULL);

    capture_stm = pa_stream_new(ctx, "X6100 GUI Capture", &spec, NULL);
    
    pa_threaded_mainloop_lock(mloop);
    pa_stream_set_read_callback(capture_stm, read_callback, NULL);
    pa_stream_connect_record(capture_stm, capture_device_id, &attr, PA_STREAM_ADJUST_LATENCY);
    pa_threaded_mainloop_unlock(mloop);
}
