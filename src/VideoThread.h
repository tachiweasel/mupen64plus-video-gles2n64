#ifndef VIDEO_THREAD_H
#define VIDEO_THREAD_H

#include <pthread.h>

#include "m64p_types.h"
#include "m64p_plugin.h"

enum VideoThreadState {
    VIDEO_THREAD_STATE_IDLE,
    VIDEO_THREAD_STATE_PROCESSING_COMMAND,
};

enum VideoThreadCommandType {
    VIDEO_THREAD_COMMAND_PLUGIN_STARTUP,
    VIDEO_THREAD_COMMAND_PLUGIN_SHUTDOWN,
    VIDEO_THREAD_COMMAND_INITIATE_GFX,
    VIDEO_THREAD_COMMAND_PREPARE_DLIST,
    VIDEO_THREAD_COMMAND_PROCESS_DLIST,
    VIDEO_THREAD_COMMAND_ROM_OPEN,
    VIDEO_THREAD_COMMAND_ROM_RESUMED,
    VIDEO_THREAD_COMMAND_UPDATE_SCREEN,
    VIDEO_THREAD_COMMAND_READ_SCREEN_2,
    VIDEO_THREAD_COMMAND_SET_RENDERING_CALLBACK,
    VIDEO_THREAD_COMMAND_SET_FRAME_SKIPPING,
    VIDEO_THREAD_COMMAND_SET_STRETCH_VIDEO,
    VIDEO_THREAD_COMMAND_START_GL,
    VIDEO_THREAD_COMMAND_STOP_GL,
    VIDEO_THREAD_COMMAND_RESIZE_GL,
};

struct VideoThreadPluginStartupCommand {
    m64p_dynlib_handle CoreLibHandle;
    void *Context;
    void (*DebugCallback)(void *, int, const char *);
};

struct VideoThreadInitiateGFXCommand {
    GFX_INFO Gfx_Info;
};

struct VideoThreadPrepareDListCommand {
    GFX_INFO gfxInfo;
};

struct VideoThreadUpdateScreenCommand {
    GFX_INFO gfxInfo;
};

struct VideoThreadReadScreen2Command {
    void *dest;
    int *width;
    int *height;
    int front;
};

struct VideoThreadSetRenderingCallbackCommand {
    void (*callback)();
};

struct VideoThreadSetFrameSkippingCommand {
    bool autoSkip;
    int maxSkips;
};

struct VideoThreadSetStretchVideoCommand {
    bool stretch;
};

struct VideoThreadResizeGLCommand {
    int width;
    int height;
};

union VideoThreadCommandParameters {
    VideoThreadPluginStartupCommand PluginStartup;
    VideoThreadInitiateGFXCommand InitiateGFX;
    VideoThreadPrepareDListCommand PrepareDList;
    VideoThreadUpdateScreenCommand UpdateScreen;
    VideoThreadReadScreen2Command ReadScreen2;
    VideoThreadSetRenderingCallbackCommand SetRenderingCallback;
    VideoThreadSetFrameSkippingCommand SetFrameSkipping;
    VideoThreadSetStretchVideoCommand SetStretchVideo;
    VideoThreadResizeGLCommand ResizeGL;
};

struct VideoThreadCommand {
    VideoThreadCommandType type;
    VideoThreadCommandParameters parameters;
};

struct VideoThreadControl {
    pthread_mutex_t lock;
    pthread_cond_t condvar;
    VideoThreadState state;
    VideoThreadCommand command;
};

extern VideoThreadControl videoThreadControl;

void *VideoThread(void *unused);

#endif

