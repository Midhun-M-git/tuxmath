/*
   game.c: Accessible in-game pause screen and audio settings dialog
   Copyright (C) 2001-2011 Bill Kendrick, David Bruce, Tim Holy
   Zendalona Accessibility Edition 2026

   Part of TuxMath, a.k.a. "Tux, of Math Command"
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL3/SDL.h>

#include "game.h"
#include "tuxmath.h"
#include "options.h"
#include "fileops.h"
#include "tts_toggle.h"
#include "draw_utils.h"

enum {
    SEL_SFX = 0,
    SEL_MUSIC,
    SEL_SPEECH_RATE,
    NUM_SELECTIONS
};

static const int espeak_rates[5] = { 4, 17, 30, 43, 56 };
static const char* const speech_rate_labels[5] = { "0.5x", "0.75x", "1x", "1.25x", "1.5x" };
static int current_tts_rate = 2; /* 1x normal default */

static SDL_Surface *up_img = NULL;
static SDL_Surface *down_img = NULL;
static SDL_Surface *left_img = NULL;
static SDL_Surface *right_img = NULL;
static SDL_Surface *pause_bkg = NULL;

static void pause_load_media(void)
{
    up_img = T4K_LoadImage("up.png", IMG_ALPHA);
    down_img = T4K_LoadImage("down.png", IMG_ALPHA);
    left_img = T4K_LoadImage("left.png", IMG_ALPHA);
    right_img = T4K_LoadImage("right.png", IMG_ALPHA);
}

static void pause_unload_media(void)
{
    if (pause_bkg)
    {
        SDL_DestroySurface(pause_bkg);
        pause_bkg = NULL;
    }
    if (up_img)
    {
        SDL_DestroySurface(up_img);
        up_img = NULL;
    }
    if (down_img)
    {
        SDL_DestroySurface(down_img);
        down_img = NULL;
    }
    if (left_img)
    {
        SDL_DestroySurface(left_img);
        left_img = NULL;
    }
    if (right_img)
    {
        SDL_DestroySurface(right_img);
        right_img = NULL;
    }
}

static void draw_vols(int sfx_vol, int mus_vol, int tts_rate, int selected_opt,
                      int bar_x, int sfx_y, int mus_y, int srate_y,
                      int seg_w, int seg_h, int seg_step)
{
    static const int srate_segs[5] = { 6, 13, 19, 26, 32 };
    SDL_Rect s_rect, m_rect, r_rect;

    if (tts_rate < 0) tts_rate = 0;
    if (tts_rate > 4) tts_rate = 4;

    s_rect.w = m_rect.w = r_rect.w = seg_w;
    s_rect.h = m_rect.h = r_rect.h = seg_h;

    for (int i = 1; i <= 32; i++)
    {
        int cur_x = bar_x + (i - 1) * seg_step;
        s_rect.x = m_rect.x = r_rect.x = cur_x;
        s_rect.y = sfx_y;
        m_rect.y = mus_y;
        r_rect.y = srate_y;

        /* SFX bar */
        if (sfx_vol >= i * 4)
        {
            Uint32 col = (selected_opt == SEL_SFX)
                ? SDL_MapRGB(screen->format, 50, 200, 255)
                : SDL_MapRGB(screen->format, 0, 90, 180);
            SDL_FillSurfaceRect(screen, &s_rect, col);
        }
        else
        {
            SDL_FillSurfaceRect(screen, &s_rect, SDL_MapRGB(screen->format, 30, 30, 30));
        }

        /* Music bar */
        if (mus_vol >= i * 4)
        {
            Uint32 col = (selected_opt == SEL_MUSIC)
                ? SDL_MapRGB(screen->format, 50, 200, 255)
                : SDL_MapRGB(screen->format, 0, 90, 180);
            SDL_FillSurfaceRect(screen, &m_rect, col);
        }
        else
        {
            SDL_FillSurfaceRect(screen, &m_rect, SDL_MapRGB(screen->format, 30, 30, 30));
        }

        /* Speech Rate bar */
        if (i <= srate_segs[tts_rate])
        {
            Uint32 col = (selected_opt == SEL_SPEECH_RATE)
                ? SDL_MapRGB(screen->format, 80, 255, 120)
                : SDL_MapRGB(screen->format, 0, 170, 70);
            SDL_FillSurfaceRect(screen, &r_rect, col);
        }
        else
        {
            SDL_FillSurfaceRect(screen, &r_rect, SDL_MapRGB(screen->format, 30, 30, 30));
        }
    }
}

