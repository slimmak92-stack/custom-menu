// GTA6-style pause menu for GTA San Andreas (1.0 US) — standalone ASI.
// No plugin-sdk, no D3DX: only Windows + Direct3D 9 headers.
//
// How it works: hooks IDirect3DDevice9::EndScene of the game's device. While the stock
// pause menu is open (CMenuManager::m_bMenuActive) it paints a GTA6-style menu on top.
// ESC / Resume still use the game's own handling, so pausing & resuming stay native.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <algorithm>

// ------------------------------------------------------------------ game ---
static IDirect3DDevice9** const kGameDevicePtr = reinterpret_cast<IDirect3DDevice9**>(0xC97C28);
static volatile BYTE* const kMenuActive = reinterpret_cast<volatile BYTE*>(0xBA67A4);

// ----------------------------------------------------------------- state ---
static IDirect3DDevice9* g_dev = nullptr;
static HWND g_hwnd = nullptr;
static WNDPROC g_oldProc = nullptr;
typedef HRESULT(WINAPI* EndScene_t)(IDirect3DDevice9*);
static EndScene_t g_origEndScene = nullptr;

static int g_mx = 0, g_my = 0;           // client coords
static bool g_down = false, g_click = false, g_keyQ = false, g_keyE = false;
static int g_escFrames = 0;              // synthetic ESC press countdown
static int g_page = 0;                   // 0 menu, 1 settings
static int g_sel = 1;
static int g_stab = 0;
static int g_hoverRow = -1;
static int g_dragRow = -1;
static bool g_wasActive = false;

// settings (persisted)
static int g_accent = 0;                 // 0..3
static int g_bg = 0;                     // 0..2
static int g_h24 = 1;
static int g_showFps = 0;

static const DWORD kAccents[4] = { 0xFFF2539C, 0xFF40D0FF, 0xFFFF963C, 0xFF5ADC82 };
static const char* const kAccentNames[4] = { "Pink", "Cyan", "Orange", "Green" };
static const char* const kBgNames[3] = { "Dusk", "Night", "Sunset" };

// ----------------------------------------------------------------- ini ---
static std::string IniPath()
{
    char buf[MAX_PATH] = {};
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&IniPath), &self);
    GetModuleFileNameA(self, buf, MAX_PATH);
    std::string p(buf);
    size_t s = p.find_last_of("\\/");
    return (s == std::string::npos ? std::string() : p.substr(0, s + 1)) + "gta6menu.ini";
}
static void LoadIni()
{
    const std::string p = IniPath();
    g_accent = std::min(3, std::max(0, (int)GetPrivateProfileIntA("menu", "accent", 0, p.c_str())));
    g_bg = std::min(2, std::max(0, (int)GetPrivateProfileIntA("menu", "background", 0, p.c_str())));
    g_h24 = GetPrivateProfileIntA("menu", "clock24", 1, p.c_str()) ? 1 : 0;
    g_showFps = GetPrivateProfileIntA("menu", "showfps", 0, p.c_str()) ? 1 : 0;
}
static void SaveIni()
{
    const std::string p = IniPath();
    char b[16];
    sprintf(b, "%d", g_accent);  WritePrivateProfileStringA("menu", "accent", b, p.c_str());
    sprintf(b, "%d", g_bg);      WritePrivateProfileStringA("menu", "background", b, p.c_str());
    sprintf(b, "%d", g_h24);     WritePrivateProfileStringA("menu", "clock24", b, p.c_str());
    sprintf(b, "%d", g_showFps); WritePrivateProfileStringA("menu", "showfps", b, p.c_str());
}

// ------------------------------------------------------------ rendering ---
struct Vtx { float x, y, z, rhw; DWORD c; float u, v; };
static const DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static float W = 1920, H = 1080, S = 1.0f;

static DWORD RGBA(int r, int g, int b, int a = 255) { return ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b; }
static DWORD WithAlpha(DWORD c, int a) { return (c & 0x00FFFFFF) | ((DWORD)a << 24); }
static const DWORD WHITE = 0xFFFFFFFF;
static const DWORD INK = 0xFF140F28;

