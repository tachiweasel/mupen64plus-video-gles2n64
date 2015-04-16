
#include <dlfcn.h>
#include <string.h>
//#include <cpu-features.h>

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

//for SDL_GetTicks
#include <SDL.h>

//#include "ae_bridge.h"

ptr_ConfigGetSharedDataFilepath ConfigGetSharedDataFilepath = NULL;
ptr_ConfigGetUserConfigPath 	ConfigGetUserConfigPath = NULL;
ptr_VidExt_GL_SwapBuffers      	CoreVideo_GL_SwapBuffers = NULL;
ptr_VidExt_SetVideoMode         CoreVideo_SetVideoMode = NULL;
ptr_VidExt_GL_SetAttribute      CoreVideo_GL_SetAttribute = NULL;
ptr_VidExt_GL_GetAttribute      CoreVideo_GL_GetAttribute = NULL;
ptr_VidExt_Init			CoreVideo_Init = NULL;
ptr_VidExt_Quit                 CoreVideo_Quit = NULL;

static FrameSkipper frameSkipper;

u32         last_good_ucode = (u32) -1;
void        (*CheckInterrupts)( void );
void        (*renderCallback)() = NULL;

VideoThreadControl videoThreadControl = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    VIDEO_THREAD_STATE_IDLE,
    {
        (VideoThreadCommandType)0
    }
};

struct VideoThreadSystemState {
    unsigned char *HEADER;
    unsigned char *RDRAM;
    unsigned char *DMEM;
    unsigned char *IMEM;

    uint32_t MI_INTR_REG;

    uint32_t DPC_START_REG;
    uint32_t DPC_END_REG;
    uint32_t DPC_CURRENT_REG;
    uint32_t DPC_STATUS_REG;
    uint32_t DPC_CLOCK_REG;
    uint32_t DPC_BUFBUSY_REG;
    uint32_t DPC_PIPEBUSY_REG;
    uint32_t DPC_TMEM_REG;

    uint32_t VI_STATUS_REG;
    uint32_t VI_ORIGIN_REG;
    uint32_t VI_WIDTH_REG;
    uint32_t VI_INTR_REG;
    uint32_t VI_V_CURRENT_LINE_REG;
    uint32_t VI_TIMING_REG;
    uint32_t VI_V_SYNC_REG;
    uint32_t VI_H_SYNC_REG;
    uint32_t VI_LEAP_REG;
    uint32_t VI_H_START_REG;
    uint32_t VI_V_START_REG;
    uint32_t VI_V_BURST_REG;
    uint32_t VI_X_SCALE_REG;
    uint32_t VI_Y_SCALE_REG;
};

VideoThreadSystemState videoThreadSystemState;

void InitVideoThreadSystemState(VideoThreadSystemState *state)
{
    memset(state, '\0', sizeof(*state));
    state->HEADER = (unsigned char *)malloc(0x40);
    state->RDRAM = (unsigned char *)malloc(4 * 1024 * 1024);
    state->DMEM = (unsigned char *)malloc(4 * 1024);
    state->IMEM = (unsigned char *)malloc(4 * 1024);
}

void CopyVideoThreadSystemStateFromGFXInfo(VideoThreadSystemState *state, GFX_INFO *info)
{
    memcpy(state->HEADER, info->HEADER, 0x40);
    memcpy(state->RDRAM, info->RDRAM, 4 * 1024 * 1024);
    memcpy(state->DMEM, info->DMEM, 4 * 1024);
    memcpy(state->IMEM, info->IMEM, 4 * 1024);

    state->MI_INTR_REG = *info->MI_INTR_REG;

    state->DPC_START_REG = *info->DPC_START_REG;
    state->DPC_END_REG = *info->DPC_END_REG;
    state->DPC_CURRENT_REG = *info->DPC_CURRENT_REG;
    state->DPC_STATUS_REG = *info->DPC_STATUS_REG;
    state->DPC_CLOCK_REG = *info->DPC_CLOCK_REG;
    state->DPC_BUFBUSY_REG = *info->DPC_BUFBUSY_REG;
    state->DPC_PIPEBUSY_REG = *info->DPC_PIPEBUSY_REG;
    state->DPC_TMEM_REG = *info->DPC_TMEM_REG;

    state->VI_STATUS_REG = *info->VI_STATUS_REG;
    state->VI_ORIGIN_REG = *info->VI_ORIGIN_REG;
    state->VI_WIDTH_REG = *info->VI_WIDTH_REG;
    state->VI_INTR_REG = *info->VI_INTR_REG;
    state->VI_V_CURRENT_LINE_REG = *info->VI_V_CURRENT_LINE_REG;
    state->VI_TIMING_REG = *info->VI_TIMING_REG;
    state->VI_V_SYNC_REG = *info->VI_V_SYNC_REG;
    state->VI_H_SYNC_REG = *info->VI_H_SYNC_REG;
    state->VI_LEAP_REG = *info->VI_LEAP_REG;
    state->VI_H_START_REG = *info->VI_H_START_REG;
    state->VI_V_START_REG = *info->VI_V_START_REG;
    state->VI_V_BURST_REG = *info->VI_V_BURST_REG;
    state->VI_X_SCALE_REG = *info->VI_X_SCALE_REG;
    state->VI_Y_SCALE_REG = *info->VI_Y_SCALE_REG;
}

