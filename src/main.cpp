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
#include <vector>
#include <fstream>

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
static int g_page = 0;                   // 0 main, 1 MoonLoader
static bool g_stockMode = false;         // stock menu page is being shown (overlay hidden)
static int g_stockReturn = 0;
static bool g_escWas = false;
static bool g_wasActive = false;
static int g_spage = 0;                  // MoonLoader list page
static int g_lastHover = 0;

// config (gta6menu.ini)
static int g_accent = 0;                 // 0..3
static int g_bg = 0;                     // 0..2
static int g_h24 = 1;
static int g_showFps = 0;
static int g_navMap = 2;                 // DOWN presses from "Resume" to reach "Map"
static int g_navOptions = 5;             // DOWN presses from "Resume" to reach "Options"

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
    g_navMap = std::min(12, std::max(0, (int)GetPrivateProfileIntA("stock", "nav_map", 2, p.c_str())));
    g_navOptions = std::min(12, std::max(0, (int)GetPrivateProfileIntA("stock", "nav_options", 5, p.c_str())));
}
static void SaveIni()
{
    const std::string p = IniPath();
    char b[16];
    sprintf(b, "%d", g_accent);  WritePrivateProfileStringA("menu", "accent", b, p.c_str());
    sprintf(b, "%d", g_bg);      WritePrivateProfileStringA("menu", "background", b, p.c_str());
    sprintf(b, "%d", g_h24);     WritePrivateProfileStringA("menu", "clock24", b, p.c_str());
    sprintf(b, "%d", g_showFps); WritePrivateProfileStringA("menu", "showfps", b, p.c_str());
    sprintf(b, "%d", g_navMap);  WritePrivateProfileStringA("stock", "nav_map", b, p.c_str());
    sprintf(b, "%d", g_navOptions); WritePrivateProfileStringA("stock", "nav_options", b, p.c_str());
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

// ------------------------------------------------------------ TXD loader ---
struct TexInfo { IDirect3DTexture9* t; int w, h; };
static std::unordered_map<std::string, TexInfo> g_tex;

static std::string ModFile(const char* name)
{
    std::string p = IniPath();
    return p.substr(0, p.find_last_of("\\/") + 1) + name;
}

// Minimal RenderWare TXD reader: D3D9 platform, uncompressed 32-bit A8R8G8B8, level 0 only.
static void LoadTxd()
{
    std::ifstream f(ModFile("gta6menu.txd"), std::ios::binary);
    if (!f) return;
    std::vector<BYTE> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u32 = [&](size_t o) -> DWORD { DWORD v = 0; if (o + 4 <= d.size()) std::memcpy(&v, &d[o], 4); return v; };
    auto u16 = [&](size_t o) -> int { WORD v = 0; if (o + 2 <= d.size()) std::memcpy(&v, &d[o], 2); return v; };
    if (d.size() < 12 || u32(0) != 0x16) return;
    size_t pos = 12, end = std::min<size_t>(d.size(), (size_t)12 + (size_t)u32(4));
    while (pos + 12 <= end)
    {
        DWORD type = u32(pos), size = u32(pos + 4);
        pos += 12;
        if (pos + size > d.size()) break;
        if (type == 0x15 && size > 12 + 92 && u32(pos) == 0x01)
        {
            size_t s = pos + 12;                       // native struct data
            char name[33] = {};
            std::memcpy(name, &d[s + 8], 32);
            int w = u16(s + 80), h = u16(s + 82);
            int depth = d[s + 84];
            DWORD dataSize = u32(s + 88);
            if (u32(s) == 9 && depth == 32 && w > 0 && h > 0 && dataSize == (DWORD)w * h * 4 && s + 92 + dataSize <= d.size())
            {
                IDirect3DTexture9* tex = nullptr;
                if (SUCCEEDED(g_dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) && tex)
                {
                    D3DLOCKED_RECT lr;
                    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)))
                    {
                        for (int y = 0; y < h; ++y)
                            std::memcpy((BYTE*)lr.pBits + y * lr.Pitch, &d[s + 92 + (size_t)y * w * 4], (size_t)w * 4);
                        tex->UnlockRect(0);
                        g_tex[name] = { tex, w, h };
                    }
                    else tex->Release();
                }
            }
        }
        pos += size;
    }
}
static const TexInfo* FindTex(const char* n) { auto it = g_tex.find(n); return it == g_tex.end() ? nullptr : &it->second; }
static void DrawTex(const char* n, float x, float y, float w, float h, DWORD col = 0xFFFFFFFF)
{
    const TexInfo* t = FindTex(n);
    if (!t) return;
    ModeTex();
    g_dev->SetTexture(0, t->t);
    Quad(x, y, x + w, y + h, col, col, col, col);
    g_dev->SetTexture(0, nullptr);
}