static void SetupState()
{
    IDirect3DDevice9* d = g_dev;
    d->SetPixelShader(nullptr);
    d->SetVertexShader(nullptr);
    d->SetFVF(kFvf);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}
static void ModeSolid()
{
    g_dev->SetTexture(0, nullptr);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
}
static void ModeTex()
{
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

// corners: TL, TR, BR, BL
static void Quad(float x0, float y0, float x1, float y1, DWORD c0, DWORD c1, DWORD c2, DWORD c3)
{
    Vtx v[4] = {
        { x0 - 0.5f, y0 - 0.5f, 0, 1, c0, 0, 0 }, { x1 - 0.5f, y0 - 0.5f, 0, 1, c1, 1, 0 },
        { x1 - 0.5f, y1 - 0.5f, 0, 1, c2, 1, 1 }, { x0 - 0.5f, y1 - 0.5f, 0, 1, c3, 0, 1 } };
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(Vtx));
}
static void Rect(float x, float y, float w, float h, DWORD c) { ModeSolid(); Quad(x, y, x + w, y + h, c, c, c, c); }
static void GradH(float x, float y, float w, float h, DWORD l, DWORD r) { ModeSolid(); Quad(x, y, x + w, y + h, l, r, r, l); }
static void GradV(float x, float y, float w, float h, DWORD t, DWORD b) { ModeSolid(); Quad(x, y, x + w, y + h, t, t, b, b); }
static void Grad4(float x, float y, float w, float h, DWORD tl, DWORD tr, DWORD br, DWORD bl) { ModeSolid(); Quad(x, y, x + w, y + h, tl, tr, br, bl); }

static void Circle(float cx, float cy, float r, DWORD c, int seg = 28)
{
    ModeSolid();
    Vtx v[40];
    int n = std::min(seg, 36);
    v[0] = { cx - 0.5f, cy - 0.5f, 0, 1, c, 0, 0 };
    for (int i = 0; i <= n; ++i)
    {
        float a = 6.2831853f * i / n;
        v[i + 1] = { cx + std::cos(a) * r - 0.5f, cy + std::sin(a) * r - 0.5f, 0, 1, c, 0, 0 };
    }
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, n, v, sizeof(Vtx));
}
static void Line(float x0, float y0, float x1, float y1, float th, DWORD c)
{
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len * th * 0.5f, ny = dx / len * th * 0.5f;
    ModeSolid();
    Vtx v[4] = {
        { x0 + nx - 0.5f, y0 + ny - 0.5f, 0, 1, c, 0, 0 }, { x1 + nx - 0.5f, y1 + ny - 0.5f, 0, 1, c, 0, 0 },
        { x1 - nx - 0.5f, y1 - ny - 0.5f, 0, 1, c, 0, 0 }, { x0 - nx - 0.5f, y0 - ny - 0.5f, 0, 1, c, 0, 0 } };
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(Vtx));
}
static void Pill(float x, float y, float w, float h, DWORD c)
{
    float r = h * 0.5f;
    Rect(x + r, y, w - h, h, c);
    Circle(x + r, y + r, r, c);
    Circle(x + w - r, y + r, r, c);
}
static void Ring(float cx, float cy, float r, float th, DWORD c)
{
    const int n = 20;
    for (int i = 0; i < n; ++i)
    {
        float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
        Line(cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, th, c);
    }
}

// ---- text: GDI renders into a texture, cached per (font, string) ----
struct TextTex { IDirect3DTexture9* tex; int w, h; };
static std::unordered_map<std::string, TextTex> g_text;
static HFONT g_fonts[4] = {};   // 0 small, 1 regular, 2 big, 3 huge
static int g_fontPx[4] = { 22, 28, 44, 92 };
static float g_fontScale = 0;

