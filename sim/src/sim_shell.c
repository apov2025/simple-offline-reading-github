/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 真机外壳的软件渲染器。模型是 apov catalog 里小纸 Pico 页面用的那个（由小纸的
 * STEP 文件制作），经 sim/tools/glb_to_mesh.py 转成 sim/assets/read_pico.mesh。
 * 正交投影、Z 缓冲、逐像素着色，全部在 CPU 上完成，不依赖显卡接口。
 *
 * Software renderer for the device shell. The model is the one used by the
 * Read Pico page of the apov catalog (built from the vendor STEP file),
 * converted by sim/tools/glb_to_mesh.py. Orthographic projection, z-buffer and
 * per-pixel shading, all on the CPU with no GPU API.
 *
 * 投影 / Projection: 透视投影，相机在机身正前方 CAM_DIST 毫米处，相当于 catalog 查看器
 * 的 32° 视角。正视时屏幕所在的平面按 1:1 落在画布上，所以屏幕仍是 684 px 宽。
 * Perspective, with the camera CAM_DIST mm in front of the body, matching the
 * catalog viewer's 32° field of view. Head-on, the plane of the screen maps 1:1
 * onto the canvas, so the screen is still 684 px wide.
 *
 * 坐标 / Coordinates: 模型单位毫米，x 向右、y 向上、z 朝向观察者（正面是 +z）。
 * Model units are millimeters; x right, y up, z toward the viewer (front is +z).
 */
#include "sim_shell.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CAM_DIST 230.0f

/* ---- 网格 / mesh ---- */

typedef struct { float x, y, z, nx, ny, nz; } vert_t;
typedef struct { uint32_t a, b, c, m; } tri_t;
typedef struct { char name[16]; float rgb[3]; } mesh_mat_t;

extern const uint8_t s_mesh_start[] asm("_binary_read_pico_mesh_start");

static const mesh_mat_t* s_mesh_mats;
static const vert_t* s_verts;
static const tri_t* s_tris;
static uint32_t s_mat_n, s_vert_n, s_tri_n;

typedef enum { SURF_MATTE, SURF_GLOSS, SURF_METAL } surface_t;
typedef struct {
    float r, g, b;
    surface_t surface;
    float sheen;  ///< 高光强度 / Specular strength
} material_t;

#define MAT_MAX 32
static material_t s_mats[MAT_MAX];
static int s_mat_case = -1, s_mat_front = -1, s_mat_back = -1, s_mat_buttons = -1, s_mat_screen = -1;
static int s_mat_touch[3] = { -1, -1, -1 };

// 屏幕窗口和各按键在模型里的范围，开机时从网格量出来。
// Extents of the screen window and keys in model space, measured from the mesh at init.
static float s_scr_x0, s_scr_x1, s_scr_y0, s_scr_y1, s_scr_z;
static float s_key_x[3], s_key_y, s_front_z;
static float s_pwr_x0, s_pwr_x1, s_pwr_y0, s_pwr_y1;
static float s_px_per_mm;

