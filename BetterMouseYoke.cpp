a/**
 * BetterMouseYoke - X-Plane 12 Plugin
 * Cross-platform: Windows, macOS, Linux
 *
 * Windows : compile -> win.xpl  (Visual Studio / MSVC)
 * macOS   : compile -> mac.xpl  (Xcode / clang, -framework CoreGraphics)
 * Linux   : compile -> lin.xpl  (gcc/clang, -lX11 -lXtst)
 */

#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>

#include "XPLMPlugin.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"

// -----------------------------------------------------------------------
// Platform includes
// -----------------------------------------------------------------------
#if IBM
    #include <windows.h>
#elif APL
    #include <CoreGraphics/CoreGraphics.h>
#elif LIN
    #include <X11/Xlib.h>
    #include <X11/extensions/XTest.h>
#endif

// -----------------------------------------------------------------------
// Configuracoes
// -----------------------------------------------------------------------
static int   cfg_set_pos             = 1;
static int   cfg_change_cursor       = 1;
static float cfg_rudder_defl_dist    = 200.0f;
static float cfg_rudder_return_speed = 2.0f;

// -----------------------------------------------------------------------
// Estado global
// -----------------------------------------------------------------------
static int   g_yoke_active    = 0;
static int   g_rudder_control = 0;
static float g_rudder_pos     = 0.0f;
static int   g_cursor_x       = 0;
static int   g_cursor_y       = 0;

static int   g_rudder_center_x = 0;
static int   g_rudder_center_y = 0;

static XPLMDataRef    g_dr_override_joystick = NULL;
static XPLMDataRef    g_dr_yoke_pitch        = NULL;
static XPLMDataRef    g_dr_yoke_roll         = NULL;
static XPLMDataRef    g_dr_yoke_heading      = NULL;
static XPLMCommandRef g_cmd_toggle           = NULL;

// -----------------------------------------------------------------------
// Linux: conexao X11 persistente (evita XOpenDisplay/XCloseDisplay por frame)
// -----------------------------------------------------------------------
#if LIN
static Display* g_display = NULL;
#endif

// -----------------------------------------------------------------------
// Cursor handles — Windows only
// -----------------------------------------------------------------------
#if IBM
    static HCURSOR g_cursor_yoke   = NULL;
    static HCURSOR g_cursor_rudder = NULL;
    static HCURSOR g_cursor_orig   = NULL;
#endif

// -----------------------------------------------------------------------
// Platform: load settings
// -----------------------------------------------------------------------
static void LoadSettings()
{
    char path[512];
    XPLMGetSystemPath(path);
    char ini[600];
    snprintf(ini, sizeof(ini), "%sResources/plugins/BetterMouseYoke/settings.ini", path);

    FILE* f = fopen(ini, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '[') continue;
        char  key[64];
        float val = 0.0f;
        if (sscanf(line, " %63[^= ] = %f", key, &val) == 2)
        {
            if      (strcmp(key, "set_pos") == 0)                    cfg_set_pos = (int)val;
            else if (strcmp(key, "change_cursor") == 0)              cfg_change_cursor = (int)val;
            else if (strcmp(key, "rudder_deflection_distance") == 0) cfg_rudder_defl_dist = val;
            else if (strcmp(key, "rudder_return_speed") == 0)        cfg_rudder_return_speed = val;
        }
    }
    fclose(f);
}

// -----------------------------------------------------------------------
// Dataref helpers
// -----------------------------------------------------------------------
static void SetYokePitch(float v)   { XPLMSetDataf(g_dr_yoke_pitch,   v); }
static void SetYokeRoll(float v)    { XPLMSetDataf(g_dr_yoke_roll,    v); }
static void SetYokeHeading(float v) { XPLMSetDataf(g_dr_yoke_heading, v); }
static void EnableOverride(int on)  { XPLMSetDatai(g_dr_override_joystick, on); }