static void ClearText()
{
    for (auto& kv : g_text) if (kv.second.tex) kv.second.tex->Release();
    g_text.clear();
}
static void EnsureFonts()
{
    if (g_fontScale == S) return;
    ClearText();
    for (int i = 0; i < 4; ++i)
    {
        if (g_fonts[i]) DeleteObject(g_fonts[i]);
        g_fonts[i] = CreateFontA(-(int)(g_fontPx[i] * S + 0.5f), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, "Bahnschrift");
    }
    g_fontScale = S;
}
static TextTex* GetText(int font, const char* str)
{
    std::string key = std::to_string(font) + ":" + str;
    auto it = g_text.find(key);
    if (it != g_text.end()) return &it->second;
    if (g_text.size() > 400) ClearText();

    TextTex tt = { nullptr, 0, 0 };
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldFont = SelectObject(dc, g_fonts[font]);
    SIZE sz = {};
    int len = (int)std::strlen(str);
    GetTextExtentPoint32A(dc, str, len, &sz);
    int w = sz.cx + 6, h = sz.cy + 6;
    if (len > 0 && w > 6 && h > 6 && w < 4096 && h < 1024)
    {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bmp && bits)
        {
            HGDIOBJ oldBmp = SelectObject(dc, bmp);
            std::memset(bits, 0, (size_t)w * h * 4);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            TextOutA(dc, 3, 3, str, len);
            GdiFlush();
            IDirect3DTexture9* tex = nullptr;
            if (SUCCEEDED(g_dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) && tex)
            {
                D3DLOCKED_RECT lr;
                if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)))
                {
                    const BYTE* src = (const BYTE*)bits;
                    for (int y = 0; y < h; ++y)
                    {
                        DWORD* dst = (DWORD*)((BYTE*)lr.pBits + y * lr.Pitch);
                        for (int x = 0; x < w; ++x)
                        {
                            BYTE a = src[(y * w + x) * 4];     // blue channel == coverage (white text)
                            dst[x] = 0x00FFFFFF | ((DWORD)a << 24);
                        }
                    }
                    tex->UnlockRect(0);
                    tt = { tex, w, h };
                }
                else tex->Release();
            }
            SelectObject(dc, oldBmp);
            DeleteObject(bmp);
        }
    }
    SelectObject(dc, oldFont);
    DeleteDC(dc);
    return &(g_text[key] = tt);
}
static float TextW(int font, const char* s) { TextTex* t = GetText(font, s); return t->tex ? (float)(t->w - 6) : 0.0f; }
static float TextH(int font, const char* s) { TextTex* t = GetText(font, s); return t->tex ? (float)(t->h - 6) : (float)g_fontPx[font] * S; }
static void Text(int font, float x, float y, DWORD col, const char* s)
{
    TextTex* t = GetText(font, s);
    if (!t->tex) return;
    ModeTex();
    g_dev->SetTexture(0, t->tex);
    Quad(x - 3, y - 3, x - 3 + t->w, y - 3 + t->h, col, col, col, col);
    g_dev->SetTexture(0, nullptr);
}
static void TextR(int font, float xr, float y, DWORD col, const char* s) { Text(font, xr - TextW(font, s), y, col, s); }
static void TextC(int font, float x, float y, float w, float h, DWORD col, const char* s)
{
    Text(font, x + (w - TextW(font, s)) * 0.5f, y + (h - TextH(font, s)) * 0.5f, col, s);
}

// ------------------------------------------------------------------ ui ---
static bool Hit(float x, float y, float w, float h, float mx, float my) { return mx >= x && mx < x + w && my >= y && my < y + h; }

