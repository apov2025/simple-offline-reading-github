/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 模拟器入口：SDL 窗口跑在主线程，固件的 app_main() 跑在另一个线程。
 * 窗口里是一台用 3D 模型渲染的小纸 Pico 正面（固定正视）：屏幕、屏幕下方三个
 * 触摸键、右侧的电源键都可以直接用鼠标点，还能随机换配色。
 *
 * Simulator entry. The SDL window owns the main thread; the firmware's
 * app_main() runs on its own thread. The window shows a Read Pico rendered
 * from its 3D model, fixed head-on: the screen, the three touch keys below it
 * and the power key on the right edge are clickable, and it can be recolored
 * at random.
 *
 * 操作 / Controls:
 *   鼠标点击、拖动屏幕 / click or drag on the screen   触摸 / touch
 *   点外壳上的三个键，或 1 2 3、← ↓ →                  三个触摸键 / the three touch keys
 *   点右侧电源键，或 P                                  电源键短按（锁屏 / 唤醒）/ power key
 *   C 或 ⌘R  随机配色 / random colorway    O 或 ⌘0  原机配色 / stock colorway
 *            （菜单栏"外观"里也有 / also under 外观 in the menu bar）
 *   F     切换"按真实耗时刷新 / 瞬间刷新" / toggle real-time refresh
 *   M     插拔 TF 卡 / insert or remove the TF card
 *   把 EPUB 或 TTF 拖进窗口：放进模拟 TF 卡的 books / fonts，并模拟一次拔卡再插卡
 *   Drop an EPUB or TTF on the window: it goes into books / fonts on the simulated card, followed by an eject and re-insert
 *   菜单栏"TF 卡"：在访达中打开卡的文件夹（⌘O）、拔插（⌘E）
 *   Menu bar "TF 卡": reveal the card folder in Finder (⌘O), eject / insert (⌘E)
 *   S     截图到 sim/data/shots / screenshot
 *
 * 脚本 / Scripting: SIM_SCRIPT=<文件> 逐行执行 / runs a file line by line:
 *   wait <ms> | tap <x> <y> | swipe <x0> <y0> <x1> <y1> <ms> | key <1-3> [ms]
 *   power | card <0|1> | look <seed> | shot <name> | shell <name> | rebooted | quit
 *   rebooted 是个标记：软休眠被唤醒后模拟器会重启，重启后的脚本从这个标记之后接着跑。
 *   rebooted is a marker: waking from soft sleep restarts the simulator, and the script then continues after it.
 *   坐标是屏幕坐标；shot 只截屏幕，shell 连外壳一起截；look 0 是原机配色。
 *   Coordinates are screen coordinates. shot saves the screen; shell includes the device shell; look 0 is stock.
 */
#include <SDL.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "epdiy.h"
#include "sim.h"
#include "sim_menu.h"
#include "sim_shell.h"

void app_main(void);

static uint8_t s_native[SIM_PANEL_H * SIM_PANEL_W];
static uint8_t s_view[SIM_VIEW_H * SIM_VIEW_W];  // 手持方向，0..15 / as held, 0..15
static uint32_t s_palette[16];

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void* firmware_thread(void* arg) {
    (void)arg;
    app_main();
    fprintf(stderr, "[sim] app_main returned\n");
    return NULL;
}

// 把面板原生朝向的画面转成手持方向，与 epdiy 的 _rotate() 互逆。
// Rotate the native panel image to the as-held orientation; inverse of epdiy _rotate().
static void rotate_to_view(void) {
    const enum EpdRotation rot = epd_get_rotation();
    for (int ly = 0; ly < SIM_VIEW_H; ly++) {
        for (int lx = 0; lx < SIM_VIEW_W; lx++) {
            int nx, ny;
            if (rot == EPD_ROT_PORTRAIT) {
                nx = SIM_PANEL_W - 1 - ly;
                ny = lx;
            } else {
                nx = ly;
                ny = SIM_PANEL_H - 1 - lx;
            }
            s_view[ly * SIM_VIEW_W + lx] = s_native[ny * SIM_PANEL_W + nx];
        }
    }
}