// -----------------------------------------------------------------------
// Platform: get mouse position (X-Plane coords: origin bottom-left)
// -----------------------------------------------------------------------
static void GetMousePos(int* x, int* y)
{
#if IBM
    POINT p;
    GetCursorPos(&p);

    HWND  hwnd = GetForegroundWindow();
    RECT  rect;
    GetClientRect(hwnd, &rect);
    POINT origin = {0, 0};
    ClientToScreen(hwnd, &origin);

    int clientW = rect.right  - rect.left;
    int clientH = rect.bottom - rect.top;

    *x = p.x - origin.x;
    *y = clientH - (p.y - origin.y);

    if (*x < 0)       *x = 0;
    if (*y < 0)       *y = 0;
    if (*x > clientW) *x = clientW;
    if (*y > clientH) *y = clientH;

#elif APL
    CGEventRef event = CGEventCreate(NULL);
    CGPoint    loc   = CGEventGetLocation(event);
    CFRelease(event);

    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);

    *x = (int)loc.x;
    *y = sh - (int)loc.y;  // flip Y: CG origin top-left, XP bottom-left

    if (*x < 0)  *x = 0;
    if (*y < 0)  *y = 0;
    if (*x > sw) *x = sw;
    if (*y > sh) *y = sh;

#elif LIN
    if (!g_display) { *x = 0; *y = 0; return; }

    Window   root = DefaultRootWindow(g_display);
    Window   child;
    int      rx, ry, wx, wy;
    unsigned mask;
    XQueryPointer(g_display, root, &root, &child, &rx, &ry, &wx, &wy, &mask);

    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);

    *x = rx;
    *y = sh - ry;

    if (*x < 0)  *x = 0;
    if (*y < 0)  *y = 0;
    if (*x > sw) *x = sw;
    if (*y > sh) *y = sh;
#endif
}

// -----------------------------------------------------------------------
// Platform: move mouse cursor
// -----------------------------------------------------------------------
static void SetMousePosXP(int x, int y)
{
#if IBM
    HWND  hwnd = GetForegroundWindow();
    RECT  rect;
    GetClientRect(hwnd, &rect);
    POINT origin = {0, 0};
    ClientToScreen(hwnd, &origin);
    int clientH = rect.bottom - rect.top;
    SetCursorPos(origin.x + x, origin.y + (clientH - y));

#elif APL
    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);
    CGPoint pt = CGPointMake((CGFloat)x, (CGFloat)(sh - y));
    // CGWarpMouseCursorPosition requer Accessibility no macOS 13+.
    // CGEventCreateMouseEvent + CGEventPost move o cursor sem essa permissao.
    CGEventRef moveEvent = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, pt, kCGMouseButtonLeft);
    if (moveEvent) {
        CGEventPost(kCGHIDEventTap, moveEvent);
        CFRelease(moveEvent);
    }

#elif LIN
    if (!g_display) return;
    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);
    XTestFakeMotionEvent(g_display, DefaultScreen(g_display), x, sh - y, 0);
    XFlush(g_display);
#endif
}

// -----------------------------------------------------------------------
// Platform: check left mouse button state
// -----------------------------------------------------------------------
static int IsLeftButtonDown()
{
#if IBM
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;

#elif APL
    return CGEventSourceButtonState(kCGEventSourceStateHIDSystemState,
                                    kCGMouseButtonLeft) ? 1 : 0;

#elif LIN
    if (!g_display) return 0;
    Window   root = DefaultRootWindow(g_display);
    Window   child;
    int      rx, ry, wx, wy;
    unsigned mask;
    XQueryPointer(g_display, root, &root, &child, &rx, &ry, &wx, &wy, &mask);
    return (mask & Button1Mask) ? 1 : 0;
#endif
}

// -----------------------------------------------------------------------
// Platform: cursor management
// -----------------------------------------------------------------------
static void InitCursors()
{
#if IBM
    g_cursor_yoke   = LoadCursor(NULL, IDC_CROSS);
    g_cursor_rudder = LoadCursor(NULL, IDC_SIZEWE);
#endif
}

static void SetYokeCursor()
{
#if IBM
    if (g_cursor_yoke) SetCursor(g_cursor_yoke);
#endif
}

static void SetRudderCursor()
{
#if IBM
    if (g_cursor_rudder) SetCursor(g_cursor_rudder);
#endif
}

static void RestoreOrigCursor()
{
#if IBM
    if (g_cursor_orig) SetCursor(g_cursor_orig);
#endif
}

static void SaveOrigCursor()
{
#if IBM
    g_cursor_orig = GetCursor();
#endif
}

