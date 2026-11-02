#include "tts_toggle.h"
#include "options.h"
#include "fileops.h"
#include "tuxmath.h"
#include "globals.h"
#include "setup.h"       /* for local_game */
#include "t4k_common.h"  /* for T4K_Tts_say */

/* These functions are defined in comets.c but have no shared header */
extern void stop_tts_announcer_thread(void);
extern void start_tts_announcer_thread(void);


void ToggleTTS(void)
{
#if defined(_WIN32) || defined(WIN32)
    static Uint32 last_toggle_time = 0;
    Uint32 current_time = SDL_GetTicks();
    if (current_time - last_toggle_time < 200)
        return;
    last_toggle_time = current_time;
#endif

    if (Opts_GetGlobalOpt(USE_TTS) == 1)
    {
        /* Stop the in-game TTS announcer thread if it is running */
        extern SDL_Thread* tts_announcer_thread;
        if (tts_announcer_thread)
            stop_tts_announcer_thread();

        /* Announce BEFORE turning off so the user can hear the confirmation */
        T4K_Tts_say(DEFAULT_VALUE, DEFAULT_VALUE, INTERRUPT,
                     _("Text to speech is disabled"));
        Opts_SetGlobalOpt(USE_TTS, 0);   /* also sets text_to_speech_status = 0 */
        T4K_Tts_set_status(0);           /* sync DLL state */
    }
    else
    {
        Opts_SetGlobalOpt(USE_TTS, 1);   /* also sets text_to_speech_status = 1 */
        T4K_Tts_set_status(1);           /* sync DLL state */
        T4K_Tts_set_voice("en");

        T4K_Tts_say(DEFAULT_VALUE, DEFAULT_VALUE, INTERRUPT,
                     _("Text to speech is enabled"));
    }

    /* Persist the new preference to the user config file */
    write_user_config_file(local_game);
}


int Tux_pollEvent(SDL_Event *event)
{
    int ret = SDL_PollEvent(event);

    if (ret)
    {
        if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            event->type = SDL_EVENT_QUIT;

        if (event->type == SDL_EVENT_WINDOW_RESIZED ||
            event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
            event->type == SDL_EVENT_WINDOW_RESTORED)
        {
            T4K_UpdateScreenSize();
            screen = T4K_GetScreen();
            if (T4K_GetWindow())
            {
                Uint32 wflags = SDL_GetWindowFlags(T4K_GetWindow());
                Opts_SetGlobalOpt(FULLSCREEN, (wflags & SDL_WINDOW_FULLSCREEN) != 0);
            }
        }

        if (event->type == SDL_EVENT_KEY_DOWN)
        {
            if (event->key.key == SDLK_F5)
                ToggleTTS();
            else if (event->key.key == SDLK_F10)
            {
                T4K_SwitchScreenMode();
                screen = T4K_GetScreen();
                if (T4K_GetWindow())
                {
                    Uint32 wflags = SDL_GetWindowFlags(T4K_GetWindow());
                    Opts_SetGlobalOpt(FULLSCREEN, (wflags & SDL_WINDOW_FULLSCREEN) != 0);
                    write_user_config_file(local_game);
                }
            }
#ifndef NOSOUND
            else if (event->key.key == SDLK_F11)
            {
                T4K_AudioToggle();
            }
#endif
        }
    }
    return ret;
}