static void save_bmp(const char* name, const uint32_t* argb, int w, int h) {
    char dir[1024], path[1200];
    snprintf(dir, sizeof(dir), "%s/shots", sim_data_dir());
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        (void*)argb, w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888
    );
    if (surface == NULL) return;
    // 存成不带透明通道的 24 位 BMP，系统预览和常见工具都能直接打开。
    // Save as 24-bit BMP without alpha so Preview and common tools open it directly.
    SDL_Surface* rgb = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_BGR24, 0);
    SDL_FreeSurface(surface);
    if (rgb == NULL) return;
    SDL_SaveBMP(rgb, path);
    SDL_FreeSurface(rgb);
    fprintf(stderr, "[sim] screenshot %s\n", path);
}

// 只截屏幕，用纯灰度（不带纸色），方便逐像素比对。
// Screen only, in pure gray (no paper tint) so shots can be compared pixel by pixel.
static void save_shot(const char* name) {
    uint32_t* out = malloc(sizeof(uint32_t) * SIM_VIEW_W * SIM_VIEW_H);
    if (out == NULL) return;
    for (int i = 0; i < SIM_VIEW_W * SIM_VIEW_H; i++) {
        const uint32_t g = (uint32_t)s_view[i] * 17;
        out[i] = 0xFF000000u | g << 16 | g << 8 | g;
    }
    save_bmp(name, out, SIM_VIEW_W, SIM_VIEW_H);
    free(out);
}

// 连外壳一起截，和窗口里看到的一样。任意视角下逐像素反查屏幕坐标。
// Shell included, as seen in the window. Each pixel is mapped back to the screen at any view angle.
static void save_shell_shot(const char* name) {
    uint32_t* out = malloc(sizeof(uint32_t) * SHELL_W * SHELL_H);
    if (out == NULL) return;
    sim_shell_render(out, SHELL_W, SHELL_H, 2);
    float quad[4][2], shade = 1.0f;
    const bool facing = sim_shell_screen_quad(quad, &shade);
    for (int y = 0; y < SHELL_H; y++) {
        for (int x = 0; x < SHELL_W; x++) {
            const uint32_t top = out[y * SHELL_W + x];
            const float a = (float)(top >> 24) / 255.0f;
            if (a >= 1.0f) continue;
            // 外壳在这里开了孔：取孔下面的固件画面，再按不透明度把外壳盖回去。
            // The shell is cut out here: take the firmware image underneath, then lay the shell back by its opacity.
            float sx, sy;
            uint32_t under = s_palette[15];
            if (facing && sim_shell_to_screen((float)x + 0.5f, (float)y + 0.5f, &sx, &sy)) {
                under = s_palette[s_view[clampi((int)sy, 0, SIM_VIEW_H - 1) * SIM_VIEW_W + clampi((int)sx, 0, SIM_VIEW_W - 1)] & 15];
            }
            uint32_t mixed = 0xFF000000u;
            for (int shift = 0; shift <= 16; shift += 8) {
                const float below = (float)(under >> shift & 255) * shade, above = (float)(top >> shift & 255);
                mixed |= (uint32_t)(below * (1.0f - a) + above * a + 0.5f) << shift;
            }
            out[y * SHELL_W + x] = mixed;
        }
    }
    save_bmp(name, out, SHELL_W, SHELL_H);
    free(out);
}

extern char** environ;