void CopyGFXInfoFromVideoThreadSystemState(GFX_INFO *info, VideoThreadSystemState *state)
{
    memcpy(info->HEADER, state->HEADER, 0x40);
    memcpy(info->RDRAM, state->RDRAM, 4 * 1024 * 1024);
    memcpy(info->DMEM, state->DMEM, 4 * 1024);
    memcpy(info->IMEM, state->IMEM, 4 * 1024);

    *info->MI_INTR_REG = state->MI_INTR_REG;

    *info->DPC_START_REG = state->DPC_START_REG;
    *info->DPC_END_REG = state->DPC_END_REG;
    *info->DPC_CURRENT_REG = state->DPC_CURRENT_REG;
    *info->DPC_STATUS_REG = state->DPC_STATUS_REG;
    *info->DPC_CLOCK_REG = state->DPC_CLOCK_REG;
    *info->DPC_BUFBUSY_REG = state->DPC_BUFBUSY_REG;
    *info->DPC_PIPEBUSY_REG = state->DPC_PIPEBUSY_REG;
    *info->DPC_TMEM_REG = state->DPC_TMEM_REG;

    *info->VI_STATUS_REG = state->VI_STATUS_REG;
    *info->VI_ORIGIN_REG = state->VI_ORIGIN_REG;
    *info->VI_WIDTH_REG = state->VI_WIDTH_REG;
    *info->VI_INTR_REG = state->VI_INTR_REG;
    *info->VI_V_CURRENT_LINE_REG = state->VI_V_CURRENT_LINE_REG;
    *info->VI_TIMING_REG = state->VI_TIMING_REG;
    *info->VI_V_SYNC_REG = state->VI_V_SYNC_REG;
    *info->VI_H_SYNC_REG = state->VI_H_SYNC_REG;
    *info->VI_LEAP_REG = state->VI_LEAP_REG;
    *info->VI_H_START_REG = state->VI_H_START_REG;
    *info->VI_V_START_REG = state->VI_V_START_REG;
    *info->VI_V_BURST_REG = state->VI_V_BURST_REG;
    *info->VI_X_SCALE_REG = state->VI_X_SCALE_REG;
    *info->VI_Y_SCALE_REG = state->VI_Y_SCALE_REG;
}

m64p_error VideoThreadPluginStartup(m64p_dynlib_handle CoreLibHandle,
        void *Context, void (*DebugCallback)(void *, int, const char *))
{
    	ConfigGetSharedDataFilepath 	= (ptr_ConfigGetSharedDataFilepath)	dlsym(CoreLibHandle, "ConfigGetSharedDataFilepath");
    	ConfigGetUserConfigPath 	= (ptr_ConfigGetUserConfigPath)		dlsym(CoreLibHandle, "ConfigGetUserConfigPath");
	CoreVideo_GL_SwapBuffers 	= (ptr_VidExt_GL_SwapBuffers) 		dlsym(CoreLibHandle, "VidExt_GL_SwapBuffers");
	CoreVideo_SetVideoMode 		= (ptr_VidExt_SetVideoMode)		dlsym(CoreLibHandle, "VidExt_SetVideoMode");
	CoreVideo_GL_SetAttribute 	= (ptr_VidExt_GL_SetAttribute) 		dlsym(CoreLibHandle, "VidExt_GL_SetAttribute");
    	CoreVideo_GL_GetAttribute 	= (ptr_VidExt_GL_GetAttribute) 		dlsym(CoreLibHandle, "VidExt_GL_GetAttribute");
	CoreVideo_Init 			= (ptr_VidExt_Init)			dlsym(CoreLibHandle, "VidExt_Init");
	CoreVideo_Quit 			= (ptr_VidExt_Quit)			dlsym(CoreLibHandle, "VidExt_Quit");

#ifdef __VFP_OPT
    MathInitVFP();
    gSPInitVFP();
#endif

#ifdef __NEON_OPT
    //if (android_getCpuFamily() == ANDROID_CPU_FAMILY_ARM &&
    //        (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON) != 0)
    //{
        MathInitNeon();
        gSPInitNeon();
    //}
#endif

    ticksInitialize();

    InitVideoThreadSystemState(&videoThreadSystemState);

    //int max_frames = Android_JNI_GetMaxFrameSkip();
	//int max_frames = 1;
// TODO: get rid of this, it should be handled through the config file:
  //  if( Android_JNI_GetAutoFrameSkip() )
    //    frameSkipper.setSkips( FrameSkipper::AUTO, max_frames );
   // else
        //frameSkipper.setSkips( FrameSkipper::MANUAL, max_frames );
//
    return M64ERR_SUCCESS;
}

