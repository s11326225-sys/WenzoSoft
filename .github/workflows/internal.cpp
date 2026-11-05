// Wenzo Soft (internal) - Oxide Survival Island, IL2CPP dump "arm64_1_17_12000".
// ESP + camera aim, neon purple ImGui menu (touch). Target-specific data is resolved at runtime by name and
// checked against the dump offsets below; a mismatch disables the affected feature.
// STATUS: not compiled, not run. Aim field roles (waV/waI) are INFERRED -> validate with "Log look quats".
#include <android/log.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#define TAG "nd"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

extern "C" int DobbyHook(void* address, void* replace, void** origin);

// ---- values from Dump_arm64_1_17_12000 (dump.cs) -----------------------------------------
static constexpr uint32_t OFF_PM_WORLDCAMROOT = 0xA8;  // Oxide.PlayerManager.worldCameraRoot (Transform)
static constexpr uint32_t OFF_PM_MOUSELOOK    = 0xB0;  // Oxide.PlayerManager.mouseLook
static constexpr uint32_t OFF_ML_WAV          = 0x48;  // ECM.Components.MouseLook.waV Quaternion (INFERRED: yaw target)
static constexpr uint32_t OFF_ML_WAI          = 0x58;  // ECM.Components.MouseLook.waI Quaternion (INFERRED: pitch target)
static constexpr int      MAX_PLAYERS         = 256;
static constexpr uint32_t TOUCH_OFF_POS = 0x4, TOUCH_OFF_PHASE = 0x24;   // UnityEngine.Touch (InputLegacyModule)

// ---- settings (menu-controlled) ----------------------------------------------------------
struct Cfg {
    bool  esp = true, esp_box = true, esp_line = true, esp_dist = true;
    float col_box[3]  = {0.88f, 0.25f, 0.98f};
    float col_line[3] = {0.69f, 0.15f, 1.00f};
    bool  aim = false, aim_hold = true, show_fov = true, dump_look = false;
    float fov_px = 250.f, smooth = 0.25f;
} g_cfg;

// ---- il2cpp runtime API -------------------------------------------------------------------
struct V3 { float x,y,z; };
struct Q4 { float x,y,z,w; };
static void*  (*il2cpp_domain_get)();
static void*  (*il2cpp_thread_attach)(void*);
static void** (*il2cpp_domain_get_assemblies)(void*, size_t*);
static void*  (*il2cpp_assembly_get_image)(void*);
static const char* (*il2cpp_image_get_name)(void*);
static void*  (*il2cpp_class_from_name)(void*, const char*, const char*);
static void*  (*il2cpp_class_get_field_from_name)(void*, const char*);
static size_t (*il2cpp_field_get_offset)(void*);
static void   (*il2cpp_field_static_get_value)(void*, void*);
static void*  (*il2cpp_class_get_method_from_name)(void*, const char*, int);
static void*  (*il2cpp_resolve_icall)(const char*);

static void* find_image(const char* name) {
    size_t n = 0; void** a = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &n);
    for (size_t i = 0; i < n; i++) { void* im = il2cpp_assembly_get_image(a[i]);
        if (im && !strcmp(il2cpp_image_get_name(im), name)) return im; }
    return nullptr;
}
// icall by registered name first; fallback = methodPointer of the managed extern wrapper (extra trailing MethodInfo arg is ignored by the callee).
static void* get_fn(const char* icall, const char* image, const char* ns, const char* cls, const char* m, int argc) {
    if (void* p = il2cpp_resolve_icall(icall)) return p;
    void* im = find_image(image); void* k = im ? il2cpp_class_from_name(im, ns, cls) : nullptr;
    void* mi = k ? il2cpp_class_get_method_from_name(k, m, argc) : nullptr;
    return mi ? *(void**)mi : nullptr;
}

// ---- resolved target ----------------------------------------------------------------------
static void* g_fld_clientList; static void* g_isLocal_mi; static bool (*g_isLocal)(void*, void*);
static intptr_t (*ic_cam_main)();
static void     (*ic_w2s)(intptr_t, V3*, int, V3*);
static intptr_t (*ic_get_tr)(intptr_t);
static void     (*ic_tr_pos)(intptr_t, V3*);
static int      (*ic_touch_count)();
static void     (*ic_get_touch)(int, void*);
static bool g_ok_esp = false, g_ok_aim = false, g_ok_touch = false;