static void Icon(int kind, float cx, float cy, float s, DWORD c)
{
    float h = s * 0.5f, th = std::max(2.0f, s * 0.09f);
    if (kind == 0) {                                   // resume: triangle
        Line(cx - h * 0.5f, cy - h, cx - h * 0.5f, cy + h, th, c);
        Line(cx - h * 0.5f, cy - h, cx + h, cy, th, c);
        Line(cx - h * 0.5f, cy + h, cx + h, cy, th, c);
    } else if (kind == 1) {                            // map
        Line(cx - h, cy - h * 0.8f, cx + h, cy - h * 0.8f, th, c);
        Line(cx - h, cy + h * 0.8f, cx + h, cy + h * 0.8f, th, c);
        Line(cx - h, cy - h * 0.8f, cx - h, cy + h * 0.8f, th, c);
        Line(cx + h, cy - h * 0.8f, cx + h, cy + h * 0.8f, th, c);
        Line(cx - h * 0.33f, cy - h * 0.8f, cx - h * 0.33f, cy + h * 0.8f, th, c);
        Line(cx + h * 0.33f, cy - h * 0.8f, cx + h * 0.33f, cy + h * 0.8f, th, c);
    } else if (kind == 2) {                            // game pad
        Line(cx - h, cy - h * 0.5f, cx + h, cy - h * 0.5f, th, c);
        Line(cx - h, cy + h * 0.5f, cx + h, cy + h * 0.5f, th, c);
        Line(cx - h, cy - h * 0.5f, cx - h, cy + h * 0.5f, th, c);
        Line(cx + h, cy - h * 0.5f, cx + h, cy + h * 0.5f, th, c);
        Line(cx - h * 0.6f, cy, cx - h * 0.2f, cy, th, c);
        Line(cx - h * 0.4f, cy - h * 0.2f, cx - h * 0.4f, cy + h * 0.2f, th, c);
        Circle(cx + h * 0.35f, cy, th, c, 10);
        Circle(cx + h * 0.65f, cy + h * 0.15f, th, c, 10);
    } else if (kind == 3) {                            // tag
        Line(cx - h, cy - h * 0.2f, cx - h * 0.2f, cy - h, th, c);
        Line(cx - h * 0.2f, cy - h, cx + h, cy - h, th, c);
        Line(cx + h, cy - h, cx + h, cy + h * 0.2f, th, c);
        Line(cx + h, cy + h * 0.2f, cx + h * 0.2f, cy + h, th, c);
        Line(cx + h * 0.2f, cy + h, cx - h, cy - h * 0.2f, th, c);
    } else {                                           // gear
        Ring(cx, cy, h * 0.5f, th, c);
        for (int i = 0; i < 8; ++i) {
            float a = i * 0.7853982f;
            Line(cx + std::cos(a) * h * 0.7f, cy + std::sin(a) * h * 0.7f, cx + std::cos(a) * h, cy + std::sin(a) * h, th * 1.6f, c);
        }
    }
}

static void Palm(float bx, float by, float tx, float ty, float size, DWORD c)
{
    float px = bx, py = by;
    for (int i = 1; i <= 20; ++i) {
        float t = i / 20.0f;
        float x = bx + (tx - bx) * t + std::sin(t * 3.14159f) * size * 0.12f;
        float y = by + (ty - by) * t;
        Line(px, py, x, y, std::max(3.0f, size * 0.07f * (1 - 0.55f * t)), c);
        px = x; py = y;
    }
    for (int f = 0; f < 10; ++f) {
        float a = (-170 + f * 36) * 0.0174533f;
        float len = size * (0.75f + ((f * 37) % 10) / 40.0f);
        float lx = tx, ly = ty;
        for (int j = 1; j <= 10; ++j) {
            float t = j / 10.0f;
            float x = tx + std::cos(a) * len * t;
            float y = ty + std::sin(a) * len * t * 0.55f + len * 0.5f * t * t;
            Line(lx, ly, x, y, std::max(2.0f, (1 - t) * size * 0.055f + 1), c);
            lx = x; ly = y;
        }
    }
}

static void KeyCap(float& x, float y, const char* key, const char* label, float h, bool rightAligned)
{
    // draws "LABEL [KEY]" ending at x, moves x left
    float kw = TextW(0, key) + 22 * S;
    float lw = TextW(0, label);
    float kx = x - kw;
    Rect(kx, y, kw, h, WHITE);
    TextC(0, kx, y, kw, h, INK, key);
    Text(0, kx - 12 * S - lw, y + (h - TextH(0, label)) * 0.5f, WHITE, label);
    x = kx - 12 * S - lw - 36 * S;
    (void)rightAligned;
}