// 把拖进窗口的文件放进模拟 TF 卡：EPUB 进 books，TTF 进 fonts。成功返回 true。
// Put a dropped file on the simulated card: EPUB into books, TTF into fonts. True on success.
static bool card_add_file(const char* src, char* status, size_t status_cap) {
    const char* base = strrchr(src, '/');
    base = base != NULL ? base + 1 : src;
    const char* ext = strrchr(base, '.');
    const char* folder = NULL;
    if (ext != NULL && strcasecmp(ext, ".epub") == 0) folder = "books";
    else if (ext != NULL && strcasecmp(ext, ".ttf") == 0) folder = "fonts";
    if (folder == NULL) {
        snprintf(status, status_cap, "只认 EPUB 和 TTF：%s", base);
        return false;
    }
    char dir[1024], dst[1400];
    snprintf(dir, sizeof(dir), "%s/sdcard/%s", sim_data_dir(), folder);
    mkdir(dir, 0755);
    snprintf(dst, sizeof(dst), "%s/%s", dir, base);
    FILE* in = fopen(src, "rb");
    FILE* out = in != NULL ? fopen(dst, "wb") : NULL;
    bool ok = in != NULL && out != NULL;
    char buf[65536];
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0) ok = fwrite(buf, 1, n, out) == n;
    if (in != NULL) fclose(in);
    if (out != NULL && fclose(out) != 0) ok = false;
    snprintf(status, status_cap, ok ? "已放入 %s：%s" : "放入 %s 失败：%s", folder, base);
    return ok;
}

static void reveal_card_folder(void) {
    char path[1100];
    snprintf(path, sizeof(path), "%s/sdcard", sim_data_dir());
    char* const argv[] = { "open", path, NULL };
    pid_t pid;
    (void)posix_spawnp(&pid, "open", NULL, NULL, argv, environ);
}

static void press_key(int key) { sim_touch_set(1, 80 + 160 * key, SIM_KEY_TOUCH_Y, 0, 0); }

/* ---- 脚本 / script ---- */

typedef struct {
    char op[12];
    int a[5];
    char name[64];
} script_cmd_t;

static script_cmd_t* s_script;
static int s_script_n, s_script_pc;
static Uint32 s_script_until;      // 当前命令结束时刻 / when the current command ends
static Uint32 s_script_started;
static bool s_script_active;       // 当前命令正在按住触摸 / current command holds a touch
static bool s_shell_dirty = true;  // 配色变了，外壳要重画 / look changed; re-render the shell

static void script_load(const char* path) {
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "[sim] cannot open script %s\n", path);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        script_cmd_t cmd = { 0 };
        if (line[0] == '#' || sscanf(line, "%11s", cmd.op) != 1) continue;
        if (strcmp(cmd.op, "rebooted") == 0) {
            // 重启后的这一次：标记之前的命令已经在重启前跑过了，丢掉。
            // On the restarted run the commands before the marker already ran; drop them.
            if (getenv("SIM_RESTARTED") != NULL) s_script_n = 0;
            continue;
        }
        const char* rest = line + strspn(line, " \t") + strlen(cmd.op);
        if (strcmp(cmd.op, "shot") == 0 || strcmp(cmd.op, "shell") == 0) sscanf(rest, "%63s", cmd.name);
        else sscanf(rest, "%d %d %d %d %d", &cmd.a[0], &cmd.a[1], &cmd.a[2], &cmd.a[3], &cmd.a[4]);
        s_script = realloc(s_script, sizeof(*s_script) * (size_t)(s_script_n + 1));
        s_script[s_script_n++] = cmd;
    }
    fclose(f);
}