static bool resolve() {
    void* asm_cs = find_image("Assembly-CSharp.dll"); void* mirror = find_image("Mirror.dll");
    if (!asm_cs || !mirror) return false;
    void* pm = il2cpp_class_from_name(asm_cs, "Oxide", "PlayerManager");
    void* ml = il2cpp_class_from_name(asm_cs, "ECM.Components", "MouseLook");
    void* nb = il2cpp_class_from_name(mirror, "Mirror", "NetworkBehaviour");
    if (!pm || !nb) return false;
    g_fld_clientList = il2cpp_class_get_field_from_name(pm, "clientPlayerList");
    void* f_cam  = il2cpp_class_get_field_from_name(pm, "worldCameraRoot");
    void* f_look = il2cpp_class_get_field_from_name(pm, "mouseLook");
    g_isLocal_mi = il2cpp_class_get_method_from_name(nb, "get_isLocalPlayer", 0);
    if (g_isLocal_mi) g_isLocal = *(bool(**)(void*, void*))g_isLocal_mi;     // MethodInfo::methodPointer is the first member

    const char* CORE = "UnityEngine.CoreModule.dll";
    ic_cam_main = (decltype(ic_cam_main))get_fn("UnityEngine.Camera::get_main_Injected()", CORE, "UnityEngine", "Camera", "get_main_Injected", 0);
    ic_w2s      = (decltype(ic_w2s))get_fn("UnityEngine.Camera::WorldToScreenPoint_Injected(UnityEngine.Vector3&,UnityEngine.Camera/MonoOrStereoscopicEye,UnityEngine.Vector3&)", CORE, "UnityEngine", "Camera", "WorldToScreenPoint_Injected", 3);
    ic_get_tr   = (decltype(ic_get_tr))get_fn("UnityEngine.Component::get_transform_Injected()", CORE, "UnityEngine", "Component", "get_transform_Injected", 0);
    ic_tr_pos   = (decltype(ic_tr_pos))get_fn("UnityEngine.Transform::get_position_Injected(UnityEngine.Vector3&)", CORE, "UnityEngine", "Transform", "get_position_Injected", 1);
    const char* INP = "UnityEngine.InputLegacyModule.dll";
    ic_touch_count = (decltype(ic_touch_count))get_fn("UnityEngine.Input::get_touchCount()", INP, "UnityEngine", "Input", "get_touchCount", 0);
    ic_get_touch   = (decltype(ic_get_touch))get_fn("UnityEngine.Input::GetTouch_Injected(System.Int32,UnityEngine.Touch&)", INP, "UnityEngine", "Input", "GetTouch_Injected", 2);

    g_ok_esp = g_fld_clientList && f_cam && g_isLocal && ic_cam_main && ic_w2s && ic_get_tr && ic_tr_pos
               && il2cpp_field_get_offset(f_cam) == OFF_PM_WORLDCAMROOT;
    void* f_wav = ml ? il2cpp_class_get_field_from_name(ml, "waV") : nullptr;
    void* f_wai = ml ? il2cpp_class_get_field_from_name(ml, "waI") : nullptr;
    g_ok_aim = g_ok_esp && f_look && f_wav && f_wai && il2cpp_field_get_offset(f_look) == OFF_PM_MOUSELOOK
               && il2cpp_field_get_offset(f_wav) == OFF_ML_WAV && il2cpp_field_get_offset(f_wai) == OFF_ML_WAI;
    g_ok_touch = ic_touch_count && ic_get_touch;
    LOG("resolve: esp=%d aim=%d touch=%d (0 = name/offset mismatch -> build differs from the dump)", g_ok_esp, g_ok_aim, g_ok_touch);
    return g_ok_esp;
}

template <class T> static inline T rd(const void* base, size_t off) { T v; memcpy(&v, (const char*)base + off, sizeof(T)); return v; }
static inline bool plausible(uintptr_t p) { return p > 0x10000 && p < 0x0000FFFFFFFFFFFFull && (p & 7) == 0; }
static bool obj_pos(intptr_t tr, V3* out) { if (!plausible(tr)) return false; ic_tr_pos(tr, out); return true; }
static bool w2s(intptr_t cam, V3 w, float H, V3* s) {          // Unity: pixels, origin bottom-left, z>0 in front
    ic_w2s(cam, &w, 2 /*Mono eye*/, s);
    if (s->z <= 0.01f) return false;
    s->y = H - s->y;
    return std::isfinite(s->x) && std::isfinite(s->y);
}