struct Setting { int tab; int kind; const char* label; const char* desc; int* val; int mn, mx; const char* const* opts; };
// kinds: 0 toggle, 1 slider, 2 cycle, 3 action
static Setting g_settings[] = {
    { 0, 2, "Accent colour", "Colour of toggles, sliders and highlights.", &g_accent, 0, 3, kAccentNames },
    { 0, 2, "Background", "Backdrop gradient shown behind the menu.", &g_bg, 0, 2, kBgNames },
    { 1, 0, "24-hour clock", "Show the clock in 24-hour format instead of AM/PM.", &g_h24, 0, 1, nullptr },
    { 1, 0, "Show FPS", "Show the frame rate in the top-left corner.", &g_showFps, 0, 1, nullptr },
    { 1, 3, "Restore defaults", "Reset every menu setting to its default.", nullptr, 0, 0, nullptr },
};
static const char* const kTabs[2] = { "DISPLAY", "MENU" };

static void DrawRow(int idx, const Setting& s, float x, float y, float w, float h, bool click, bool down)
{
    bool hov = Hit(x, y, w, h, (float)g_mx, (float)g_my);
    if (hov) g_hoverRow = idx;
    DWORD fg = hov ? INK : WHITE;
    if (hov) {
        Rect(x, y, w * 0.55f, h, WHITE);
        GradH(x + w * 0.55f, y, w * 0.45f, h, WHITE, 0x38FFFFFF);
    } else {
        Rect(x, y + h - 1, w, 1, 0x2EFFFFFF);
    }
    float ty = y + (h - TextH(1, s.label)) * 0.5f;
    Text(1, x + 22 * S, ty, fg, s.label);
    float rx = x + w - 50 * S;

    if (s.kind == 3) {
        if (hov && click) { g_accent = 0; g_bg = 0; g_h24 = 1; g_showFps = 0; SaveIni(); }
    } else if (s.kind == 0) {
        if (hov && click) { *s.val = !*s.val; SaveIni(); }
        bool on = *s.val != 0;
        float pw = 84 * S, ph = 34 * S, px = rx - pw, py = y + (h - ph) * 0.5f;
        Pill(px, py, pw, ph, on ? kAccents[g_accent] : (hov ? 0x64000000 : 0x64FFFFFF));
        Circle(on ? px + pw - ph * 0.5f : px + ph * 0.5f, py + ph * 0.5f, ph * 0.5f - 4 * S, WHITE);
        TextR(1, px - 16 * S, ty, fg, on ? "On" : "Off");
    } else if (s.kind == 2) {
        int n = s.mx - s.mn + 1;
        const char* label = s.opts[*s.val];
        float vw = TextW(1, label), aw = 22 * S;
        float cr = rx, cl = cr - (aw * 2 + vw + 28 * S);
        if (hov && click) {
            int dir = 1;
            if ((float)g_mx >= cl && (float)g_mx <= cr && (float)g_mx < (cl + cr) * 0.5f) dir = -1;
            *s.val = ((*s.val - s.mn + dir) % n + n) % n + s.mn;
            SaveIni();
            label = s.opts[*s.val];
        }
        Text(1, cl + aw + 14 * S, ty, fg, label);
        float cy = y + h * 0.5f, a = 7 * S, lx = cl + aw * 0.5f, rr = cr - aw * 0.5f;
        Line(lx + a * 0.5f, cy - a, lx - a * 0.5f, cy, 2.5f * S, fg);
        Line(lx - a * 0.5f, cy, lx + a * 0.5f, cy + a, 2.5f * S, fg);
        Line(rr - a * 0.5f, cy - a, rr + a * 0.5f, cy, 2.5f * S, fg);
        Line(rr + a * 0.5f, cy, rr - a * 0.5f, cy + a, 2.5f * S, fg);
    }
    (void)down;
}

static void DrawBackground(int style)
{
    static const DWORD tl[3] = { 0xFF2C3E6E, 0xFF10122A, 0xFF3A2A5E };
    static const DWORD tr[3] = { 0xFF3A4C86, 0xFF1A1D40, 0xFF5A3A7E };
    static const DWORD br[3] = { 0xFFB5568C, 0xFF3A2060, 0xFFFF7A5A };
    static const DWORD bl[3] = { 0xFF8A4A92, 0xFF2A1A50, 0xFFC8506E };
    Grad4(0, 0, W, H, tl[style], tr[style], br[style], bl[style]);
}