// ------------------------------------------------ keyboard event queue ---
// Lets the menu drive the stock pause menu / MoonLoader hotkeys with real key presses.
struct KeyEv { WORD scan; bool ext; bool down; };
static std::vector<KeyEv> g_evq;
static int g_evDelay = 0;

static void PushKey(WORD scan, bool ext) { g_evq.push_back({ scan, ext, true }); g_evq.push_back({ scan, ext, false }); }
static void PumpKeys()
{
    if (g_evq.empty()) return;
    if (g_evDelay > 0) { --g_evDelay; return; }
    KeyEv e = g_evq.front();
    g_evq.erase(g_evq.begin());
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = e.scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (e.ext ? KEYEVENTF_EXTENDEDKEY : 0) | (e.down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof(INPUT));
    g_evDelay = e.down ? 1 : 3;
}
static void StockResume() { PushKey(0x01, false); }
static void StockGoto(int downs)
{
    for (int i = 0; i < downs; ++i) PushKey(0x50, true);   // DOWN arrow
    PushKey(0x1C, false);                                  // ENTER
    g_stockMode = true;
    g_stockReturn = 0;
}
static void ReloadMoonLoader()
{
    g_evq.push_back({ 0x1D, false, true });                // CTRL down
    PushKey(0x13, false);                                  // R
    g_evq.push_back({ 0x1D, false, false });               // CTRL up
}