// 返回 false 表示脚本要求退出。/ Returns false when the script asks to quit.
static bool script_step(Uint32 now) {
    while (s_script_pc < s_script_n) {
        const script_cmd_t* c = &s_script[s_script_pc];
        if (s_script_until == 0) {
            s_script_started = now;
            int hold_ms = 0;
            if (strcmp(c->op, "wait") == 0) {
                hold_ms = c->a[0];
            } else if (strcmp(c->op, "tap") == 0) {
                sim_touch_set(1, c->a[0], c->a[1], 0, 0);
                s_script_active = true;
                hold_ms = 120;
            } else if (strcmp(c->op, "swipe") == 0) {
                sim_touch_set(1, c->a[0], c->a[1], 0, 0);
                s_script_active = true;
                hold_ms = c->a[4] > 0 ? c->a[4] : 300;
            } else if (strcmp(c->op, "key") == 0) {
                press_key(c->a[0] - 1);
                s_script_active = true;
                hold_ms = c->a[1] > 0 ? c->a[1] : 120;
            } else if (strcmp(c->op, "power") == 0) {
                sim_power_key_press();
            } else if (strcmp(c->op, "card") == 0) {
                sim_sd_set_present(c->a[0] != 0);
            } else if (strcmp(c->op, "look") == 0) {
                sim_shell_set_look((unsigned)c->a[0]);
                s_shell_dirty = true;
            } else if (strcmp(c->op, "shot") == 0 || strcmp(c->op, "shell") == 0) {
                sim_panel_snapshot(s_native);
                rotate_to_view();
                if (c->op[2] == 'o') save_shot(c->name);
                else save_shell_shot(c->name);
            } else if (strcmp(c->op, "quit") == 0) {
                return false;
            }
            s_script_until = now + (Uint32)hold_ms + 1;
        }
        if (strcmp(c->op, "swipe") == 0 && s_script_active) {
            const float t = (float)(now - s_script_started) / (float)(s_script_until - s_script_started);
            const float k = t > 1.0f ? 1.0f : t;
            sim_touch_set(
                1, c->a[0] + (int)((c->a[2] - c->a[0]) * k), c->a[1] + (int)((c->a[3] - c->a[1]) * k), 0, 0
            );
        }
        if ((Sint32)(now - s_script_until) < 0) return true;
        if (s_script_active) {
            sim_touch_set(0, 0, 0, 0, 0);
            s_script_active = false;
        }
        s_script_until = 0;
        s_script_pc++;
    }
    return true;
}

/* ---- 窗口 / window ---- */

static int key_index(SDL_Keycode sym) {
    switch (sym) {
        case SDLK_1: case SDLK_LEFT: return 0;
        case SDLK_2: case SDLK_DOWN: return 1;
        case SDLK_3: case SDLK_RIGHT: return 2;
        default: return -1;
    }
}