static void DrawMenuPage(bool click)
{
    DrawBackground(g_bg);
    float sbw = W * 0.25f;
    Rect(0, 0, sbw, H, 0xFF1B1A2B);

    // logo
    Text(3, 66 * S, 36 * S, WHITE, "VI");
    float lw = TextW(3, "VI");
    DWORD stripe[3] = { 0xFFFF5E8C, 0xFFFF786E, 0xFFFFA05A };
    for (int i = 0; i < 3; ++i) Rect(66 * S, 126 * S + i * 6 * S, lw, 3 * S, stripe[i]);

    static const char* const names[5] = { "Resume", "Map", "Game", "Store", "Settings" };
    for (int i = 0; i < 5; ++i) {
        float x = 30 * S, y = (202 + i * 54) * S, w = sbw - 60 * S, h = 48 * S;
        bool hov = Hit(x, y, w, h, (float)g_mx, (float)g_my);
        if (hov && click) {
            if (i == 0) g_escFrames = 4;               // press the game's own ESC => native resume
            else if (i == 4) { g_page = 1; g_sel = 4; }
            else g_sel = i;
        }
        bool on = hov || g_sel == i;
        DWORD fg = on ? INK : WHITE;
        if (on) {
            Rect(x, y, w * 0.6f, h, WHITE);
            GradH(x + w * 0.6f, y, w * 0.4f, h, WHITE, 0x46FFFFFF);
        }
        Icon(i, x + 32 * S, y + h * 0.5f, 26 * S, fg);
        Text(1, x + 70 * S, y + (h - TextH(1, names[i])) * 0.5f, fg, names[i]);
    }

    // content area
    float cx = sbw + 40 * S, cy = 150 * S;
    if (g_sel == 1) {
        Text(2, cx, cy, WHITE, "MAP");
        Text(1, cx, cy + 90 * S, 0xC8FFFFFF, "Use the stock map: press ESC, then pick Map in the game menu.");
    } else if (g_sel == 2) {
        Text(2, cx, cy, WHITE, "GAME");
        char b[64];
        sprintf(b, "Resolution  %d x %d", (int)W, (int)H);
        Text(1, cx, cy + 100 * S, WHITE, b);
        sprintf(b, "Menu build  GTA6 style 1.0");
        Text(1, cx, cy + 164 * S, WHITE, b);
    } else if (g_sel == 3) {
        Text(2, cx, cy, WHITE, "STORE");
        Text(1, cx, cy + 90 * S, 0xC8FFFFFF, "Nothing here yet.");
    }

    // top-right clock
    SYSTEMTIME st; GetLocalTime(&st);
    char clock[32];
    if (g_h24) sprintf(clock, "%02d:%02d", st.wHour, st.wMinute);
    else sprintf(clock, "%d:%02d %s", (st.wHour % 12) ? (st.wHour % 12) : 12, st.wMinute, st.wHour >= 12 ? "PM" : "AM");
    float right = W - 50 * S, tw = TextW(2, clock);
    Text(2, right - tw, 30 * S, WHITE, clock);
    float ccx = right - tw - 30 * S, ccy = 30 * S + TextH(2, clock) * 0.5f, r = 13 * S;
    Ring(ccx, ccy, r, 3 * S, kAccents[g_accent]);
    Line(ccx, ccy, ccx, ccy - r * 0.6f, 3 * S, kAccents[g_accent]);
    Line(ccx, ccy, ccx + r * 0.5f, ccy + r * 0.2f, 3 * S, kAccents[g_accent]);

    // hints
    float hx = W - 50 * S, hy = H - 62 * S - 17 * S;
    KeyCap(hx, hy, "ESC", "BACK", 34 * S, true);
    KeyCap(hx, hy, "ENTER", "SELECT", 34 * S, true);
}

