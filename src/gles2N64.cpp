// gles2N64.cpp

#include <dlfcn.h>
#include <pthread.h>
#include <string.h>

#include "m64p_types.h"
#include "m64p_plugin.h"

#include "gles2N64.h"
#include "VideoThread.h"
#include "Debug.h"
#include "OpenGL.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "VI.h"
#include "Config.h"
#include "Textures.h"
#include "ShaderCombiner.h"
#include "3DMath.h"
#include "FrameSkipper.h"
#include "ticks.h"

// for SDL_GetTicks
#include <SDL.h>

GFX_INFO mainThreadGFXInfo = { 0 };

// Must be called with the lock held!
void WaitForVideoThreadToBecomeIdle()
{
    while (videoThreadControl.state != VIDEO_THREAD_STATE_IDLE)
        pthread_cond_wait(&videoThreadControl.condvar, &videoThreadControl.lock);
}

// Must be called with the lock held!
void SendSyncCommand(VideoThreadCommandType type)
{
    WaitForVideoThreadToBecomeIdle();

    videoThreadControl.command.type = type;
    videoThreadControl.state = VIDEO_THREAD_STATE_PROCESSING_COMMAND;
    pthread_cond_broadcast(&videoThreadControl.condvar);
    WaitForVideoThreadToBecomeIdle();
    pthread_mutex_unlock(&videoThreadControl.lock);
}

// Must be called with the lock held!
void SendAsyncCommand(VideoThreadCommandType type)
{
    WaitForVideoThreadToBecomeIdle();

    videoThreadControl.command.type = type;
    videoThreadControl.state = VIDEO_THREAD_STATE_PROCESSING_COMMAND;
    pthread_cond_broadcast(&videoThreadControl.condvar);
    pthread_mutex_unlock(&videoThreadControl.lock);
}

bool VideoThreadIsIdle()
{
}

extern "C" {

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle,
                                     void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, VideoThread, NULL) < 0) {
        fprintf(stderr, "Failed to create video thread!\n");
        abort();
    }

    pthread_mutex_lock(&videoThreadControl.lock);
    videoThreadControl.command.parameters.PluginStartup.CoreLibHandle = CoreLibHandle;
    videoThreadControl.command.parameters.PluginStartup.Context = Context;
    videoThreadControl.command.parameters.PluginStartup.DebugCallback = DebugCallback;
    SendSyncCommand(VIDEO_THREAD_COMMAND_PLUGIN_STARTUP);
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    pthread_mutex_lock(&videoThreadControl.lock);
    SendSyncCommand(VIDEO_THREAD_COMMAND_PLUGIN_SHUTDOWN);
	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType,
        int *PluginVersion, int *APIVersion, const char **PluginNamePtr,
        int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_GFX;

    if (PluginVersion != NULL)
        *PluginVersion = PLUGIN_VERSION;

    if (APIVersion != NULL)
        *APIVersion = PLUGIN_API_VERSION;
    
    if (PluginNamePtr != NULL)
        *PluginNamePtr = PLUGIN_NAME;

    if (Capabilities != NULL)
    {
        *Capabilities = 0;
    }
                    
    return M64ERR_SUCCESS;
}

EXPORT void CALL ChangeWindow (void)
{
}

EXPORT void CALL MoveScreen (int xpos, int ypos)
{
}

EXPORT int CALL InitiateGFX (GFX_INFO Gfx_Info)
{
    mainThreadGFXInfo = Gfx_Info;

    pthread_mutex_lock(&videoThreadControl.lock);
    videoThreadControl.command.parameters.InitiateGFX.Gfx_Info = Gfx_Info;
    SendSyncCommand(VIDEO_THREAD_COMMAND_INITIATE_GFX);
    return 1;
}

int frameCount = 0;

EXPORT void CALL ProcessDList(void)
{
    static unsigned lastFrameTime = 0;
    unsigned thisFrameTime = SDL_GetTicks();
    printf("non-RSP time: %d ms\n", (int)(thisFrameTime - lastFrameTime));
    lastFrameTime = thisFrameTime;

    pthread_mutex_lock(&videoThreadControl.lock);
    if (videoThreadControl.state == VIDEO_THREAD_STATE_IDLE) {
        videoThreadControl.command.parameters.PrepareDList.gfxInfo = mainThreadGFXInfo;
        SendSyncCommand(VIDEO_THREAD_COMMAND_PREPARE_DLIST);

        pthread_mutex_lock(&videoThreadControl.lock);
        SendAsyncCommand(VIDEO_THREAD_COMMAND_PROCESS_DLIST);
    } else {
        pthread_mutex_unlock(&videoThreadControl.lock);
    }

    // Hack to avoid hang!
    *mainThreadGFXInfo.MI_INTR_REG |= MI_INTR_DP;
    mainThreadGFXInfo.CheckInterrupts();
    *mainThreadGFXInfo.MI_INTR_REG |= MI_INTR_SP;
    mainThreadGFXInfo.CheckInterrupts();
}