// -----------------------------------------------------------------------
// Toggle command
// -----------------------------------------------------------------------
static int CommandHandler(XPLMCommandRef cmd, XPLMCommandPhase phase, void* refcon)
{
    if (phase != xplm_CommandBegin)
        return 1;

    g_yoke_active = !g_yoke_active;

    if (g_yoke_active)
    {
        SaveOrigCursor();
        EnableOverride(1);

        if (cfg_set_pos)
        {
            int sw = 0, sh = 0;
            XPLMGetScreenSize(&sw, &sh);
            if (sw > 0 && sh > 0)
            {
                float pitch = XPLMGetDataf(g_dr_yoke_pitch);
                float roll  = XPLMGetDataf(g_dr_yoke_roll);
                int mx = (int)((roll   + 1.0f) * 0.5f * (float)sw);
                int my = (int)((-pitch + 1.0f) * 0.5f * (float)sh);
                SetMousePosXP(mx, my);
            }
        }

        if (cfg_change_cursor) SetYokeCursor();

        XPLMDebugString("BetterMouseYoke: ATIVO\n");
    }
    else
    {
        g_rudder_control = 0;
        g_rudder_pos     = 0.0f;

        SetYokePitch(0.0f);
        SetYokeRoll(0.0f);
        SetYokeHeading(0.0f);
        EnableOverride(0);

        if (cfg_change_cursor) RestoreOrigCursor();

        XPLMDebugString("BetterMouseYoke: INATIVO\n");
    }

    return 1;
}

// -----------------------------------------------------------------------
// Draw callback - HUD
// -----------------------------------------------------------------------
static int DrawCallback(XPLMDrawingPhase phase, int isBefore, void* refcon)
{
    if (!g_yoke_active)
        return 1;

    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);

    float magenta[3] = { 1.0f, 0.0f, 1.0f };
    float green[3]   = { 0.0f, 1.0f, 0.0f };

    if (g_rudder_control)
        XPLMDrawString(magenta, 10, sh - 20, (char*)"MOUSE RUDDER CONTROL", NULL, xplmFont_Basic);
    else
        XPLMDrawString(magenta, 10, sh - 20, (char*)"MOUSE YOKE CONTROL",   NULL, xplmFont_Basic);

    if (!g_rudder_control)
    {
        XPLMDrawString(green, sw / 2 - 3, sh / 2, (char*)"+", NULL, xplmFont_Basic);
    }
    else
    {
        for (int i = 1; i < 3; i++)
        {
            XPLMDrawString(green, g_cursor_x - (int)cfg_rudder_defl_dist, g_cursor_y + 4 - 7 * i, (char*)"|", NULL, xplmFont_Basic);
            XPLMDrawString(green, g_cursor_x + (int)cfg_rudder_defl_dist, g_cursor_y + 4 - 7 * i, (char*)"|", NULL, xplmFont_Basic);
        }
    }

    return 1;
}

// -----------------------------------------------------------------------
// Flight loop
// -----------------------------------------------------------------------
static float FlightLoopCallback(float elapsedSinceLastCall,
                                float elapsedSinceLastLoop,
                                int   counter,
                                void* refcon)
{
    if (!g_yoke_active)
        return -1.0f;

    int sw = 0, sh = 0;
    XPLMGetScreenSize(&sw, &sh);
    if (sw <= 0 || sh <= 0)
        return -1.0f;

    GetMousePos(&g_cursor_x, &g_cursor_y);

    int left_down = IsLeftButtonDown();

    if (left_down && !g_rudder_control)
    {
        g_rudder_control  = 1;
        g_rudder_center_x = g_cursor_x;
        g_rudder_center_y = g_cursor_y;
        if (cfg_change_cursor) SetRudderCursor();
    }
    else if (!left_down && g_rudder_control)
    {
        g_rudder_control = 0;
        if (cfg_change_cursor) SetYokeCursor();
    }

    if (g_rudder_control)
    {
        float dx = (float)(g_cursor_x - g_rudder_center_x);

        if (fabsf(dx) < 8.0f) dx = 0.0f;

        float rudder = dx / cfg_rudder_defl_dist;
        if (rudder >  1.0f) rudder =  1.0f;
        if (rudder < -1.0f) rudder = -1.0f;

        g_rudder_pos = rudder;

        SetYokeHeading(rudder);
        SetYokePitch(0.0f);
        SetYokeRoll(0.0f);
    }
    else
    {
        if (g_rudder_pos > 0.001f || g_rudder_pos < -0.001f)
        {
            float step = cfg_rudder_return_speed * elapsedSinceLastCall;
            if (g_rudder_pos > 0.0f)
            {
                g_rudder_pos -= step;
                if (g_rudder_pos < 0.0f) g_rudder_pos = 0.0f;
            }
            else
            {
                g_rudder_pos += step;
                if (g_rudder_pos > 0.0f) g_rudder_pos = 0.0f;
            }
            SetYokeHeading(g_rudder_pos);
        }

        float roll  = ((float)g_cursor_x / (float)sw) * 2.0f - 1.0f;
        float pitch = -(((float)g_cursor_y / (float)sh) * 2.0f - 1.0f);

        if (roll  >  1.0f) roll  =  1.0f;
        if (roll  < -1.0f) roll  = -1.0f;
        if (pitch >  1.0f) pitch =  1.0f;
        if (pitch < -1.0f) pitch = -1.0f;

        SetYokeRoll(roll);
        SetYokePitch(pitch);
    }

    return -1.0f;
}