m64p_error VideoThreadPluginShutdown(void)
{
    OGL_Stop();  // paulscode, OGL_Stop missing from Yongzh's code
    if (CoreVideo_Quit) CoreVideo_Quit();
	return M64ERR_SUCCESS;
}

int VideoThreadInitiateGFX (GFX_INFO Gfx_Info)
{
    CopyVideoThreadSystemStateFromGFXInfo(&videoThreadSystemState, &Gfx_Info);

    DMEM = videoThreadSystemState.DMEM;
    IMEM = videoThreadSystemState.IMEM;
    RDRAM = videoThreadSystemState.RDRAM;

    REG.MI_INTR = (u32*) &videoThreadSystemState.MI_INTR_REG;
    REG.DPC_START = (u32*) &videoThreadSystemState.DPC_START_REG;
    REG.DPC_END = (u32*) &videoThreadSystemState.DPC_END_REG;
    REG.DPC_CURRENT = (u32*) &videoThreadSystemState.DPC_CURRENT_REG;
    REG.DPC_STATUS = (u32*) &videoThreadSystemState.DPC_STATUS_REG;
    REG.DPC_CLOCK = (u32*) &videoThreadSystemState.DPC_CLOCK_REG;
    REG.DPC_BUFBUSY = (u32*) &videoThreadSystemState.DPC_BUFBUSY_REG;
    REG.DPC_PIPEBUSY = (u32*) &videoThreadSystemState.DPC_PIPEBUSY_REG;
    REG.DPC_TMEM = (u32*) &videoThreadSystemState.DPC_TMEM_REG;

    REG.VI_STATUS = (u32*) &videoThreadSystemState.VI_STATUS_REG;
    REG.VI_ORIGIN = (u32*) &videoThreadSystemState.VI_ORIGIN_REG;
    REG.VI_WIDTH = (u32*) &videoThreadSystemState.VI_WIDTH_REG;
    REG.VI_INTR = (u32*) &videoThreadSystemState.VI_INTR_REG;
    REG.VI_V_CURRENT_LINE = (u32*) &videoThreadSystemState.VI_V_CURRENT_LINE_REG;
    REG.VI_TIMING = (u32*) &videoThreadSystemState.VI_TIMING_REG;
    REG.VI_V_SYNC = (u32*) &videoThreadSystemState.VI_V_SYNC_REG;
    REG.VI_H_SYNC = (u32*) &videoThreadSystemState.VI_H_SYNC_REG;
    REG.VI_LEAP = (u32*) &videoThreadSystemState.VI_LEAP_REG;
    REG.VI_H_START = (u32*) &videoThreadSystemState.VI_H_START_REG;
    REG.VI_V_START = (u32*) &videoThreadSystemState.VI_V_START_REG;
    REG.VI_V_BURST = (u32*) &videoThreadSystemState.VI_V_BURST_REG;
    REG.VI_X_SCALE = (u32*) &videoThreadSystemState.VI_X_SCALE_REG;
    REG.VI_Y_SCALE = (u32*) &videoThreadSystemState.VI_Y_SCALE_REG;

    Config_LoadConfig();
    Config_LoadRomConfig(Gfx_Info.HEADER);

    OGL_Start();

    return 1;
}