// ---- touch input (legacy Input module; verify in Misc tab: "touches" must change when you touch) ----
struct Touches { ImVec2 p[10]; int n = 0; } g_touch;
static void read_touches(float H) {
    g_touch.n = 0; if (!g_ok_touch) return;
    int n = ic_touch_count(); if (n < 0 || n > 10) n = 0;
    for (int i = 0; i < n; i++) {
        alignas(8) char buf[128] = {};
        ic_get_touch(i, buf);
        int phase = rd<int>(buf, TOUCH_OFF_PHASE);                    // 0 Began 1 Moved 2 Stationary 3 Ended 4 Canceled
        if (phase == 3 || phase == 4) continue;
        g_touch.p[g_touch.n++] = ImVec2(rd<float>(buf, TOUCH_OFF_POS), H - rd<float>(buf, TOUCH_OFF_POS + 4));
    }
}

// ---- world logic (ESP via ImGui background draw list + aim) -------------------------------
struct Target { V3 head, feet, sHead, sFeet; float dist; };
static void norm180(float& a) { while (a > 180.f) a -= 360.f; while (a < -180.f) a += 360.f; }
static Q4 q_axis(float x, float y, float z, float deg) { float h = deg*0.00872664626f, s = sinf(h); return {x*s, y*s, z*s, cosf(h)}; }
static ImU32 col3(const float* c, float a = 1.f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], a)); }
static bool g_aim_held = false; static ImVec2 g_aim_btn_c; static float g_aim_btn_r;