// --------------------------------------------------- MoonLoader scripts ---
static std::vector<std::string> g_scripts;
static bool g_moonFound = false;
static void ScanScripts()
{
    g_scripts.clear();
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir(exe);
    dir = dir.substr(0, dir.find_last_of("\\/") + 1) + "moonloader\\";
    const char* patterns[2] = { "*.lua", "*.luac" };
    for (const char* pat : patterns)
    {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((dir + pat).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) g_scripts.push_back(fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(g_scripts.begin(), g_scripts.end());
    g_moonFound = GetModuleHandleA("moonloader.asi") != nullptr;
}

// ------------------------------------------------------------------ ui ---
static bool Hit(float x, float y, float w, float h, float mx, float my) { return mx >= x && mx < x + w && my >= y && my < y + h; }

static void KeyCap(float& x, float y, const char* key, const char* label, float h)
{
    float kw = TextW(0, key) + 22 * S;
    float lw = TextW(0, label);
    float kx = x - kw;
    Rect(kx, y, kw, h, WHITE);
    TextC(0, kx, y, kw, h, INK, key);
    Text(0, kx - 12 * S - lw, y + (h - TextH(0, label)) * 0.5f, WHITE, label);
    x = kx - 12 * S - lw - 36 * S;
}

static void WrapText(float x, float y, float maxw, DWORD col, const char* text)
{
    std::string line, word, d = std::string(text) + " ";
    for (char ch : d) {
        if (ch != ' ') { word += ch; continue; }
        std::string test = line.empty() ? word : line + " " + word;
        if (TextW(1, test.c_str()) > maxw && !line.empty()) {
            Text(1, x, y, col, line.c_str());
            y += 40 * S; line = word;
        } else line = test;
        word.clear();
    }
    if (!line.empty()) Text(1, x, y, col, line.c_str());
}

static void DrawBackground(int style)
{
    static const DWORD tl[3] = { 0xFF2C3E6E, 0xFF10122A, 0xFF3A2A5E };
    static const DWORD tr[3] = { 0xFF3A4C86, 0xFF1A1D40, 0xFF5A3A7E };
    static const DWORD br[3] = { 0xFFB5568C, 0xFF3A2060, 0xFFFF7A5A };
    static const DWORD bl[3] = { 0xFF8A4A92, 0xFF2A1A50, 0xFFC8506E };
    Grad4(0, 0, W, H, tl[style], tr[style], br[style], bl[style]);
}

static void DrawClock()
{
    SYSTEMTIME st; GetLocalTime(&st);
    char clock[32];
    if (g_h24) sprintf(clock, "%02d:%02d", st.wHour, st.wMinute);
    else sprintf(clock, "%d:%02d %s", (st.wHour % 12) ? (st.wHour % 12) : 12, st.wMinute, st.wHour >= 12 ? "PM" : "AM");
    float right = W - 50 * S, tw = TextW(2, clock);
    Text(2, right - tw, 30 * S, WHITE, clock);
    float cx = right - tw - 30 * S, cy = 30 * S + TextH(2, clock) * 0.5f, r = 13 * S;
    Ring(cx, cy, r, 3 * S, kAccents[g_accent]);
    Line(cx, cy, cx, cy - r * 0.6f, 3 * S, kAccents[g_accent]);
    Line(cx, cy, cx + r * 0.5f, cy + r * 0.2f, 3 * S, kAccents[g_accent]);
}

static void DrawMainPage(bool click)
{
    DrawBackground(g_bg);
    float sbw = W * 0.25f;
    Rect(0, 0, sbw, H, 0xFF1B1A2B);

    // logo (from gta6menu.txd)
    if (const TexInfo* lg = FindTex("logo")) {
        float lh = 150 * S, lw = lh * lg->w / lg->h;
        DrawTex("logo", 40 * S, 26 * S, lw, lh);
    } else {
        Text(3, 56 * S, 36 * S, WHITE, "SA");
    }

    static const char* const names[4] = { "Resume", "Map", "Settings", "MoonLoader" };
    static const char* const icons[4] = { "ic_play", "ic_map", "ic_gear", "ic_script" };
    static const char* const descs[4] = {
        "Return to the game.",
        "Open the world map.",
        "Display, audio, controls and the rest of the game options.",
        "Lua scripts found in your moonloader folder, and a one-click reload." };
    int hovered = -1;
    for (int i = 0; i < 4; ++i) {
        float x = 30 * S, y = (214 + i * 58) * S, w = sbw - 60 * S, h = 52 * S;
        bool hov = Hit(x, y, w, h, (float)g_mx, (float)g_my);
        if (hov) { hovered = i; g_lastHover = i; }
        if (hov && click) {
            if (i == 0) StockResume();
            else if (i == 1) StockGoto(g_navMap);
            else if (i == 2) StockGoto(g_navOptions);
            else { g_page = 1; g_spage = 0; ScanScripts(); }
        }
        DWORD fg = hov ? INK : WHITE;
        if (hov) {
            Rect(x, y, w * 0.6f, h, WHITE);
            GradH(x + w * 0.6f, y, w * 0.4f, h, WHITE, 0x46FFFFFF);
        }
        float is = 30 * S;
        DrawTex(icons[i], x + 16 * S, y + (h - is) * 0.5f, is, is, fg);
        Text(1, x + 70 * S, y + (h - TextH(1, names[i])) * 0.5f, fg, names[i]);
    }
    (void)hovered;

    // content: title + description of the last hovered entry
    float cx = sbw + 40 * S;
    Text(2, cx, 150 * S, WHITE, names[g_lastHover]);
    WrapText(cx, 236 * S, W - sbw - 120 * S, 0xDCFFFFFF, descs[g_lastHover]);

    DrawClock();
    float hx = W - 50 * S, hy = H - 62 * S - 17 * S;
    KeyCap(hx, hy, "ESC", "BACK", 34 * S);
    KeyCap(hx, hy, "ENTER", "SELECT", 34 * S);
}

static void DrawScriptsPage(bool click, bool keyQ, bool keyE)
{
    Grad4(0, 0, W, H, 0xFF762F80, 0xFF8E4196, 0xFF6C2B78, 0xFF5B2166);
    DrawTex("palms", W - H * 1.05f, H - H * 0.95f, H, H);

    Text(2, 32 * S, 22 * S, WHITE, "MOONLOADER");
    Text(0, 32 * S, 84 * S, 0xC8FFFFFF, g_moonFound ? "MoonLoader detected" : "MoonLoader not detected (moonloader.asi not loaded)");

    const int perPage = 8;
    int total = (int)g_scripts.size();
    int pages = std::max(1, (total + perPage - 1) / perPage);
    if (keyE) g_spage = (g_spage + 1) % pages;
    if (keyQ) g_spage = (g_spage + pages - 1) % pages;
    g_spage = std::min(g_spage, pages - 1);

    Text(2, 32 * S, 150 * S, WHITE, "SCRIPTS");
    int hoverRow = -2;     // -1 = reload row
    std::string hoverName;
    for (int r = 0; r < perPage + 1; ++r) {
        float x = 32 * S, y = (247 + r * 68) * S, w = 932 * S, h = 68 * S;
        std::string label;
        bool action = (r == 0);
        if (action) label = "Reload all scripts";
        else {
            int idx = g_spage * perPage + (r - 1);
            if (idx >= total) break;
            label = g_scripts[idx];
        }
        bool hov = Hit(x, y, w, h, (float)g_mx, (float)g_my);
        if (hov) { hoverRow = r; hoverName = label; }
        DWORD fg = hov ? INK : WHITE;
        if (hov) {
            Rect(x, y, w * 0.55f, h, WHITE);
            GradH(x + w * 0.55f, y, w * 0.45f, h, WHITE, 0x38FFFFFF);
        } else Rect(x, y + h - 1, w, 1, 0x2EFFFFFF);
        Text(1, x + 22 * S, y + (h - TextH(1, label.c_str())) * 0.5f, fg, label.c_str());
        if (action) {
            if (hov && click) ReloadMoonLoader();
            DrawTex("ic_script", x + w - 50 * S - 34 * S, y + (h - 34 * S) * 0.5f, 34 * S, 34 * S, fg);
        }
    }

    // right panel
    float dx = 1178 * S;
    std::string title = hoverRow >= 0 ? hoverName : "SCRIPTS";
    for (char& ch : title) if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (TextW(2, title.c_str()) > 640 * S) title = title.substr(0, 14) + "...";
    Text(2, dx, 150 * S, WHITE, title.c_str());
    Rect(dx, 216 * S, 640 * S, 2 * S, 0x5AFFFFFF);
    const char* desc = hoverRow == 0 ? "Sends CTRL+R, MoonLoader's default hotkey, to reload every script."
                     : hoverRow > 0 ? "Lua script found in your moonloader folder."
                                    : "Lua scripts found in your moonloader folder.";
    WrapText(dx, 238 * S, 640 * S, 0xE6FFFFFF, desc);
    char b[64]; sprintf(b, "%d script%s found", total, total == 1 ? "" : "s");
    Text(1, dx, 420 * S, 0xC8FFFFFF, b);

    char pg[24]; sprintf(pg, "%d / %d", g_spage + 1, pages);
    Text(1, 48 * S, H - 150 * S, WHITE, pg);

    float hx = W - 50 * S, hy = H - 62 * S - 17 * S;
    float bw = TextW(0, "ESC") + 22 * S, lw = TextW(0, "BACK");
    float backX = hx - bw - 12 * S - lw;
    if (click && Hit(backX, hy, bw + 12 * S + lw, 34 * S, (float)g_mx, (float)g_my)) g_page = 0;
    KeyCap(hx, hy, "ESC", "BACK", 34 * S);
    KeyCap(hx, hy, "Q E", "PAGE", 34 * S);
}

static void DrawCursor()
{
    float x = (float)g_mx, y = (float)g_my, s = 22 * S;
    ModeSolid();
    Vtx sh[3] = { { x + 1.5f, y + 1.5f, 0, 1, 0x80000000, 0, 0 }, { x + 1.5f, y + s + 1.5f, 0, 1, 0x80000000, 0, 0 }, { x + s * 0.7f + 1.5f, y + s * 0.72f + 1.5f, 0, 1, 0x80000000, 0, 0 } };
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, sh, sizeof(Vtx));
    Vtx v[3] = { { x - 0.5f, y - 0.5f, 0, 1, WHITE, 0, 0 }, { x - 0.5f, y + s - 0.5f, 0, 1, WHITE, 0, 0 }, { x + s * 0.7f - 0.5f, y + s * 0.72f - 0.5f, 0, 1, WHITE, 0, 0 } };
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof(Vtx));
}