void VideoThreadPrepareDList(GFX_INFO *gfxInfo)
{
    unsigned beforeTime = SDL_GetTicks();

    CopyVideoThreadSystemStateFromGFXInfo(&videoThreadSystemState, gfxInfo);

    unsigned afterTime = SDL_GetTicks();
    printf("DList preparation time: %d ms\n", (int)(afterTime - beforeTime));
}

void VideoThreadProcessDList()
{
    OGL.frame_dl++;

#if 0
    if (config.autoFrameSkip)
    {
        OGL_UpdateFrameTime();

        if (OGL.consecutiveSkips < 1)
        {
            unsigned t = 0;
            for(int i = 0; i < OGL_FRAMETIME_NUM; i++) t += OGL.frameTime[i];
            t *= config.targetFPS;
            if (config.romPAL) t = (t * 5) / 6;
            if (t > (OGL_FRAMETIME_NUM * 1000))
            {
                OGL.consecutiveSkips++;
                OGL.frameSkipped++;
                RSP.busy = FALSE;
                RSP.DList++;

                /* avoid hang on frameskip */
                *REG.MI_INTR |= MI_INTR_DP;
                CheckInterrupts();
                *REG.MI_INTR |= MI_INTR_SP;
                CheckInterrupts();
                return;
            }
        }
    }
    else if (frameSkipper.willSkipNext())
    if ((OGL.frame_dl % 30) != 0) {
        printf("ticks: %d\n", (int)SDL_GetTicks());

        OGL.frameSkipped++;
        RSP.busy = FALSE;
        RSP.DList++;

        /* avoid hang on frameskip */
        *REG.MI_INTR |= MI_INTR_DP;
        CheckInterrupts();
        *REG.MI_INTR |= MI_INTR_SP;
        CheckInterrupts();
        return;
    }
#endif

    unsigned beforeTime = SDL_GetTicks();

    OGL.consecutiveSkips = 0;
    RSP_ProcessDList();
    OGL.mustRenderDlist = true;

    // CopyGFXInfoFromVideoThreadSystemState(gfxInfo, &videoThreadSystemState);

    unsigned afterTime = SDL_GetTicks();
    printf("RSP time: %d ms\n", (int)(afterTime - beforeTime));

    OGL_SwapBuffers();
}

int VideoThreadRomOpen (void)
{
    RSP_Init();
    OGL.frame_vsync = 0;
    OGL.frame_dl = 0;
    OGL.frame_prevdl = -1;
    OGL.mustRenderDlist = false;

    frameSkipper.setTargetFPS(config.romPAL ? 50 : 60);
    frameSkipper.setSkips(config.autoFrameSkip,
        config.frameRenderRate);
    return 1;
}

void VideoThreadRomResumed(void)
{
    frameSkipper.start();
}

void VideoThreadUpdateScreen (GFX_INFO *gfxInfo)
{
    CopyVideoThreadSystemStateFromGFXInfo(&videoThreadSystemState, gfxInfo);

    frameSkipper.update();

    //has there been any display lists since last update
    if (OGL.frame_prevdl == OGL.frame_dl) return;

    OGL.frame_prevdl = OGL.frame_dl;

    if (OGL.frame_dl > 0) OGL.frame_vsync++;

    if (OGL.mustRenderDlist)
    {
		if(config.printFPS)
		{
		    static unsigned int lastTick=0;
		    static int frames=0;
		    unsigned int nowTick = SDL_GetTicks();
		    frames++;
		    if(lastTick + 5000 <= nowTick)
		    {
		        printf("Video: %.3f VI/S\n", frames/5.0);

		        frames = 0;
		        lastTick = nowTick;
		    }
		}
	
        OGL.screenUpdate=true;
        VI_UpdateScreen();
        OGL.mustRenderDlist = false;
    }

    CopyGFXInfoFromVideoThreadSystemState(gfxInfo, &videoThreadSystemState);
}

// paulscode, API changed this to "ReadScreen2" in Mupen64Plus 1.99.4
void VideoThreadReadScreen2(void *dest, int *width, int *height, int front)
{
/* TODO: 'int front' was added in 1.99.4.  What to do with this here? */
    OGL_ReadScreen(dest, width, height);
}

void VideoThreadSetRenderingCallback(void (*callback)())
{
    renderCallback = callback;
}