static void world(float W, float H, int frameNo) {
    if (!g_ok_esp || (!g_cfg.esp && !g_cfg.aim)) return;
    intptr_t cam = ic_cam_main(); if (!plausible(cam)) return;
    V3 camPos; if (!obj_pos(ic_get_tr(cam), &camPos)) return;

    void* list = nullptr; il2cpp_field_static_get_value(g_fld_clientList, &list);
    if (!plausible((uintptr_t)list)) return;
    void* items = rd<void*>(list, 0x10); int size = rd<int>(list, 0x18);      // List<T>._items/_size
    if (!plausible((uintptr_t)items) || size <= 0 || size > MAX_PLAYERS) return;

    void* localPm = nullptr; std::vector<Target> ts;
    for (int i = 0; i < size; i++) {
        void* pm = rd<void*>(items, 0x20 + 8*i);                              // Il2CppArray data at +0x20
        if (!plausible((uintptr_t)pm)) continue;
        if (g_isLocal(pm, g_isLocal_mi)) { localPm = pm; continue; }
        Target t{};
        if (!obj_pos(rd<intptr_t>(pm, OFF_PM_WORLDCAMROOT), &t.head) || !obj_pos(ic_get_tr((intptr_t)pm), &t.feet)) continue;
        // FILL: alive check - current-health field is not readable in dump.cs (GenericVitals.m_MaxHealth=0x88 only; look in base class WxQ).
        if (!w2s(cam, t.head, H, &t.sHead) || !w2s(cam, t.feet, H, &t.sFeet)) continue;
        t.dist = sqrtf((t.head.x-camPos.x)*(t.head.x-camPos.x) + (t.head.y-camPos.y)*(t.head.y-camPos.y) + (t.head.z-camPos.z)*(t.head.z-camPos.z));
        ts.push_back(t);
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (g_cfg.esp) for (auto& t : ts) {
        float h = t.sFeet.y - t.sHead.y; if (h < 4.f || h > H*2) continue;
        float w = h * 0.45f, cx = t.sFeet.x, top = t.sHead.y - h*0.1f, bh = h*1.1f;
        float th = std::max(1.5f, H/540.f);
        if (g_cfg.esp_box) {
            dl->AddRect(ImVec2(cx-w/2-1, top-1), ImVec2(cx+w/2+1, top+bh+1), col3(g_cfg.col_box, 0.25f), 0, 0, th*2.5f);   // glow
            dl->AddRect(ImVec2(cx-w/2, top), ImVec2(cx+w/2, top+bh), col3(g_cfg.col_box), 0, 0, th);
        }
        if (g_cfg.esp_line) dl->AddLine(ImVec2(W/2, H), ImVec2(cx, t.sFeet.y), col3(g_cfg.col_line, 0.9f), th);
        if (g_cfg.esp_dist) { char b[16]; snprintf(b, sizeof b, "%dm", (int)t.dist); dl->AddText(ImVec2(cx - 10, t.sFeet.y + 2), col3(g_cfg.col_box), b); }
    }

    if (g_cfg.aim && g_cfg.show_fov)
        dl->AddCircle(ImVec2(W/2, H/2), g_cfg.fov_px, col3(g_cfg.col_box, 0.8f), 64, std::max(1.5f, H/540.f));

    if (g_cfg.aim && g_ok_aim && localPm && !ts.empty() && (!g_cfg.aim_hold || g_aim_held)) {
        void* ml = rd<void*>(localPm, OFF_PM_MOUSELOOK); if (!plausible((uintptr_t)ml)) return;
        Q4 qv = rd<Q4>(ml, OFF_ML_WAV), qi = rd<Q4>(ml, OFF_ML_WAI);
        if (g_cfg.dump_look && (frameNo % 30) == 0)
            LOG("look waV=(%.3f %.3f %.3f %.3f) waI=(%.3f %.3f %.3f %.3f)", qv.x,qv.y,qv.z,qv.w, qi.x,qi.y,qi.z,qi.w);
        const Target* best = nullptr; float bd = g_cfg.fov_px;
        for (auto& t : ts) { float d = hypotf(t.sHead.x - W/2, t.sHead.y - H/2); if (d < bd) { bd = d; best = &t; } }
        if (!best) return;
        V3 d{best->head.x-camPos.x, best->head.y-camPos.y, best->head.z-camPos.z};
        float len = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z); if (len < 0.01f) return;
        float yaw = atan2f(d.x, d.z) * 57.2957795f, pitch = -asinf(d.y/len) * 57.2957795f;   // Unity: +X rot looks down
        float curYaw = 2.f*atan2f(qv.y, qv.w) * 57.2957795f, curPitch = 2.f*atan2f(qi.x, qi.w) * 57.2957795f;
        float dy = yaw - curYaw, dp = pitch - curPitch; norm180(dy); norm180(dp);
        Q4 nv = q_axis(0,1,0, curYaw + dy*g_cfg.smooth), ni = q_axis(1,0,0, curPitch + dp*g_cfg.smooth);
        memcpy((char*)ml + OFF_ML_WAV, &nv, sizeof nv); memcpy((char*)ml + OFF_ML_WAI, &ni, sizeof ni);
    }
}