// -----------------------------------------------------------------------
// Plugin entrypoints
// -----------------------------------------------------------------------
PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc)
{
    strncpy(outName, "BetterMouseYoke",                          255);
    strncpy(outSig,  "bettermouseyoke.xp12",                     255);
    strncpy(outDesc, "Mouse yoke control melhorado para X-Plane 12", 255);

    LoadSettings();

#if LIN
    g_display = XOpenDisplay(NULL);
    if (!g_display)
        XPLMDebugString("BetterMouseYoke: AVISO - nao foi possivel abrir display X11.\n");
#endif

    g_dr_override_joystick = XPLMFindDataRef("sim/operation/override/override_joystick");
    g_dr_yoke_pitch        = XPLMFindDataRef("sim/cockpit2/controls/yoke_pitch_ratio");
    g_dr_yoke_roll         = XPLMFindDataRef("sim/cockpit2/controls/yoke_roll_ratio");
    g_dr_yoke_heading      = XPLMFindDataRef("sim/cockpit2/controls/yoke_heading_ratio");

    if (!g_dr_override_joystick || !g_dr_yoke_pitch || !g_dr_yoke_roll || !g_dr_yoke_heading)
    {
        XPLMDebugString("BetterMouseYoke: ERRO - Datarefs nao encontrados!\n");
        return 0;
    }

    g_cmd_toggle = XPLMCreateCommand("BetterMouseYoke/toggle", "Ativar/Desativar Mouse Yoke");
    XPLMRegisterCommandHandler(g_cmd_toggle, CommandHandler, 1, NULL);

    XPLMRegisterDrawCallback(DrawCallback, xplm_Phase_Window, 0, NULL);
    XPLMRegisterFlightLoopCallback(FlightLoopCallback, -1.0f, NULL);

    InitCursors();

    XPLMDebugString("BetterMouseYoke: Iniciado! Atribua uma tecla ao comando BetterMouseYoke/toggle.\n");

    return 1;
}

PLUGIN_API void XPluginStop()
{
    if (g_yoke_active)
    {
        g_yoke_active = 0;
        SetYokePitch(0.0f);
        SetYokeRoll(0.0f);
        SetYokeHeading(0.0f);
        EnableOverride(0);
    }

    XPLMUnregisterFlightLoopCallback(FlightLoopCallback, NULL);
    XPLMUnregisterDrawCallback(DrawCallback, xplm_Phase_Window, 0, NULL);

    if (g_cmd_toggle)
        XPLMUnregisterCommandHandler(g_cmd_toggle, CommandHandler, 1, NULL);

#if LIN
    if (g_display)
    {
        XCloseDisplay(g_display);
        g_display = NULL;
    }
#endif

    XPLMDebugString("BetterMouseYoke: Encerrado.\n");
}

PLUGIN_API void XPluginDisable()
{
    if (g_yoke_active)
    {
        g_yoke_active = 0;
        SetYokePitch(0.0f);
        SetYokeRoll(0.0f);
        SetYokeHeading(0.0f);
        EnableOverride(0);
    }
}

PLUGIN_API int XPluginEnable()
{
    LoadSettings();
    return 1;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, int msg, void* param)
{
}