int main(int argc, char** argv) {
    (void)argc;
    sim_set_argv(argv);
    (void)sim_data_dir();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

    // 屏幕放不下原始尺寸就整体缩小。/ Shrink the window when the display cannot fit the native size.
    float scale = 1.0f;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.h > 0) {
        const float fit = (float)(usable.h - 60) / (float)SHELL_H;
        if (fit < scale) scale = fit;
    }
    const char* env_scale = getenv("SIM_SCALE");
    if (env_scale != NULL && atof(env_scale) > 0.1) scale = (float)atof(env_scale);

    SDL_Window* window = SDL_CreateWindow(
        "小纸 Pico 模拟器", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)(SHELL_W * scale),
        (int)(SHELL_H * scale), SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE
    );
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (renderer == NULL && window != NULL) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (window == NULL || renderer == NULL) {
        fprintf(stderr, "SDL window failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, SHELL_W, SHELL_H);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    sim_shell_init();
#ifdef SIM_HAS_MENU
    sim_menu_install();
#endif
    SDL_Texture* shell = NULL;
    uint32_t* shell_pixels = NULL;
    int shell_w = 0, shell_h = 0;
    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SIM_VIEW_W, SIM_VIEW_H
    );

    // 纸白与墨黑取实物的大致色调，不是纯白纯黑。/ Paper white and ink black, tinted roughly like the real panel.
    for (int i = 0; i < 16; i++) {
        const int r = 30 + (228 - 30) * i / 15, g = 30 + (226 - 30) * i / 15, b = 32 + (216 - 32) * i / 15;
        s_palette[i] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }

    const char* script = getenv("SIM_SCRIPT");
    if (script != NULL) script_load(script);
    if (getenv("SIM_FAST") != NULL) sim_set_realtime(false);

    pthread_t fw;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16 * 1024 * 1024);
    pthread_create(&fw, &attr, firmware_thread, NULL);
    pthread_attr_destroy(&attr);

    uint32_t shown_gen = 0;
    int shown_count = -1, shown_sleep = -1;
    bool shown_realtime = true, shown_card = true;
    sim_shell_hit_t mouse_target = SHELL_HIT_NONE;  // 鼠标按下时落在哪 / where the mouse went down
    int held_key = -1;                               // 键盘或鼠标正按着的触摸键 / touch key being held
    Uint32 power_flash_until = 0;
    Uint32 reinsert_at = 0;    // 拖入文件后到这个时刻把卡"插回去" / when to "re-insert" the card after a drop
    char status[256] = "";     // 标题栏里的临时提示 / transient note in the title bar
    Uint32 status_until = 0;
    unsigned look_seed = (unsigned)time(NULL);
    int shot_n = 0;
    bool running = true;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_USEREVENT:
                    // 菜单栏"外观"里的项。/ Items from the 外观 menu in the menu bar.
                    if (ev.user.code == SIM_MENU_RANDOM_LOOK) {
                        sim_shell_set_look(++look_seed);
                        s_shell_dirty = true;
                    } else if (ev.user.code == SIM_MENU_STOCK_LOOK) {
                        sim_shell_set_look(0);
                        s_shell_dirty = true;
                    } else if (ev.user.code == SIM_MENU_OPEN_CARD) {
                        reveal_card_folder();
                    } else if (ev.user.code == SIM_MENU_TOGGLE_CARD) {
                        sim_sd_set_present(!sim_sd_present());
                    }
                    break;
                case SDL_DROPFILE:
                    // 真机上往卡里放书要先把卡拔下来，所以这里放完文件后模拟一次拔卡再插卡，
                    // 固件走的就是和真机一样的路径。
                    // On the device, adding a book means taking the card out, so after copying the file
                    // the card is ejected and re-inserted; the firmware takes the same path as on hardware.
                    if (card_add_file(ev.drop.file, status, sizeof(status))) {
                        sim_sd_set_present(false);
                        reinsert_at = SDL_GetTicks() + 1500;
                    }
                    status_until = SDL_GetTicks() + 4000;
                    shown_count = -1;
                    SDL_free(ev.drop.file);
                    break;
                case SDL_MOUSEBUTTONDOWN: {
                    if (ev.button.button != SDL_BUTTON_LEFT) break;
                    const float mx = (float)ev.button.x, my = (float)ev.button.y;
                    mouse_target = sim_shell_hit(mx, my);
                    if (mouse_target == SHELL_HIT_SCREEN) {
                        float sx, sy;
                        if (sim_shell_to_screen(mx, my, &sx, &sy)) {
                            sim_touch_set(1, clampi((int)sx, 0, SIM_VIEW_W - 1), clampi((int)sy, 0, SIM_VIEW_H - 1), 0, 0);
                        }
                    } else if (mouse_target >= SHELL_HIT_KEY1 && mouse_target <= SHELL_HIT_KEY3) {
                        held_key = mouse_target - SHELL_HIT_KEY1;
                        press_key(held_key);
                    } else if (mouse_target == SHELL_HIT_POWER) {
                        sim_power_key_press();
                        power_flash_until = SDL_GetTicks() + 180;
                    }
                    break;
                }
                case SDL_MOUSEMOTION:
                    if (mouse_target == SHELL_HIT_SCREEN) {
                        // 手指滑出屏幕边缘时坐标停在边上，和真机触摸屏一致。
                        // A drag past the screen edge clamps to the edge, as on the real touch panel.
                        float sx, sy;
                        if (sim_shell_to_screen((float)ev.motion.x, (float)ev.motion.y, &sx, &sy)) {
                            sim_touch_set(1, clampi((int)sx, 0, SIM_VIEW_W - 1), clampi((int)sy, 0, SIM_VIEW_H - 1), 0, 0);
                        }
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (ev.button.button != SDL_BUTTON_LEFT) break;
                    if (mouse_target == SHELL_HIT_SCREEN
                        || (mouse_target >= SHELL_HIT_KEY1 && mouse_target <= SHELL_HIT_KEY3)) {
                        sim_touch_set(0, 0, 0, 0, 0);
                        held_key = -1;
                    }
                    mouse_target = SHELL_HIT_NONE;
                    break;
                case SDL_KEYDOWN: {
                    // 带 ⌘ 的组合键归菜单栏处理，这里不再当成模拟器按键。
                    // ⌘ combinations belong to the menu bar; do not treat them as simulator keys.
                    if (ev.key.repeat || (ev.key.keysym.mod & KMOD_GUI)) break;
                    const int k = key_index(ev.key.keysym.sym);
                    if (k >= 0) {
                        held_key = k;
                        press_key(k);
                        break;
                    }
                    switch (ev.key.keysym.sym) {
                        case SDLK_p:
                            sim_power_key_press();
                            power_flash_until = SDL_GetTicks() + 180;
                            break;
                        case SDLK_f:
                            sim_set_realtime(!sim_realtime());
                            break;
                        case SDLK_m:
                            sim_sd_set_present(!sim_sd_present());
                            break;
                        case SDLK_c:
                            sim_shell_set_look(++look_seed);
                            s_shell_dirty = true;
                            break;
                        case SDLK_o:
                            sim_shell_set_look(0);
                            s_shell_dirty = true;
                            break;
                        case SDLK_s: {
                            char name[32];
                            snprintf(name, sizeof(name), "shot-%03d", ++shot_n);
                            save_shot(name);
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }
                case SDL_KEYUP:
                    if (key_index(ev.key.keysym.sym) == held_key && held_key >= 0) {
                        held_key = -1;
                        sim_touch_set(0, 0, 0, 0, 0);
                    }
                    break;
                default:
                    break;
            }
        }

        if (s_script != NULL && !script_step(SDL_GetTicks())) running = false;
        if (reinsert_at != 0 && (Sint32)(SDL_GetTicks() - reinsert_at) >= 0) {
            reinsert_at = 0;
            sim_sd_set_present(true);
        }
        if (status_until != 0 && (Sint32)(SDL_GetTicks() - status_until) >= 0) {
            status_until = 0;
            status[0] = 0;
            shown_count = -1;
        }

        // 外壳是固定正视的一张图：只有窗口像素尺寸或配色变了才重画，2 倍超采样抗锯齿。
        // The shell is one fixed head-on image: re-render only when the output size or the look changes, 2× supersampled.
        int out_w, out_h;
        SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
        const int want_quality = 2;
        if (out_w > 0 && out_h > 0 && (shell == NULL || out_w != shell_w || out_h != shell_h || s_shell_dirty)) {
            if (shell == NULL || out_w != shell_w || out_h != shell_h) {
                if (shell != NULL) SDL_DestroyTexture(shell);
                shell = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, out_w, out_h);
                if (shell != NULL) SDL_SetTextureBlendMode(shell, SDL_BLENDMODE_BLEND);
                shell_pixels = realloc(shell_pixels, sizeof(uint32_t) * (size_t)out_w * (size_t)out_h);
                shell_w = out_w;
                shell_h = out_h;
            }
            if (shell != NULL && shell_pixels != NULL) {
                sim_shell_render(shell_pixels, shell_w, shell_h, want_quality);
                SDL_UpdateTexture(shell, NULL, shell_pixels, shell_w * 4);
            }
            s_shell_dirty = false;
        }

        const uint32_t gen = sim_panel_snapshot(s_native);
        if (gen != shown_gen) {
            shown_gen = gen;
            rotate_to_view();
            void* pixels;
            int pitch;
            if (SDL_LockTexture(texture, NULL, &pixels, &pitch) == 0) {
                for (int y = 0; y < SIM_VIEW_H; y++) {
                    uint32_t* row = (uint32_t*)((uint8_t*)pixels + y * pitch);
                    const uint8_t* src = &s_view[y * SIM_VIEW_W];
                    for (int x = 0; x < SIM_VIEW_W; x++) row[x] = s_palette[src[x] & 15];
                }
                SDL_UnlockTexture(texture);
            }
        }

        sim_refresh_info_t info;
        sim_panel_info(&info);
        const int sleep_state = sim_sleep_state();
        if (info.count != shown_count || sleep_state != shown_sleep || sim_realtime() != shown_realtime
            || sim_sd_present() != shown_card) {
            shown_count = info.count;
            shown_sleep = sleep_state;
            shown_realtime = sim_realtime();
            shown_card = sim_sd_present();
            char title[600];
            snprintf(
                title, sizeof(title), "小纸 Pico 模拟器 — %s%s%s%s %d相 约%dms · 第%d次刷新 · %s%s",
                status, status[0] ? " · " : "", sleep_state ? "休眠中(按电源键唤醒) · " : "",
                info.mode[0] ? info.mode : "-", info.phases, info.est_ms, info.count,
                shown_realtime ? "真实耗时" : "瞬间刷新", shown_card ? "" : " · 无卡"
            );
            SDL_SetWindowTitle(window, title);
        }

        SDL_SetRenderDrawColor(renderer, 233, 236, 239, 255);
        SDL_RenderClear(renderer);
        // 先把固件画面贴到模型的屏幕位置，再盖上外壳：外壳在屏幕窗口处透明，窗口的圆角由模型决定。
        // 画面切成小格、每格的角各自投影，这样贴图不依赖"屏幕是个矩形"的假设。
        // First map the firmware image onto the model's screen, then lay the shell on top: it is
        // transparent over the screen window, so the rounded corners come from the model. The image is
        // cut into a grid with each cell corner projected on its own, so the mapping does not assume
        // the screen is a rectangle.
        float quad[4][2], shade;
        const bool facing = sim_shell_screen_quad(quad, &shade);
        if (facing) {
            enum { GRID_U = 12, GRID_V = 20 };
            static SDL_Vertex verts[(GRID_U + 1) * (GRID_V + 1)];
            static int indices[GRID_U * GRID_V * 6];
            const Uint8 c = (Uint8)(255.0f * shade);
            for (int j = 0; j <= GRID_V; j++) {
                for (int i = 0; i <= GRID_U; i++) {
                    SDL_Vertex* vert = &verts[j * (GRID_U + 1) + i];
                    const float u = (float)i / GRID_U, v = (float)j / GRID_V;
                    sim_shell_screen_to_canvas(u, v, &vert->position.x, &vert->position.y);
                    vert->color = (SDL_Color){ c, c, c, 255 };
                    vert->tex_coord.x = u;
                    vert->tex_coord.y = v;
                }
            }
            int n = 0;
            for (int j = 0; j < GRID_V; j++) {
                for (int i = 0; i < GRID_U; i++) {
                    const int a = j * (GRID_U + 1) + i, b = a + 1, d = a + GRID_U + 1, e = d + 1;
                    indices[n++] = a;
                    indices[n++] = b;
                    indices[n++] = e;
                    indices[n++] = a;
                    indices[n++] = e;
                    indices[n++] = d;
                }
            }
            SDL_RenderGeometry(renderer, texture, verts, (GRID_U + 1) * (GRID_V + 1), indices, n);
        }
        if (shell != NULL) SDL_RenderCopy(renderer, shell, NULL, NULL);
        // 按住触摸键时，在标记处压一层暗色作反馈。/ While a touch key is held, darken its mark as feedback.
        if (facing && held_key >= 0) {
            float kx, ky;
            sim_shell_key_canvas(held_key, &kx, &ky);
            SDL_Rect glow = { (int)kx - 44, (int)ky - 22, 88, 44 };
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 44);
            SDL_RenderFillRect(renderer, &glow);
        }
        if ((Sint32)(power_flash_until - SDL_GetTicks()) > 0) {
            float px, py;
            sim_shell_power_canvas(&px, &py);
            SDL_Rect glow = { (int)px - 10, (int)py - 70, 20, 140 };
            SDL_SetRenderDrawColor(renderer, 255, 190, 60, 120);
            SDL_RenderFillRect(renderer, &glow);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    sim_request_quit();
    SDL_DestroyTexture(texture);
    if (shell != NULL) SDL_DestroyTexture(shell);
    free(shell_pixels);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    fflush(NULL);
    _exit(0);
}