static void Frame()
{
    bool active = *kMenuActive == 1;
    if (!active) {
        if (g_wasActive) { g_page = 0; g_stockMode = false; g_stockReturn = 0; g_evq.clear(); g_evDelay = 0; }
        g_wasActive = false;
        g_click = false;
        return;
    }
    g_wasActive = true;
    PumpKeys();

    // leaving a stock page with ESC brings our menu back
    bool esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (g_stockMode) {
        if (esc && !g_escWas && g_evq.empty()) g_stockReturn = 14;
        if (g_stockReturn > 0 && --g_stockReturn == 0) g_stockMode = false;
    }
    g_escWas = esc;

    D3DVIEWPORT9 vp;
    if (FAILED(g_dev->GetViewport(&vp)) || vp.Width < 320) return;
    W = (float)vp.Width; H = (float)vp.Height; S = H / 1080.0f;

    RECT cr = {};
    GetClientRect(g_hwnd, &cr);
    int cw = std::max<LONG>(1, cr.right), ch = std::max<LONG>(1, cr.bottom);
    int rawX = g_mx, rawY = g_my;
    g_mx = (int)(rawX * W / cw); g_my = (int)(rawY * H / ch);

    bool click = g_click, keyQ = g_keyQ, keyE = g_keyE;
    g_click = g_keyQ = g_keyE = false;

    EnsureFonts();
    IDirect3DStateBlock9* sb = nullptr;
    g_dev->CreateStateBlock(D3DSBT_ALL, &sb);
    SetupState();

    if (g_stockMode) {
        const char* msg = "Press ESC to return to the GTA 6 menu";
        float tw = TextW(0, msg) + 48 * S, th = 44 * S;
        Rect((W - tw) * 0.5f, H - 90 * S, tw, th, 0xB4141028);
        TextC(0, (W - tw) * 0.5f, H - 90 * S, tw, th, WHITE, msg);
    } else {
        if (g_page == 1) DrawScriptsPage(click, keyQ, keyE); else DrawMainPage(click);
        if (g_showFps) {
            static DWORD last = GetTickCount(); static int frames = 0; static int fps = 0;
            ++frames; DWORD now = GetTickCount();
            if (now - last >= 500) { fps = (int)(frames * 1000 / (now - last)); frames = 0; last = now; }
            char b[24]; sprintf(b, "%d FPS", fps);
            Text(0, 12 * S, 8 * S, WHITE, b);
        }
        DrawCursor();
    }

    if (sb) { sb->Apply(); sb->Release(); }
    g_mx = rawX; g_my = rawY;
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
    LoadTxd();

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