// ---- Wenzo Soft theme: dark violet + neon purple/magenta ----------------------------------
static const ImVec4 C_BG    (0.055f, 0.020f, 0.095f, 0.95f);
static const ImVec4 C_PANEL (0.140f, 0.060f, 0.230f, 1.00f);
static const ImVec4 C_PURPLE(0.690f, 0.150f, 1.000f, 1.00f);   // #B026FF
static const ImVec4 C_MAG   (0.930f, 0.250f, 0.980f, 1.00f);   // magenta
static const ImVec4 C_TEXT  (0.950f, 0.900f, 1.000f, 1.00f);
static ImVec4 A(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

static void apply_theme(float scale) {
    ImGuiStyle& s = ImGui::GetStyle(); ImVec4* c = s.Colors;
    s.WindowRounding = 14; s.FrameRounding = 9; s.GrabRounding = 9; s.TabRounding = 9; s.ChildRounding = 9; s.PopupRounding = 9;
    s.WindowPadding = ImVec2(12, 10); s.FramePadding = ImVec2(10, 8); s.ItemSpacing = ImVec2(10, 10);
    s.WindowBorderSize = 1.5f; s.FrameBorderSize = 0; s.GrabMinSize = 22; s.ScrollbarSize = 18; s.ScrollbarRounding = 9;
    c[ImGuiCol_Text] = C_TEXT;                         c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.45f, 0.65f, 1);
    c[ImGuiCol_WindowBg] = C_BG;                       c[ImGuiCol_ChildBg] = A(C_PANEL, 0.5f);
    c[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.03f, 0.13f, 0.98f);
    c[ImGuiCol_Border] = A(C_PURPLE, 0.85f);           c[ImGuiCol_BorderShadow] = ImVec4(0,0,0,0);
    c[ImGuiCol_FrameBg] = C_PANEL;                     c[ImGuiCol_FrameBgHovered] = A(C_PURPLE, 0.35f);   c[ImGuiCol_FrameBgActive] = A(C_PURPLE, 0.55f);
    c[ImGuiCol_CheckMark] = C_MAG;
    c[ImGuiCol_SliderGrab] = C_MAG;                    c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.45f, 1.0f, 1);
    c[ImGuiCol_Button] = A(C_PURPLE, 0.55f);           c[ImGuiCol_ButtonHovered] = A(C_PURPLE, 0.80f);    c[ImGuiCol_ButtonActive] = C_MAG;
    c[ImGuiCol_Header] = A(C_PURPLE, 0.45f);           c[ImGuiCol_HeaderHovered] = A(C_PURPLE, 0.70f);    c[ImGuiCol_HeaderActive] = C_PURPLE;
    c[ImGuiCol_Tab] = C_PANEL;                         c[ImGuiCol_TabHovered] = A(C_MAG, 0.80f);          c[ImGuiCol_TabActive] = C_PURPLE;
    c[ImGuiCol_Separator] = A(C_PURPLE, 0.5f);
    s.ScaleAllSizes(scale);
}

static void glow_rect(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, const ImVec4& col, int layers, float step) {
    for (int i = 1; i <= layers; i++) {
        float k = 1.f - (float)i / (layers + 1);
        dl->AddRect(ImVec2(a.x - i*step, a.y - i*step), ImVec2(b.x + i*step, b.y + i*step), ImGui::ColorConvertFloat4ToU32(A(col, 0.20f*k*k)),
                    rounding + i*step, 0, step * 1.6f);
    }
}
static void glow_text(ImDrawList* dl, ImVec2 p, float size, const char* t, float spread) {
    ImFont* f = ImGui::GetFont();
    for (int i = 3; i >= 1; i--) { float o = spread * i, a = 0.10f * (4 - i);
        for (int k = 0; k < 4; k++) { ImVec2 d((k&1 ? o : -o), (k&2 ? o : -o));
            dl->AddText(f, size, ImVec2(p.x + d.x, p.y + d.y), ImGui::ColorConvertFloat4ToU32(A(C_MAG, a)), t); } }
    dl->AddText(f, size, p, ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 0.85f, 1.f, 1.f)), t);
}

static bool g_menu_open = true; static float g_scale = 2.f;