void VideoThreadSetFrameSkipping(bool autoSkip, int maxSkips)
{
    frameSkipper.setSkips(
            autoSkip ? FrameSkipper::AUTO : FrameSkipper::MANUAL,
            maxSkips);
}

void VideoThreadSetStretchVideo(bool stretch)
{
    config.stretchVideo = stretch;
}

void VideoThreadStartGL()
{
    OGL_Start();
}

void VideoThreadStopGL()
{
    OGL_Stop();
}

void VideoThreadResizeGL(int width, int height)
{
    const float ratio = (config.romPAL ? 9.0f/11.0f : 0.75f);
    int videoWidth = width;
    int videoHeight = height;

    if (!config.stretchVideo) {
        videoWidth = (int) (height / ratio);
        if (videoWidth > width) {
            videoWidth = width;
            videoHeight = (int) (width * ratio);
        }
    }
    int x = (width - videoWidth) / 2;
    int y = (height - videoHeight) / 2;

    OGL_ResizeWindow(x, y, videoWidth, videoHeight);
}

void *VideoThread(void *unused)
{
    pthread_mutex_lock(&videoThreadControl.lock);
    while (true) {
        while (videoThreadControl.state != VIDEO_THREAD_STATE_PROCESSING_COMMAND)
            pthread_cond_wait(&videoThreadControl.condvar, &videoThreadControl.lock);
        pthread_mutex_unlock(&videoThreadControl.lock);

        // FIXME(tachiweasel): Should probably report errors somehow.
        VideoThreadCommandParameters *parameters = &videoThreadControl.command.parameters;
        switch (videoThreadControl.command.type) {
        case VIDEO_THREAD_COMMAND_PLUGIN_STARTUP:
            VideoThreadPluginStartup(parameters->PluginStartup.CoreLibHandle,
                                     parameters->PluginStartup.Context,
                                     parameters->PluginStartup.DebugCallback);
            break;
        case VIDEO_THREAD_COMMAND_PLUGIN_SHUTDOWN:
            VideoThreadPluginShutdown();
            break;
        case VIDEO_THREAD_COMMAND_INITIATE_GFX:
            VideoThreadInitiateGFX(parameters->InitiateGFX.Gfx_Info);
            break;
        case VIDEO_THREAD_COMMAND_PREPARE_DLIST:
            VideoThreadPrepareDList(&parameters->PrepareDList.gfxInfo);
            break;
        case VIDEO_THREAD_COMMAND_PROCESS_DLIST:
            VideoThreadProcessDList();
            break;
        case VIDEO_THREAD_COMMAND_ROM_OPEN:
            VideoThreadRomOpen();
            break;
        case VIDEO_THREAD_COMMAND_ROM_RESUMED:
            VideoThreadRomResumed();
            break;
        case VIDEO_THREAD_COMMAND_UPDATE_SCREEN:
            VideoThreadUpdateScreen(&parameters->UpdateScreen.gfxInfo);
            break;
        case VIDEO_THREAD_COMMAND_READ_SCREEN_2:
            VideoThreadReadScreen2(parameters->ReadScreen2.dest,
                                   parameters->ReadScreen2.width,
                                   parameters->ReadScreen2.height,
                                   parameters->ReadScreen2.front);
            break;
        case VIDEO_THREAD_COMMAND_SET_RENDERING_CALLBACK:
            VideoThreadSetRenderingCallback(parameters->SetRenderingCallback.callback);
            break;
        case VIDEO_THREAD_COMMAND_SET_FRAME_SKIPPING:
            VideoThreadSetFrameSkipping(parameters->SetFrameSkipping.autoSkip,
                                        parameters->SetFrameSkipping.maxSkips);
            break;
        case VIDEO_THREAD_COMMAND_SET_STRETCH_VIDEO:
            VideoThreadSetStretchVideo(parameters->SetStretchVideo.stretch);
            break;
        case VIDEO_THREAD_COMMAND_START_GL:
            VideoThreadStartGL();
            break;
        case VIDEO_THREAD_COMMAND_STOP_GL:
            VideoThreadStopGL();
            break;
        case VIDEO_THREAD_COMMAND_RESIZE_GL:
            VideoThreadResizeGL(parameters->ResizeGL.width, parameters->ResizeGL.height);
            break;
        }

        pthread_mutex_lock(&videoThreadControl.lock);
        videoThreadControl.state = VIDEO_THREAD_STATE_IDLE;
        pthread_cond_broadcast(&videoThreadControl.condvar);
    }
    return NULL;
}