static void DrawSettingsPage(bool click, bool down)
{
    DrawBackground(0);
    Grad4(0, 0, W, H, 0xFF762F80, 0xFF8E4196, 0xFF6C2B78, 0xFF5B2166);
    DWORD pc = RGBA(52, 10, 72, 150);
    Palm(W * 0.56f, H + 10 * S, W * 0.60f, H * 0.42f, H * 0.34f, pc);
    Palm(W * 0.76f, H + 10 * S, W * 0.72f, H * 0.30f, H * 0.40f, pc);
    Palm(W * 0.93f, H + 10 * S, W * 0.89f, H * 0.52f, H * 0.28f, pc);

    Text(2, 32 * S, 22 * S, WHITE, "SETTINGS");
    float tx = 32 * S;
    for (int i = 0; i < 2; ++i) {
        float tw = TextW(0, kTabs[i]) + 40 * S, th = 38 * S, ty = 78 * S;
        bool hov = Hit(tx, ty, tw, th, (float)g_mx, (float)g_my);
        if (hov && click) g_stab = i;
        bool act = g_stab == i;
        Rect(tx, ty, tw, th, act ? WHITE : (hov ? 0x5AFFFFFF : 0x32FFFFFF));
        TextC(0, tx, ty, tw, th, act ? INK : WHITE, kTabs[i]);
        tx += tw + 6 * S;
    }
    Text(2, 32 * S, 180 * S, WHITE, kTabs[g_stab]);

    g_hoverRow = -1;
    int row = 0;
    const Setting* shown = nullptr;
    for (int i = 0; i < (int)(sizeof(g_settings) / sizeof(g_settings[0])); ++i) {
        if (g_settings[i].tab != g_stab) continue;
        DrawRow(row, g_settings[i], 32 * S, (247 + row * 68) * S, 932 * S, 68 * S, click, down);
        if (g_hoverRow == row) shown = &g_settings[i];
        if (!shown && row == 0) shown = &g_settings[i];
        ++row;
    }
    if (shown) {
        std::string up = shown->label;
        for (char& ch : up) if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        Text(2, 1178 * S, 180 * S, WHITE, up.c_str());
        Rect(1178 * S, 246 * S, 640 * S, 2 * S, 0x5AFFFFFF);
        // simple word wrap
        std::string line, word;
        float y = 268 * S, maxw = 640 * S;
        std::string d = std::string(shown->desc) + " ";
        for (char ch : d) {
            if (ch != ' ') { word += ch; continue; }
            std::string test = line.empty() ? word : line + " " + word;
            if (TextW(1, test.c_str()) > maxw && !line.empty()) {
                Text(1, 1178 * S, y, 0xE6FFFFFF, line.c_str());
                y += 40 * S; line = word;
            } else line = test;
            word.clear();
        }
        if (!line.empty()) Text(1, 1178 * S, y, 0xE6FFFFFF, line.c_str());
    }

    char pg[16]; sprintf(pg, "%d / 2", g_stab + 1);
    Text(1, 48 * S, H - 150 * S, WHITE, pg);

    // hints + clickable BACK
    float hx = W - 50 * S, hy = H - 62 * S - 17 * S;
    float bw = TextW(0, "ESC") + 22 * S, lw = TextW(0, "BACK");
    float backX = hx - bw - 12 * S - lw;
    if (click && Hit(backX, hy, bw + 12 * S + lw, 34 * S, (float)g_mx, (float)g_my)) { g_page = 0; g_sel = 1; }
    KeyCap(hx, hy, "ESC", "BACK", 34 * S, true);
    KeyCap(hx, hy, "Q E", "TABS", 34 * S, true);
}

static void DrawCursor()
{
    float x = (float)g_mx, y = (float)g_my, s = 22 * S;
    ModeSolid();
    Vtx v[3] = { { x - 0.5f, y - 0.5f, 0, 1, WHITE, 0, 0 }, { x - 0.5f, y + s - 0.5f, 0, 1, WHITE, 0, 0 }, { x + s * 0.7f - 0.5f, y + s * 0.72f - 0.5f, 0, 1, WHITE, 0, 0 } };
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof(Vtx));
}

static void SendEsc(bool down)
{
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0;
    in.ki.wScan = 0x01;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof(INPUT));
}