static void menu(float W, float H) {
    const float u = g_scale;                       // ~ one "unit" of UI scale
    // floating launcher button
    float bs = 44.f * u;
    ImGui::SetNextWindowPos(ImVec2(16.f * u, 16.f * u)); ImGui::SetNextWindowSize(ImVec2(bs, bs));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##wz_btn", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (ImGui::InvisibleButton("##b", ImVec2(bs, bs))) g_menu_open = !g_menu_open;
    {
        ImDrawList* d = ImGui::GetWindowDrawList(); ImVec2 p = ImGui::GetWindowPos(), c(p.x + bs/2, p.y + bs/2);
        for (int i = 4; i >= 1; i--) d->AddCircle(c, bs/2 + i*2.5f, ImGui::ColorConvertFloat4ToU32(A(C_MAG, 0.07f*(5-i))), 40, 3.f);
        d->AddCircleFilled(c, bs/2, ImGui::ColorConvertFloat4ToU32(A(C_BG, 0.92f)), 40);
        d->AddCircle(c, bs/2, ImGui::ColorConvertFloat4ToU32(C_PURPLE), 40, 2.5f);
        ImVec2 ts = ImGui::CalcTextSize("W"); float fs = ImGui::GetFontSize() * 1.5f;
        glow_text(d, ImVec2(c.x - ts.x*0.75f, c.y - ts.y*0.75f), fs, "W", 1.2f);
    }
    ImGui::End(); ImGui::PopStyleVar(2);

    // hold-to-aim on-screen button (raw multitouch hit test, independent of ImGui's single pointer)
    g_aim_held = false;
    if (g_cfg.aim && g_cfg.aim_hold) {
        g_aim_btn_r = 34.f * u; g_aim_btn_c = ImVec2(W - 90.f * u, H * 0.58f);
        for (int i = 0; i < g_touch.n; i++) { float dx = g_touch.p[i].x - g_aim_btn_c.x, dy = g_touch.p[i].y - g_aim_btn_c.y;
            if (dx*dx + dy*dy <= g_aim_btn_r*g_aim_btn_r) g_aim_held = true; }
        ImDrawList* d = ImGui::GetForegroundDrawList();
        for (int i = 4; i >= 1; i--) d->AddCircle(g_aim_btn_c, g_aim_btn_r + i*3.f, ImGui::ColorConvertFloat4ToU32(A(C_MAG, g_aim_held ? 0.14f*(5-i) : 0.05f*(5-i))), 40, 3.f);
        d->AddCircleFilled(g_aim_btn_c, g_aim_btn_r, ImGui::ColorConvertFloat4ToU32(g_aim_held ? A(C_MAG, 0.75f) : A(C_BG, 0.85f)), 40);
        d->AddCircle(g_aim_btn_c, g_aim_btn_r, ImGui::ColorConvertFloat4ToU32(C_PURPLE), 40, 2.5f);
        ImVec2 ts = ImGui::CalcTextSize("AIM"); d->AddText(ImVec2(g_aim_btn_c.x - ts.x/2, g_aim_btn_c.y - ts.y/2), ImGui::ColorConvertFloat4ToU32(C_TEXT), "AIM");
    }

    if (!g_menu_open) return;

    ImGui::SetNextWindowPos(ImVec2(W * 0.27f, H * 0.08f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(W * 0.46f, 380.f * u), H * 0.80f), ImGuiCond_FirstUseEver);
    ImGui::Begin("##wenzo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    glow_rect(ImGui::GetBackgroundDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y), 14.f * u / 2.f, C_PURPLE, 7, 2.2f * u / 2.f);

    // header: gradient bar + glowing title
    {
        ImDrawList* d = ImGui::GetWindowDrawList(); float hh = 42.f * u / 1.2f;
        ImVec2 a = wp, b(wp.x + ws.x, wp.y + hh);
        d->AddRectFilledMultiColor(a, b, ImGui::ColorConvertFloat4ToU32(A(C_PURPLE, 0.55f)), ImGui::ColorConvertFloat4ToU32(A(C_MAG, 0.35f)),
                                   ImGui::ColorConvertFloat4ToU32(A(C_BG, 0.0f)), ImGui::ColorConvertFloat4ToU32(A(C_BG, 0.0f)));
        float fs = ImGui::GetFontSize() * 1.35f; ImVec2 ts = ImGui::CalcTextSize("WENZO SOFT");
        glow_text(d, ImVec2(wp.x + (ws.x - ts.x*1.35f)/2, wp.y + (hh - fs)/2), fs, "WENZO SOFT", 1.3f);
        d->AddLine(ImVec2(a.x + 6, b.y), ImVec2(b.x - 6, b.y), ImGui::ColorConvertFloat4ToU32(C_MAG), 2.f);
        ImGui::Dummy(ImVec2(0, hh - ImGui::GetStyle().WindowPadding.y + 4));
    }

    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("AIM")) {
            ImGui::Checkbox("Enable Aim", &g_cfg.aim);
            ImGui::Checkbox("Hold on-screen AIM button", &g_cfg.aim_hold);
            ImGui::Checkbox("Draw FOV circle", &g_cfg.show_fov);
            ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##fov", &g_cfg.fov_px, 50.f, 700.f, "FOV  %.0f px");
            ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##sm", &g_cfg.smooth, 0.02f, 1.f, "Smooth  %.2f");
            if (!g_ok_aim) ImGui::TextColored(C_MAG, "aim disabled: MouseLook fields differ from dump");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ESP")) {
            ImGui::Checkbox("Enable ESP", &g_cfg.esp);
            ImGui::Checkbox("Box", &g_cfg.esp_box); ImGui::SameLine(); ImGui::ColorEdit3("##cb", g_cfg.col_box, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::Checkbox("Line", &g_cfg.esp_line); ImGui::SameLine(); ImGui::ColorEdit3("##cl", g_cfg.col_line, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::Checkbox("Distance", &g_cfg.esp_dist);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("MISC")) {
            ImGui::Checkbox("Log look quats (logcat)", &g_cfg.dump_look);
            ImGui::Separator();
            ImGui::TextColored(g_ok_esp ? C_MAG : C_TEXT, "ESP resolve : %s", g_ok_esp ? "ok" : "FAIL");
            ImGui::TextColored(g_ok_aim ? C_MAG : C_TEXT, "Aim resolve : %s", g_ok_aim ? "ok" : "FAIL");
            ImGui::TextColored(g_ok_touch ? C_MAG : C_TEXT, "Touch input : %s (n=%d)", g_ok_touch ? "ok" : "FAIL", g_touch.n);
            ImGui::TextDisabled("Wenzo Soft | dump 1_17_12000");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---- frame driver / hook ------------------------------------------------------------------
static bool g_imgui_ready = false;
static double now_s() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

static void imgui_init(float H) {
    ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr; io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    g_scale = std::max(1.6f, H / 480.f);
    io.FontGlobalScale = g_scale; apply_theme(g_scale / 2.f);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_imgui_ready = true; LOG("imgui ready, scale=%.2f", g_scale);
}

static EGLBoolean (*orig_swap)(EGLDisplay, EGLSurface);
static EGLBoolean hk_swap(EGLDisplay d, EGLSurface s) {
    static thread_local bool attached = false; static int frameNo = 0; static double last = 0;
    EGLint w = 0, h = 0; eglQuerySurface(d, s, EGL_WIDTH, &w); eglQuerySurface(d, s, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && eglGetCurrentContext() != EGL_NO_CONTEXT) {
        if (!attached) { il2cpp_thread_attach(il2cpp_domain_get()); attached = true; }
        if (!g_imgui_ready) imgui_init((float)h);
        float W = (float)w, H = (float)h;
        double t = now_s(); ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(W, H); io.DeltaTime = last > 0 ? (float)std::max(1e-4, t - last) : 1.f/60.f; last = t;

        read_touches(H);
        io.AddMousePosEvent(g_touch.n ? g_touch.p[0].x : -FLT_MAX, g_touch.n ? g_touch.p[0].y : -FLT_MAX);
        io.AddMouseButtonEvent(0, g_touch.n > 0);

        ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
        world(W, H, frameNo++);
        menu(W, H);
        ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());   // backend saves/restores GL state itself
    }
    return orig_swap(d, s);
}

static void* boot(void*) {
    void* h = nullptr;
    for (int i = 0; i < 600 && !(h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD)); i++) usleep(500000);
    if (!h) { LOG("libil2cpp.so never appeared"); return nullptr; }
#define SYM(x) *(void**)&x = dlsym(h, #x); if (!x) { LOG("missing export " #x); return nullptr; }
    SYM(il2cpp_domain_get) SYM(il2cpp_thread_attach) SYM(il2cpp_domain_get_assemblies) SYM(il2cpp_assembly_get_image)
    SYM(il2cpp_image_get_name) SYM(il2cpp_class_from_name) SYM(il2cpp_class_get_field_from_name) SYM(il2cpp_field_get_offset)
    SYM(il2cpp_field_static_get_value) SYM(il2cpp_class_get_method_from_name) SYM(il2cpp_resolve_icall)
    il2cpp_thread_attach(il2cpp_domain_get());
    for (int i = 0; i < 240 && !resolve(); i++) usleep(500000);
    if (!g_ok_esp) { LOG("resolve failed, not hooking"); return nullptr; }
    void* egl = dlopen("libEGL.so", RTLD_NOW);
    void* swap = egl ? dlsym(egl, "eglSwapBuffers") : nullptr;
    if (!swap || DobbyHook(swap, (void*)hk_swap, (void**)&orig_swap) != 0) { LOG("eglSwapBuffers hook failed"); return nullptr; }
    LOG("hooked eglSwapBuffers");
    return nullptr;
}

__attribute__((constructor)) static void nd_entry() { pthread_t t; pthread_create(&t, nullptr, boot, nullptr); pthread_detach(t); }