EXPORT void CALL ProcessRDPList(void)
{
}

EXPORT void CALL ResizeVideoOutput(int Width, int Height)
{
}

EXPORT void CALL RomClosed (void)
{
}

EXPORT int CALL RomOpen (void)
{
    pthread_mutex_lock(&videoThreadControl.lock);
    SendSyncCommand(VIDEO_THREAD_COMMAND_ROM_OPEN);
    return 1;
}

EXPORT void CALL RomResumed(void)
{
    pthread_mutex_lock(&videoThreadControl.lock);
    SendSyncCommand(VIDEO_THREAD_COMMAND_ROM_RESUMED);
}

EXPORT void CALL ShowCFB (void)
{
}

EXPORT void CALL UpdateScreen (void)
{
    /*pthread_mutex_lock(&videoThreadControl.lock);
    videoThreadControl.command.parameters.UpdateScreen.gfxInfo = mainThreadGFXInfo;
    SendSyncCommand(VIDEO_THREAD_COMMAND_UPDATE_SCREEN);*/
}

EXPORT void CALL ViStatusChanged (void)
{
}

EXPORT void CALL ViWidthChanged (void)
{
}

EXPORT void CALL FBRead(u32 addr)
{
}

EXPORT void CALL FBWrite(u32 addr, u32 size)
{
}

EXPORT void CALL FBGetFrameBufferInfo(void *p)
{
}

// paulscode, API changed this to "ReadScreen2" in Mupen64Plus 1.99.4
EXPORT void CALL ReadScreen2(void *dest, int *width, int *height, int front)
{
    pthread_mutex_lock(&videoThreadControl.lock);
    videoThreadControl.command.parameters.ReadScreen2.dest = dest;
    videoThreadControl.command.parameters.ReadScreen2.width = width;
    videoThreadControl.command.parameters.ReadScreen2.height = height;
    videoThreadControl.command.parameters.ReadScreen2.front = front;
    SendSyncCommand(VIDEO_THREAD_COMMAND_READ_SCREEN_2);
}

EXPORT void CALL SetRenderingCallback(void (*callback)())
{
    pthread_mutex_lock(&videoThreadControl.lock);
    videoThreadControl.command.parameters.SetRenderingCallback.callback = callback;
    SendSyncCommand(VIDEO_THREAD_COMMAND_SET_RENDERING_CALLBACK);
}

EXPORT void CALL SetFrameSkipping(bool autoSkip, int maxSkips)
{
    pthread_mutex_lock(&videoThreadControl.lock);
    videoThreadControl.command.parameters.SetFrameSkipping.autoSkip = autoSkip;
    videoThreadControl.command.parameters.SetFrameSkipping.maxSkips = maxSkips;
    SendSyncCommand(VIDEO_THREAD_COMMAND_SET_FRAME_SKIPPING);
}

EXPORT void CALL SetStretchVideo(bool stretch)
{
    pthread_mutex_lock(&videoThreadControl.lock);
    videoThreadControl.command.parameters.SetStretchVideo.stretch = stretch;
    SendSyncCommand(VIDEO_THREAD_COMMAND_SET_STRETCH_VIDEO);
}

EXPORT void CALL StartGL()
{
    pthread_mutex_lock(&videoThreadControl.lock);
    SendSyncCommand(VIDEO_THREAD_COMMAND_START_GL);
}

EXPORT void CALL StopGL()
{
    pthread_mutex_lock(&videoThreadControl.lock);
    SendSyncCommand(VIDEO_THREAD_COMMAND_STOP_GL);
}

EXPORT void CALL ResizeGL(int width, int height)
{
    pthread_mutex_lock(&videoThreadControl.lock);
    videoThreadControl.command.parameters.ResizeGL.width = width;
    videoThreadControl.command.parameters.ResizeGL.height = height;
    SendSyncCommand(VIDEO_THREAD_COMMAND_RESIZE_GL);
}

} // extern "C"