static void pause_announce_selection(int selected_opt, int sfx_vol, int mus_vol, int tts_rate)
{
    if (selected_opt == SEL_SFX)
    {
        int pct = (sfx_vol * 100) / 128;
        T4K_Tts_say(DEFAULT_VALUE, DEFAULT_VALUE, INTERRUPT, _("Sound effects volume %d percent"), pct);
    }
    else if (selected_opt == SEL_MUSIC)
    {
        int pct = (mus_vol * 100) / 128;
        T4K_Tts_say(DEFAULT_VALUE, DEFAULT_VALUE, INTERRUPT, _("Music volume %d percent"), pct);
    }
    else if (selected_opt == SEL_SPEECH_RATE)
    {
        if (tts_rate < 0) tts_rate = 0;
        if (tts_rate > 4) tts_rate = 4;
        T4K_Tts_say(espeak_rates[tts_rate], DEFAULT_VALUE, INTERRUPT, _("Speech rate %s"), speech_rate_labels[tts_rate]);
    }
}

/* Pause loop: returns PAUSE_RESUME (0) to continue, or PAUSE_QUIT (1) to exit to menu */
int pause_game(void)
{
    /* Only pause if allowed by options: */
    if (!Opts_AllowPause())
    {
        fprintf(stderr, "Pause requested but not allowed by Opts!\n");
        return PAUSE_RESUME;
    }

    int paused = 1;
    int pause_quit = 0;
    int selected_opt = SEL_SFX;
    int mousePressed = 0;
    int tocks = 0;
    SDL_Event event;

    float scale = get_scale();
    if (scale < 0.6f) scale = 0.6f;

    int font_title = (int)(32 * scale);
    int font_label = (int)(20 * scale);
    int font_hint  = (int)(16 * scale);

    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};

    int seg_w = (int)(5 * scale);
    if (seg_w < 3) seg_w = 3;
    int seg_step = (int)(7 * scale);
    if (seg_step < 5) seg_step = 5;
    int seg_h = (int)(24 * scale);
    if (seg_h < 16) seg_h = 16;

    int total_bar_w = 32 * seg_step;
    int bar_x = screen->w / 2 - total_bar_w / 2;

    int btn_w = (int)(30 * scale);
    int btn_h = seg_h;
    if (left_img) { btn_w = left_img->w; btn_h = left_img->h; }

    int row_spacing = (int)(62 * scale);
    int sfx_y   = screen->h / 2 - row_spacing + (int)(10 * scale);
    int mus_y   = screen->h / 2 + (int)(10 * scale);
    int srate_y = screen->h / 2 + row_spacing + (int)(10 * scale);

    SDL_Rect rectSfxLeft    = { bar_x - btn_w - 10, sfx_y, btn_w, btn_h };
    SDL_Rect rectSfxRight   = { bar_x + total_bar_w + 10, sfx_y, btn_w, btn_h };
    SDL_Rect rectMusLeft    = { bar_x - btn_w - 10, mus_y, btn_w, btn_h };
    SDL_Rect rectMusRight   = { bar_x + total_bar_w + 10, mus_y, btn_w, btn_h };
    SDL_Rect rectSrateLeft  = { bar_x - btn_w - 10, srate_y, btn_w, btn_h };
    SDL_Rect rectSrateRight = { bar_x + total_bar_w + 10, srate_y, btn_w, btn_h };

    int sfx_volume = (int)(T4K_AudioGetSfxVolume() * 128.0f);
    int mus_volume = (int)(T4K_AudioGetMusicVolume() * 128.0f);
    int tts_rate   = current_tts_rate;

    pause_load_media();

    /* Pause music playback smoothly */
    T4K_AudioMusicPause();

    /* Audible pause indication */
    if (sounds[SND_TOCK])
        T4K_PlaySound(sounds[SND_TOCK]);

    /* Audible TTS pause announcement */
    T4K_Tts_say(DEFAULT_VALUE, DEFAULT_VALUE, INTERRUPT,
                _("Game Paused. Tab or arrows to adjust audio, Space to resume, Escape to quit."));

    /* Dim the game screen */
    T4K_DarkenScreen(1);
    if (pause_bkg)
        SDL_DestroySurface(pause_bkg);
    pause_bkg = SDL_DuplicateSurface(screen);

    SDL_ShowCursor();

    while (paused)
    {
        int old_sfx = sfx_volume;
        int old_mus = mus_volume;
        int old_rate = tts_rate;

        while (Tux_pollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    user_quit_received = GAME_OVER_WINDOW_CLOSE;
                    paused = 0;
                    pause_quit = 1;
                    {
                        SDL_Event qe = { .type = SDL_EVENT_QUIT };
                        SDL_PushEvent(&qe);
                    }
                    break;

                case SDL_EVENT_KEY_DOWN:
                    switch (event.key.key)
                    {
                        case SDLK_ESCAPE:
                            paused = 0;
                            pause_quit = 1;
                            break;

                        case SDLK_SPACE:
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            paused = 0;
                            pause_quit = 0;
                            break;

                        case SDLK_TAB:
                            if (SDL_GetModState() & SDL_KMOD_SHIFT)
                                selected_opt = (selected_opt + NUM_SELECTIONS - 1) % NUM_SELECTIONS;
                            else
                                selected_opt = (selected_opt + 1) % NUM_SELECTIONS;
                            pause_announce_selection(selected_opt, sfx_volume, mus_volume, tts_rate);
                            break;

                        case SDLK_UP:
                            selected_opt = (selected_opt + NUM_SELECTIONS - 1) % NUM_SELECTIONS;
                            pause_announce_selection(selected_opt, sfx_volume, mus_volume, tts_rate);
                            break;

                        case SDLK_DOWN:
                            selected_opt = (selected_opt + 1) % NUM_SELECTIONS;
                            pause_announce_selection(selected_opt, sfx_volume, mus_volume, tts_rate);
                            break;

                        case SDLK_LEFT:
                            if (selected_opt == SEL_SFX)
                                sfx_volume -= 4;
                            else if (selected_opt == SEL_MUSIC)
                                mus_volume -= 4;
                            else if (selected_opt == SEL_SPEECH_RATE)
                                tts_rate--;
                            break;

                        case SDLK_RIGHT:
                            if (selected_opt == SEL_SFX)
                                sfx_volume += 4;
                            else if (selected_opt == SEL_MUSIC)
                                mus_volume += 4;
                            else if (selected_opt == SEL_SPEECH_RATE)
                                tts_rate++;
                            break;

                        case SDLK_PAGEUP:
                            tts_rate++;
                            break;

                        case SDLK_PAGEDOWN:
                            tts_rate--;
                            break;

                        default:
                            break;
                    }
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    mousePressed = 1;
                    tocks = 0;
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    mousePressed = 0;
                    break;

                default:
                    break;
            }
        }

        if (mousePressed)
        {
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            int x = (int)mx;
            int y = (int)my;

            /* Check button clicks */
            if (T4K_inRect(rectSfxLeft, x, y))
            {
                sfx_volume -= 4;
                selected_opt = SEL_SFX;
            }
            else if (T4K_inRect(rectSfxRight, x, y))
            {
                sfx_volume += 4;
                selected_opt = SEL_SFX;
            }
            else if (T4K_inRect(rectMusLeft, x, y))
            {
                mus_volume -= 4;
                selected_opt = SEL_MUSIC;
            }
            else if (T4K_inRect(rectMusRight, x, y))
            {
                mus_volume += 4;
                selected_opt = SEL_MUSIC;
            }
            else if (T4K_inRect(rectSrateLeft, x, y))
            {
                tts_rate--;
                selected_opt = SEL_SPEECH_RATE;
            }
            else if (T4K_inRect(rectSrateRight, x, y))
            {
                tts_rate++;
                selected_opt = SEL_SPEECH_RATE;
            }
            else if (x >= bar_x && x <= bar_x + total_bar_w)
            {
                float ratio = (float)(x - bar_x) / (float)total_bar_w;
                if (ratio < 0.0f) ratio = 0.0f;
                if (ratio > 1.0f) ratio = 1.0f;

                if (y >= sfx_y && y <= sfx_y + seg_h)
                {
                    sfx_volume = (int)(ratio * 128.0f);
                    selected_opt = SEL_SFX;
                }
                else if (y >= mus_y && y <= mus_y + seg_h)
                {
                    mus_volume = (int)(ratio * 128.0f);
                    selected_opt = SEL_MUSIC;
                }
                else if (y >= srate_y && y <= srate_y + seg_h)
                {
                    tts_rate = (int)(ratio * 5.0f);
                    if (tts_rate > 4) tts_rate = 4;
                    selected_opt = SEL_SPEECH_RATE;
                }
            }
        }

        /* Clamp values */
        if (sfx_volume < 0) sfx_volume = 0;
        if (sfx_volume > 128) sfx_volume = 128;
        if (mus_volume < 0) mus_volume = 0;
        if (mus_volume > 128) mus_volume = 128;
        if (tts_rate < 0) tts_rate = 0;
        if (tts_rate > 4) tts_rate = 4;

        /* Apply updates */
        if (sfx_volume != old_sfx)
        {
            T4K_AudioSetSfxVolume((float)sfx_volume / 128.0f);
            if (tocks % 4 == 0 && sounds[SND_TOCK])
                T4K_PlaySound(sounds[SND_TOCK]);
            tocks++;
        }

        if (mus_volume != old_mus)
        {
            T4K_AudioSetMusicVolume((float)mus_volume / 128.0f);
        }

        if (tts_rate != old_rate)
        {
            current_tts_rate = tts_rate;
            T4K_Tts_set_rate(espeak_rates[tts_rate]);
            T4K_Tts_say(espeak_rates[tts_rate], DEFAULT_VALUE, INTERRUPT,
                        _("Speech rate %s"), speech_rate_labels[tts_rate]);
        }

        /* Redraw pause dialog */
        if (pause_bkg)
        {
            SDL_SetSurfaceBlendMode(pause_bkg, SDL_BLENDMODE_NONE);
            SDL_BlitSurface(pause_bkg, NULL, screen, NULL);
        }

        /* Title */
        SDL_Surface *t_surf = T4K_BlackOutline(_("Paused!"), font_title, &white);
        if (t_surf)
        {
            SDL_Rect tr = { screen->w / 2 - t_surf->w / 2, sfx_y - (int)(90 * scale), t_surf->w, t_surf->h };
            SDL_BlitSurface(t_surf, NULL, screen, &tr);
            SDL_DestroySurface(t_surf);
        }

        /* Row 1 Label: Sound Effects Volume */
        const SDL_Color *sfx_col = (selected_opt == SEL_SFX) ? &yellow : &white;
        char sfx_buf[64];
        snprintf(sfx_buf, sizeof(sfx_buf), "%s: %d%%", _("Sound Effects Volume"), (sfx_volume * 100) / 128);
        t_surf = T4K_BlackOutline(sfx_buf, font_label, sfx_col);
        if (t_surf)
        {
            SDL_Rect tr = { screen->w / 2 - t_surf->w / 2, sfx_y - (int)(28 * scale), t_surf->w, t_surf->h };
            SDL_BlitSurface(t_surf, NULL, screen, &tr);
            SDL_DestroySurface(t_surf);
        }

        /* Row 2 Label: Music Volume */
        const SDL_Color *mus_col = (selected_opt == SEL_MUSIC) ? &yellow : &white;
        char mus_buf[64];
        snprintf(mus_buf, sizeof(mus_buf), "%s: %d%%", _("Music Volume"), (mus_volume * 100) / 128);
        t_surf = T4K_BlackOutline(mus_buf, font_label, mus_col);
        if (t_surf)
        {
            SDL_Rect tr = { screen->w / 2 - t_surf->w / 2, mus_y - (int)(28 * scale), t_surf->w, t_surf->h };
            SDL_BlitSurface(t_surf, NULL, screen, &tr);
            SDL_DestroySurface(t_surf);
        }

        /* Row 3 Label: Speech Rate */
        const SDL_Color *srate_col = (selected_opt == SEL_SPEECH_RATE) ? &yellow : &white;
        char srate_buf[64];
        snprintf(srate_buf, sizeof(srate_buf), "%s: %s", _("Speech Rate"), speech_rate_labels[tts_rate]);
        t_surf = T4K_BlackOutline(srate_buf, font_label, srate_col);
        if (t_surf)
        {
            SDL_Rect tr = { screen->w / 2 - t_surf->w / 2, srate_y - (int)(28 * scale), t_surf->w, t_surf->h };
            SDL_BlitSurface(t_surf, NULL, screen, &tr);
            SDL_DestroySurface(t_surf);
        }

        /* Draw left and right buttons */
        if (left_img)
        {
            SDL_BlitSurface(left_img, NULL, screen, &rectSfxLeft);
            SDL_BlitSurface(left_img, NULL, screen, &rectMusLeft);
            SDL_BlitSurface(left_img, NULL, screen, &rectSrateLeft);
        }
        if (right_img)
        {
            SDL_BlitSurface(right_img, NULL, screen, &rectSfxRight);
            SDL_BlitSurface(right_img, NULL, screen, &rectMusRight);
            SDL_BlitSurface(right_img, NULL, screen, &rectSrateRight);
        }

        /* Draw progress bars */
        draw_vols(sfx_volume, mus_volume, tts_rate, selected_opt,
                  bar_x, sfx_y, mus_y, srate_y,
                  seg_w, seg_h, seg_step);

        /* Navigation hints */
        t_surf = T4K_BlackOutline(_("Tab or Up/Down: Select  |  Left/Right: Adjust  |  F5: TTS"), font_hint, &white);
        if (t_surf)
        {
            SDL_Rect tr = { screen->w / 2 - t_surf->w / 2, srate_y + seg_h + (int)(18 * scale), t_surf->w, t_surf->h };
            SDL_BlitSurface(t_surf, NULL, screen, &tr);
            SDL_DestroySurface(t_surf);
        }

        t_surf = T4K_BlackOutline(_("Press Space to resume  |  Escape to return to menu"), font_hint, &white);
        if (t_surf)
        {
            SDL_Rect tr = { screen->w / 2 - t_surf->w / 2, srate_y + seg_h + (int)(40 * scale), t_surf->w, t_surf->h };
            SDL_BlitSurface(t_surf, NULL, screen, &tr);
            SDL_DestroySurface(t_surf);
        }

        T4K_PresentScreen();
        SDL_Delay(33);
    }

    pause_unload_media();

    if (!pause_quit)
    {
        /* Game is resuming */
        T4K_AudioMusicResume();
        T4K_Tts_say(DEFAULT_VALUE, DEFAULT_VALUE, INTERRUPT, _("Game Resumed"));
    }

    return pause_quit ? PAUSE_QUIT : PAUSE_RESUME;
}