static void Frame()
{
    bool active = *kMenuActive == 1;
    if (!active) {
        if (g_wasActive) { g_page = 0; g_sel = 1; g_dragRow = -1; }
        g_wasActive = false;
        g_click = false;
        return;
    }
    g_wasActive = true;

    D3DVIEWPORT9 vp;
    if (FAILED(g_dev->GetViewport(&vp)) || vp.Width < 320) return;
    W = (float)vp.Width; H = (float)vp.Height; S = H / 1080.0f;

    // client -> backbuffer coords
    RECT cr = {};
    GetClientRect(g_hwnd, &cr);
    int cw = std::max<LONG>(1, cr.right), ch = std::max<LONG>(1, cr.bottom);
    int mx = (int)(g_mx * W / cw), my = (int)(g_my * H / ch);
    int rawX = g_mx, rawY = g_my;
    g_mx = mx; g_my = my;

    bool click = g_click, down = g_down, keyQ = g_keyQ, keyE = g_keyE;
    g_click = g_keyQ = g_keyE = false;
    if (g_page == 1) {
        if (keyQ || keyE) g_stab = (g_stab + 1) % 2;
    }

    EnsureFonts();
    IDirect3DStateBlock9* sb = nullptr;
    g_dev->CreateStateBlock(D3DSBT_ALL, &sb);
    SetupState();

    if (g_page == 1) DrawSettingsPage(click, down); else DrawMenuPage(click);
    if (g_showFps) {
        static DWORD last = GetTickCount(); static int frames = 0; static int fps = 0;
        ++frames; DWORD now = GetTickCount();
        if (now - last >= 500) { fps = (int)(frames * 1000 / (now - last)); frames = 0; last = now; }
        char b[24]; sprintf(b, "%d FPS", fps);
        Text(0, 12 * S, 8 * S, 0xFFFFFFFF, b);
    }
    DrawCursor();

    if (sb) { sb->Apply(); sb->Release(); }

    g_mx = rawX; g_my = rawY;

    if (g_escFrames > 0) {
        if (g_escFrames == 4) SendEsc(true);
        if (g_escFrames == 2) SendEsc(false);
        --g_escFrames;
    }
}

// ---------------------------------------------------------------- hooks ---
static HRESULT WINAPI HookedEndScene(IDirect3DDevice9* dev)
{
    if (dev == g_dev) {
        IDirect3DSurface9 *rt = nullptr, *bb = nullptr;
        dev->GetRenderTarget(0, &rt);
        dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
        bool onBack = rt && rt == bb;
        if (rt) rt->Release();
        if (bb) bb->Release();
        if (onBack) Frame();
    }
    return g_origEndScene(dev);
}

static LRESULT CALLBACK HookedWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_MOUSEMOVE: g_mx = (short)LOWORD(l); g_my = (short)HIWORD(l); break;
    case WM_LBUTTONDOWN: g_down = true; g_click = true; g_mx = (short)LOWORD(l); g_my = (short)HIWORD(l); break;
    case WM_LBUTTONUP: g_down = false; break;
    case WM_KEYDOWN: if (w == 'Q') g_keyQ = true; if (w == 'E') g_keyE = true; break;
    default: break;
    }
    return CallWindowProcW(g_oldProc, h, m, w, l);
}

static DWORD WINAPI InitThread(LPVOID)
{
    for (;;) {
        IDirect3DDevice9* d = nullptr;
        if (!IsBadReadPtr(kGameDevicePtr, sizeof(void*))) d = *kGameDevicePtr;
        if (d) { g_dev = d; break; }
        Sleep(200);
    }
    Sleep(1500);

    D3DDEVICE_CREATION_PARAMETERS cp = {};
    if (SUCCEEDED(g_dev->GetCreationParameters(&cp))) g_hwnd = cp.hFocusWindow;
    if (!g_hwnd) g_hwnd = GetForegroundWindow();

    LoadIni();

    void** vtbl = *reinterpret_cast<void***>(g_dev);
    DWORD old = 0;
    if (VirtualProtect(&vtbl[42], sizeof(void*), PAGE_READWRITE, &old)) {
        g_origEndScene = reinterpret_cast<EndScene_t>(vtbl[42]);
        vtbl[42] = reinterpret_cast<void*>(&HookedEndScene);
        VirtualProtect(&vtbl[42], sizeof(void*), old, &old);
    }
    if (g_hwnd) g_oldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));
    return 0;
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