static float s_yaw, s_pitch;
static float s_rot[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

static int mat_index(const char* name) {
    for (uint32_t i = 0; i < s_mat_n; i++) {
        if (strncmp(s_mesh_mats[i].name, name, 16) == 0) return (int)i;
    }
    return -1;
}

static void mat_bounds(int mat, float lo[3], float hi[3]) {
    lo[0] = lo[1] = lo[2] = 1e9f;
    hi[0] = hi[1] = hi[2] = -1e9f;
    for (uint32_t t = 0; t < s_tri_n; t++) {
        if ((int)s_tris[t].m != mat) continue;
        const uint32_t idx[3] = { s_tris[t].a, s_tris[t].b, s_tris[t].c };
        for (int k = 0; k < 3; k++) {
            const float p[3] = { s_verts[idx[k]].x, s_verts[idx[k]].y, s_verts[idx[k]].z };
            for (int a = 0; a < 3; a++) {
                if (p[a] < lo[a]) lo[a] = p[a];
                if (p[a] > hi[a]) hi[a] = p[a];
            }
        }
    }
}

void sim_shell_init(void) {
    const uint32_t* head = (const uint32_t*)(const void*)s_mesh_start;
    s_mat_n = head[1];
    s_vert_n = head[2];
    s_tri_n = head[3];
    if (s_mat_n > MAT_MAX) s_mat_n = MAT_MAX;
    s_mesh_mats = (const mesh_mat_t*)(const void*)(s_mesh_start + 16);
    s_verts = (const vert_t*)(const void*)(s_mesh_start + 16 + sizeof(mesh_mat_t) * head[1]);
    s_tris = (const tri_t*)(const void*)((const uint8_t*)s_verts + sizeof(vert_t) * s_vert_n);

    s_mat_case = mat_index("Case");
    s_mat_front = mat_index("Front");
    s_mat_back = mat_index("Back");
    s_mat_buttons = mat_index("Buttons");
    s_mat_screen = mat_index("Screen");
    s_mat_touch[0] = mat_index("TouchLeft");
    s_mat_touch[1] = mat_index("TouchCenter");
    s_mat_touch[2] = mat_index("TouchRight");

    float lo[3], hi[3];
    mat_bounds(s_mat_screen, lo, hi);
    s_scr_x0 = lo[0];
    s_scr_x1 = hi[0];
    s_scr_y0 = lo[1];
    s_scr_y1 = hi[1];
    s_scr_z = hi[2];
    s_px_per_mm = 684.0f / (s_scr_x1 - s_scr_x0);
    mat_bounds(s_mat_front, lo, hi);
    s_front_z = hi[2];
    for (int k = 0; k < 3; k++) {
        mat_bounds(s_mat_touch[k], lo, hi);
        s_key_x[k] = (lo[0] + hi[0]) * 0.5f;
        s_key_y = (lo[1] + hi[1]) * 0.5f;
    }
    mat_bounds(s_mat_buttons, lo, hi);
    s_pwr_x0 = lo[0];
    s_pwr_x1 = hi[0];
    s_pwr_y0 = lo[1];
    s_pwr_y1 = hi[1];

    sim_shell_set_look(0);
    sim_shell_set_view(0, 0);
}

/* ---- 视角 / view ---- */

void sim_shell_set_view(float yaw, float pitch) {
    const float limit = 1.45f;
    if (pitch > limit) pitch = limit;
    if (pitch < -limit) pitch = -limit;
    s_yaw = yaw;
    s_pitch = pitch;
    const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    // R = Rx(pitch) · Ry(yaw)
    s_rot[0][0] = cy;       s_rot[0][1] = 0;   s_rot[0][2] = sy;
    s_rot[1][0] = sp * sy;  s_rot[1][1] = cp;  s_rot[1][2] = -sp * cy;
    s_rot[2][0] = -cp * sy; s_rot[2][1] = sp;  s_rot[2][2] = cp * cy;
}

void sim_shell_get_view(float* yaw, float* pitch) {
    *yaw = s_yaw;
    *pitch = s_pitch;
}

static void to_view(const float m[3], float v[3]) {
    for (int i = 0; i < 3; i++) v[i] = s_rot[i][0] * m[0] + s_rot[i][1] * m[1] + s_rot[i][2] * m[2];
}

// 近大远小的倍数：离相机越近越大，屏幕平面（正视时）正好是 1。
// Perspective factor: larger when nearer the camera; exactly 1 on the screen plane when head-on.
static float persp(float view_z) { return (CAM_DIST - s_scr_z) / (CAM_DIST - view_z); }

static void to_canvas(const float m[3], float* x, float* y) {
    float v[3];
    to_view(m, v);
    const float k = s_px_per_mm * persp(v[2]);
    *x = SHELL_W * 0.5f + v[0] * k;
    *y = SHELL_H * 0.5f - v[1] * k;
}

// 从相机穿过画布上一点的视线，打到模型的 z = plane_z 平面，返回模型坐标。
// Cast the ray from the camera through a canvas point onto the model plane z = plane_z.
static bool canvas_to_plane(float x, float y, float plane_z, float m[3]) {
    const float focal = s_px_per_mm * (CAM_DIST - s_scr_z);
    const float dir[3] = { (x - SHELL_W * 0.5f) / focal, -(y - SHELL_H * 0.5f) / focal, -1.0f };
    // 相机位置和视线方向转回模型空间：m = Rᵀ v。/ Camera position and ray direction back in model space: m = Rᵀ v.
    float eye_m[3], dir_m[3];
    for (int i = 0; i < 3; i++) {
        eye_m[i] = s_rot[2][i] * CAM_DIST;
        dir_m[i] = s_rot[0][i] * dir[0] + s_rot[1][i] * dir[1] + s_rot[2][i] * dir[2];
    }
    // 视线要从正面一侧射向机身；几乎贴着表面或从背面看都不算。
    // The ray must come at the body from its front side; grazing or from behind does not count.
    if (dir_m[2] > -0.12f) return false;
    const float t = (plane_z - eye_m[2]) / dir_m[2];
    if (t <= 0.0f) return false;
    for (int i = 0; i < 3; i++) m[i] = eye_m[i] + t * dir_m[i];
    return true;
}

static bool screen_faces_camera(void) {
    const float center[3] = { (s_scr_x0 + s_scr_x1) * 0.5f, (s_scr_y0 + s_scr_y1) * 0.5f, s_scr_z };
    float v[3];
    to_view(center, v);
    // 屏幕法线（R 的第三列）与"屏幕中心指向相机"的夹角小于 90° 才看得见。
    // Visible when the screen normal (third column of R) points toward the camera from the screen center.
    return s_rot[0][2] * (0.0f - v[0]) + s_rot[1][2] * (0.0f - v[1]) + s_rot[2][2] * (CAM_DIST - v[2]) > 8.0f;
}

void sim_shell_screen_to_canvas(float u, float v, float* x, float* y) {
    const float m[3] = { s_scr_x0 + (s_scr_x1 - s_scr_x0) * u, s_scr_y1 - (s_scr_y1 - s_scr_y0) * v, s_scr_z };
    to_canvas(m, x, y);
}

bool sim_shell_screen_quad(float xy[4][2], float* shade) {
    if (!screen_faces_camera()) return false;
    const float corners[4][3] = {
        { s_scr_x0, s_scr_y1, s_scr_z }, { s_scr_x1, s_scr_y1, s_scr_z },
        { s_scr_x1, s_scr_y0, s_scr_z }, { s_scr_x0, s_scr_y0, s_scr_z },
    };
    for (int i = 0; i < 4; i++) to_canvas(corners[i], &xy[i][0], &xy[i][1]);
    // 屏幕转离光源时略微变暗，和外壳的明暗一致。/ The screen dims slightly as it turns from the light, matching the shell.
    const float nl = s_rot[0][2] * -0.39f + s_rot[1][2] * 0.58f + s_rot[2][2] * 0.71f;
    float s = 0.80f + 0.28f * (nl > 0 ? nl : 0);
    *shade = s > 1.0f ? 1.0f : s;
    return true;
}

bool sim_shell_to_screen(float x, float y, float* sx, float* sy) {
    float m[3];
    if (!canvas_to_plane(x, y, s_scr_z, m)) return false;
    *sx = (m[0] - s_scr_x0) / (s_scr_x1 - s_scr_x0) * 684.0f;
    *sy = (s_scr_y1 - m[1]) / (s_scr_y1 - s_scr_y0) * 1216.0f;
    return true;
}

sim_shell_hit_t sim_shell_hit(float x, float y) {
    float m[3];
    // 电源键在侧面：把点击投到键帽所在的竖直面附近，范围放宽一些好点中。
    // The power key is on the side: project onto the front plane and accept a generous band around the key.
    if (canvas_to_plane(x, y, 0.0f, m) && m[0] >= s_pwr_x0 - 1.6f && m[0] <= s_pwr_x1 + 2.2f
        && m[1] >= s_pwr_y0 - 1.5f && m[1] <= s_pwr_y1 + 1.5f) {
        return SHELL_HIT_POWER;
    }
    if (!canvas_to_plane(x, y, s_front_z, m)) return SHELL_HIT_NONE;
    if (m[0] >= s_scr_x0 && m[0] <= s_scr_x1 && m[1] >= s_scr_y0 && m[1] <= s_scr_y1) return SHELL_HIT_SCREEN;
    // 屏幕下方的下巴：离哪个标记近就算哪个键，和真机一样不用点得很准。
    // The chin below the screen: the nearest mark wins; like the device, a rough tap is enough.
    if (m[1] < s_scr_y0 - 0.6f && m[1] > s_key_y - 5.2f && fabsf(m[0]) < s_scr_x1 + 4.0f) {
        int best = 0;
        for (int k = 1; k < 3; k++) {
            if (fabsf(m[0] - s_key_x[k]) < fabsf(m[0] - s_key_x[best])) best = k;
        }
        return (sim_shell_hit_t)(SHELL_HIT_KEY1 + best);
    }
    return SHELL_HIT_NONE;
}

void sim_shell_key_canvas(int key, float* x, float* y) {
    const float m[3] = { s_key_x[key], s_key_y, s_front_z };
    to_canvas(m, x, y);
}

void sim_shell_power_canvas(float* x, float* y) {
    const float m[3] = { s_pwr_x1, (s_pwr_y0 + s_pwr_y1) * 0.5f, 0.0f };
    to_canvas(m, x, y);
}

/* ---- 配色，算法与 catalog 页面的"随机搭配"一致 / colorway, same algorithm as the catalog's Randomize ---- */

static void hsl_to_rgb(float hue, float sat, float light, float rgb[3]) {
    float h = fmodf(hue, 360.0f);
    if (h < 0) h += 360.0f;
    const float amp = sat * (light < 1.0f - light ? light : 1.0f - light);
    const int offsets[3] = { 0, 8, 4 };
    for (int i = 0; i < 3; i++) {
        const float k = fmodf((float)offsets[i] + h / 30.0f, 12.0f);
        float t = k - 3.0f < 9.0f - k ? k - 3.0f : 9.0f - k;
        if (t > 1.0f) t = 1.0f;
        if (t < -1.0f) t = -1.0f;
        rgb[i] = light - amp * t;
    }
}

static float luminance(const float rgb[3]) {
    float lin[3];
    for (int i = 0; i < 3; i++) {
        lin[i] = rgb[i] <= 0.04045f ? rgb[i] / 12.92f : powf((rgb[i] + 0.055f) / 1.055f, 2.4f);
    }
    return lin[0] * 0.2126f + lin[1] * 0.7152f + lin[2] * 0.0722f;
}

static float contrast(const float a[3], const float b[3]) {
    const float x = luminance(a), y = luminance(b);
    return ((x > y ? x : y) + 0.05f) / ((x < y ? x : y) + 0.05f);
}

static void set_color(int mat, const float rgb[3]) {
    if (mat < 0) return;
    s_mats[mat].r = rgb[0];
    s_mats[mat].g = rgb[1];
    s_mats[mat].b = rgb[2];
}

static void hex_color(int mat, uint32_t hex) {
    const float rgb[3] = { (float)(hex >> 16 & 255) / 255.0f, (float)(hex >> 8 & 255) / 255.0f, (float)(hex & 255) / 255.0f };
    set_color(mat, rgb);
}

void sim_shell_set_look(unsigned seed) {
    for (uint32_t i = 0; i < s_mat_n; i++) {
        s_mats[i] = (material_t){ s_mesh_mats[i].rgb[0], s_mesh_mats[i].rgb[1], s_mesh_mats[i].rgb[2], SURF_MATTE, 0.0f };
        if (strncmp(s_mesh_mats[i].name, "ConnectorMetal", 16) == 0 || strncmp(s_mesh_mats[i].name, "Contacts", 16) == 0) {
            s_mats[i].surface = SURF_METAL;
        }
    }
    // 原机：细磨砂铝中框，前后都是白色玻璃，深灰侧键，浅灰触摸标记。
    // Stock: fine matte aluminum frame, white glass front and back, dark gray side key, light gray touch marks.
    hex_color(s_mat_case, 0xa6a6a3);
    hex_color(s_mat_front, 0xededeb);
    hex_color(s_mat_back, 0xededeb);
    hex_color(s_mat_buttons, 0x41454c);
    hex_color(s_mat_screen, 0xe4e2d8);
    for (int k = 0; k < 3; k++) hex_color(s_mat_touch[k], 0xa3acab);
    if (s_mat_case >= 0) s_mats[s_mat_case].surface = SURF_METAL;
    if (s_mat_front >= 0) s_mats[s_mat_front] = (material_t){ s_mats[s_mat_front].r, s_mats[s_mat_front].g, s_mats[s_mat_front].b, SURF_GLOSS, 0.20f };
    if (s_mat_back >= 0) s_mats[s_mat_back] = (material_t){ s_mats[s_mat_back].r, s_mats[s_mat_back].g, s_mats[s_mat_back].b, SURF_GLOSS, 0.45f };
    if (s_mat_buttons >= 0) s_mats[s_mat_buttons].surface = SURF_GLOSS, s_mats[s_mat_buttons].sheen = 0.12f;
    if (seed == 0) return;

    srand(seed);
#define RND() ((float)rand() / ((float)RAND_MAX + 1.0f))
#define BETWEEN(lo, hi) ((lo) + RND() * ((hi) - (lo)))
    const bool glossy = RND() < 0.5f;
    const int harmony = (int)(RND() * 5.0f);
    const float hue = RND() * 360.0f;
    const float dir = RND() < 0.5f ? -1.0f : 1.0f;
    const float offsets[5][2] = {
        { 0, 0 }, { 24 * dir, -30 * dir }, { 0, 180 }, { 0, 150 * dir }, { 120 * dir, -120 * dir },
    };
    const bool dark_front = RND() < 0.65f;
    const float case_sat = BETWEEN(0.22f, 0.48f);
    const float case_light = dark_front ? BETWEEN(0.42f, 0.78f) : BETWEEN(0.18f, 0.48f);
    const float front_sat = BETWEEN(0.18f, 0.36f);
    const float front_light = dark_front ? BETWEEN(0.15f, 0.25f) : BETWEEN(0.8f, 0.9f);
    const float button_sat = BETWEEN(0.4f, 0.68f);
    float button_light = dark_front ? BETWEEN(0.62f, 0.76f) : BETWEEN(0.27f, 0.4f);
#undef BETWEEN
#undef RND
    float case_rgb[3], front_rgb[3], button_rgb[3];
    hsl_to_rgb(hue, case_sat, case_light, case_rgb);
    hsl_to_rgb(hue + offsets[harmony][0], front_sat, front_light, front_rgb);
    hsl_to_rgb(hue + offsets[harmony][1], button_sat, button_light, button_rgb);
    // 触摸标记画在前面板上，对比度不够就把按键色往更亮或更暗推。
    // Touch marks sit on the front panel; push the button color lighter or darker until it contrasts.
    while (contrast(front_rgb, button_rgb) < 3.0f && button_light > 0.08f && button_light < 0.92f) {
        button_light += dark_front ? 0.04f : -0.04f;
        if (button_light < 0.08f) button_light = 0.08f;
        if (button_light > 0.92f) button_light = 0.92f;
        hsl_to_rgb(hue + offsets[harmony][1], button_sat, button_light, button_rgb);
    }
    set_color(s_mat_case, case_rgb);
    set_color(s_mat_back, case_rgb);
    set_color(s_mat_front, front_rgb);
    set_color(s_mat_buttons, button_rgb);
    for (int k = 0; k < 3; k++) set_color(s_mat_touch[k], button_rgb);
    if (s_mat_case >= 0) {
        s_mats[s_mat_case].surface = SURF_GLOSS;
        s_mats[s_mat_case].sheen = glossy ? 0.40f : 0.05f;
    }
    if (s_mat_back >= 0) s_mats[s_mat_back].sheen = glossy ? 0.40f : 0.05f;
}

/* ---- 渲染 / rendering ---- */

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// 光从左上前方来。n 是观察空间里的单位法线，观察方向是 +z。
// Key light from the upper left front. n is the unit normal in view space; the viewer looks along +z.
static void shade(const material_t* mat, float nx, float ny, float nz, float rgb[3]) {
    const float lx = -0.39f, ly = 0.58f, lz = 0.71f;
    const float nl = clamp01(nx * lx + ny * ly + nz * lz);
    float k, spec = 0.0f;
    if (mat->surface == SURF_METAL) {
        // 金属主要反射环境：上方亮、下方暗的摄影棚，加一块柔光箱的高光。
        // Metal mostly mirrors its surroundings: a studio bright above and dim below, plus a softbox highlight.
        const float rx = 2.0f * nz * nx, ry = 2.0f * nz * ny, rz = 2.0f * nz * nz - 1.0f;
        const float sky = clamp01(ry * 0.5f + 0.5f);
        const float box = clamp01(rx * lx + ry * ly + rz * lz);
        k = 0.30f + 0.82f * (0.5f + 0.5f * sky * sky * (3.0f - 2.0f * sky)) + 0.35f * powf(box, 8.0f);
        k *= 0.62f + 0.38f * nz;
    } else {
        k = 0.80f + 0.28f * nl;
        if (mat->sheen > 0.0f) {
            // Blinn-Phong 高光，半程向量取光线与视线的中间方向。/ Blinn-Phong highlight with the light/view half vector.
            const float hx = lx, hy = ly, hz = lz + 1.0f;
            const float hl = sqrtf(hx * hx + hy * hy + hz * hz);
            const float nh = clamp01((nx * hx + ny * hy + nz * hz) / hl);
            spec = mat->sheen * powf(nh, 90.0f);
            // 掠射角的菲涅耳反光，让玻璃边缘亮一点。/ Fresnel-like rim so glass brightens toward grazing angles.
            spec += mat->sheen * 0.35f * powf(1.0f - clamp01(nz), 3.0f);
        }
    }
    rgb[0] = clamp01(mat->r * k + spec);
    rgb[1] = clamp01(mat->g * k + spec);
    rgb[2] = clamp01(mat->b * k + spec);
}

static float* s_zbuf;
static uint32_t* s_color;
static float* s_alpha;
static float* s_blur;
static float* s_tmp;
static float* s_screen;
static size_t s_big_cap, s_small_cap, s_screen_cap;
static float* s_tv;  // 每顶点：屏幕 x、y、观察空间 z、法线 / per vertex: screen x, y, view z, normal

static void box_blur(float* buf, float* tmp, int w, int h, int radius) {
    if (radius < 1) return;
    const float inv = 1.0f / (float)(2 * radius + 1);
    for (int y = 0; y < h; y++) {
        const float* src = buf + (size_t)y * w;
        float* dst = tmp + (size_t)y * w;
        float sum = 0;
        for (int x = -radius; x <= radius; x++) sum += src[x < 0 ? 0 : (x >= w ? w - 1 : x)];
        for (int x = 0; x < w; x++) {
            dst[x] = sum * inv;
            const int add = x + radius + 1, sub = x - radius;
            sum += src[add >= w ? w - 1 : add] - src[sub < 0 ? 0 : sub];
        }
    }
    for (int x = 0; x < w; x++) {
        float sum = 0;
        for (int y = -radius; y <= radius; y++) sum += tmp[(size_t)(y < 0 ? 0 : (y >= h ? h - 1 : y)) * w + x];
        for (int y = 0; y < h; y++) {
            buf[(size_t)y * w + x] = sum * inv;
            const int add = y + radius + 1, sub = y - radius;
            sum += tmp[(size_t)(add >= h ? h - 1 : add) * w + x] - tmp[(size_t)(sub < 0 ? 0 : sub) * w + x];
        }
    }
}

void sim_shell_render(uint32_t* out, int out_w, int out_h, int quality) {
    if (quality < 1) quality = 1;
    const int w = out_w * quality, h = out_h * quality;
    const size_t big = (size_t)w * (size_t)h, small = (size_t)out_w * (size_t)out_h;
    if (big > s_big_cap) {
        s_zbuf = realloc(s_zbuf, big * sizeof(float));
        s_color = realloc(s_color, big * sizeof(uint32_t));
        s_big_cap = big;
    }
    if (small > s_small_cap) {
        s_alpha = realloc(s_alpha, small * sizeof(float));
        s_blur = realloc(s_blur, small * sizeof(float));
        s_tmp = realloc(s_tmp, small * sizeof(float));
        s_small_cap = small;
    }
    if (s_tv == NULL) s_tv = malloc(sizeof(float) * 6 * s_vert_n);
    if (s_zbuf == NULL || s_color == NULL || s_alpha == NULL || s_blur == NULL || s_tmp == NULL || s_tv == NULL) return;

    for (size_t i = 0; i < big; i++) s_zbuf[i] = -1e9f;

    // 画布等比铺进输出，四周留白居中。/ Fit the canvas into the output, centered.
    const float fit_x = (float)w / SHELL_W, fit_y = (float)h / SHELL_H;
    const float fit = fit_x < fit_y ? fit_x : fit_y;
    const float scale = s_px_per_mm * fit;
    const float cx = (float)w * 0.5f, cy = (float)h * 0.5f;
    for (uint32_t i = 0; i < s_vert_n; i++) {
        const float m[3] = { s_verts[i].x, s_verts[i].y, s_verts[i].z };
        const float n[3] = { s_verts[i].nx, s_verts[i].ny, s_verts[i].nz };
        float v[3], vn[3];
        to_view(m, v);
        to_view(n, vn);
        float* t = s_tv + (size_t)i * 6;
        // 深度缓冲存"距离的倒数"：它在屏幕空间里是线性的，透视下按重心坐标插值才正确。
        // The depth buffer stores inverse distance: it is linear in screen space, so barycentric interpolation stays exact under perspective.
        const float k = scale * persp(v[2]);
        t[0] = cx + v[0] * k;
        t[1] = cy - v[1] * k;
        t[2] = 1.0f / (CAM_DIST - v[2]);
        t[3] = vn[0];
        t[4] = vn[1];
        t[5] = vn[2];
    }

    for (uint32_t ti = 0; ti < s_tri_n; ti++) {
        const tri_t* tri = &s_tris[ti];
        const float* a = s_tv + (size_t)tri->a * 6;
        const float* b = s_tv + (size_t)tri->b * 6;
        const float* c = s_tv + (size_t)tri->c * 6;
        // 屏幕 y 向下，所以朝向观察者的面在这里面积为负。/ Screen y points down, so viewer-facing triangles have negative area here.
        const float area = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
        if (area > -1e-7f) continue;
        float fx0 = a[0], fx1 = a[0], fy0 = a[1], fy1 = a[1];
        if (b[0] < fx0) fx0 = b[0];
        if (c[0] < fx0) fx0 = c[0];
        if (b[0] > fx1) fx1 = b[0];
        if (c[0] > fx1) fx1 = c[0];
        if (b[1] < fy0) fy0 = b[1];
        if (c[1] < fy0) fy0 = c[1];
        if (b[1] > fy1) fy1 = b[1];
        if (c[1] > fy1) fy1 = c[1];
        int x0 = (int)floorf(fx0), x1 = (int)ceilf(fx1), y0 = (int)floorf(fy0), y1 = (int)ceilf(fy1);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w - 1) x1 = w - 1;
        if (y1 > h - 1) y1 = h - 1;
        if (x0 > x1 || y0 > y1) continue;
        const float inv = 1.0f / area;
        const material_t* mat = &s_mats[tri->m < MAT_MAX ? tri->m : 0];
        for (int y = y0; y <= y1; y++) {
            const float py = (float)y + 0.5f;
            for (int x = x0; x <= x1; x++) {
                const float px = (float)x + 0.5f;
                const float wa = ((c[0] - b[0]) * (py - b[1]) - (c[1] - b[1]) * (px - b[0])) * inv;
                const float wb = ((a[0] - c[0]) * (py - c[1]) - (a[1] - c[1]) * (px - c[0])) * inv;
                const float wc = 1.0f - wa - wb;
                if (wa < 0 || wb < 0 || wc < 0) continue;
                const float z = wa * a[2] + wb * b[2] + wc * c[2];
                const size_t at = (size_t)y * w + (size_t)x;
                if (z <= s_zbuf[at]) continue;
                s_zbuf[at] = z;
                float nx = wa * a[3] + wb * b[3] + wc * c[3];
                float ny = wa * a[4] + wb * b[4] + wc * c[4];
                float nz = wa * a[5] + wb * b[5] + wc * c[5];
                const float len = sqrtf(nx * nx + ny * ny + nz * nz);
                if (len > 1e-6f) {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }
                // 屏幕窗口不上色，只留下"这里是屏幕"的标记：外壳在这里开孔，露出下面的固件画面。
                // The screen window is not colored, only flagged: the shell is cut out here to show the firmware image below.
                if ((int)tri->m == s_mat_screen) {
                    s_color[at] = 0;
                    continue;
                }
                float rgb[3];
                shade(mat, nx, ny, nz, rgb);
                s_color[at] = 0xFF000000u | (uint32_t)(rgb[0] * 255.0f + 0.5f) << 16
                    | (uint32_t)(rgb[1] * 255.0f + 0.5f) << 8 | (uint32_t)(rgb[2] * 255.0f + 0.5f);
            }
        }
    }

    // 缩回输出分辨率。每个输出像素记三样：机身（不含屏幕）的平均颜色与占比、屏幕窗口的占比。
    // Downsample. Per output pixel: mean color and share of the body (screen excluded), and the share of the screen window.
    const float q2 = 1.0f / (float)(quality * quality);
    for (int y = 0; y < out_h; y++) {
        for (int x = 0; x < out_w; x++) {
            float r = 0, g = 0, b = 0, body = 0, screen = 0;
            for (int j = 0; j < quality; j++) {
                const size_t row = (size_t)(y * quality + j) * w + (size_t)x * quality;
                for (int i = 0; i < quality; i++) {
                    if (s_zbuf[row + i] <= -1e8f) continue;
                    const uint32_t c = s_color[row + i];
                    if ((c >> 24) == 0) {
                        screen += 1.0f;
                        continue;
                    }
                    r += (float)(c >> 16 & 255);
                    g += (float)(c >> 8 & 255);
                    b += (float)(c & 255);
                    body += 1.0f;
                }
            }
            const size_t at = (size_t)y * out_w + x;
            s_alpha[at] = body * q2;
            s_tmp[at] = screen * q2;
            s_blur[at] = (body + screen) * q2;
            const float n = body > 0 ? 1.0f / body : 0.0f;
            s_color[at] = (uint32_t)(r * n + 0.5f) << 16 | (uint32_t)(g * n + 0.5f) << 8 | (uint32_t)(b * n + 0.5f);
        }
    }
    // 屏幕占比先挪到单独的缓冲里，下面的模糊会借用 s_tmp。/ Keep the screen share aside; the blur below reuses s_tmp.
    if (small > s_screen_cap) {
        s_screen = realloc(s_screen, small * sizeof(float));
        s_screen_cap = small;
    }
    if (s_screen == NULL) return;
    memcpy(s_screen, s_tmp, small * sizeof(float));

    const float out_fit = fit / (float)quality;
    box_blur(s_blur, s_tmp, out_w, out_h, (int)(15.0f * out_fit));
    box_blur(s_blur, s_tmp, out_w, out_h, (int)(15.0f * out_fit));
    const int drop = (int)(16.0f * out_fit);

    // 背景取 catalog 查看器的浅灰（#edf0f2），向下略微加深。
    // Background is the catalog viewer's light gray (#edf0f2), deepening slightly toward the bottom.
    for (int y = 0; y < out_h; y++) {
        const float t = (float)y / (float)(out_h > 1 ? out_h - 1 : 1);
        const float bg[3] = { 240.0f - 14.0f * t, 243.0f - 14.0f * t, 245.0f - 13.0f * t };
        const int sy = y - drop < 0 ? 0 : y - drop;
        for (int x = 0; x < out_w; x++) {
            const size_t at = (size_t)y * out_w + x;
            const float a = s_alpha[at];
            const float dim = 1.0f - 0.34f * s_blur[(size_t)sy * out_w + x];
            const uint32_t c = s_color[at];
            // 不透明度 = 1 - 屏幕占比；颜色是机身与背景按各自占比混出来的（不含屏幕那一份）。
            // Opacity = 1 - screen share; color mixes body and background by their shares, the screen's share excluded.
            const float hole = s_screen[at];
            const float back = 1.0f - a - hole;
            const float solid = a + (back > 0 ? back : 0);
            const float n = solid > 1e-4f ? 1.0f / solid : 0.0f;
            const float wb = (back > 0 ? back : 0) * n, wa = a * n;
            const float r = bg[0] * dim * wb + (float)(c >> 16 & 255) * wa;
            const float g = bg[1] * dim * wb + (float)(c >> 8 & 255) * wa;
            const float b = bg[2] * dim * wb + (float)(c & 255) * wa;
            out[at] = (uint32_t)(clamp01(solid) * 255.0f + 0.5f) << 24 | (uint32_t)(r + 0.5f) << 16
                | (uint32_t)(g + 0.5f) << 8 | (uint32_t)(b + 0.5f);
        }
    }
}
