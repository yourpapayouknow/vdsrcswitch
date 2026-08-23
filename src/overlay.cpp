/*
 * overlay.cpp  —  vdsrcswitch
 * 使用 GDI+ 抗锯齿引擎实现极其圆滑的圆角矩形与高质量字体渲染
 */

#include "overlay.h"
#include "config.h"
#include <windows.h>
#include <gdiplus.h>
#include <wchar.h>
#include <stdio.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */
#define COL_ORANGE      Color(255, 255, 140, 0)
#define COL_CYAN        Color(255, 0, 220, 220)
#define COL_BLACK       Color(255, 0, 0, 0)

#define BOX_W           420
#define BOX_H           120
#define CORNER_R        14
#define BORDER_W         4
#define PADDING_X       24
#define PADDING_Y       14

/* -------------------------------------------------------------------------
 * Module-level State
 * ------------------------------------------------------------------------- */
static HWND      s_hwnd         = NULL;
static int       s_screen_w     = 0;
static int       s_screen_h     = 0;
static ULONG_PTR s_gdiplusToken = 0;

extern "C" {
    void log_write(const char* msg);
}

/* -------------------------------------------------------------------------
 * 获取显示器 DPI
 * ------------------------------------------------------------------------- */
static int get_monitor_dpi(HWND hwnd)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef UINT (WINAPI *GetDpiForWindowProc)(HWND);
        GetDpiForWindowProc getDpi = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindowProc");
        if (getDpi) {
            return (int)getDpi(hwnd);
        }
    }
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return dpi;
}

/* -------------------------------------------------------------------------
 * 构造 GDI+ 圆角矩形路径
 * ------------------------------------------------------------------------- */
static void get_rounded_rect_path(GraphicsPath& path, Rect rect, int radius)
{
    int diameter = radius * 2;
    if (diameter > rect.Width)  diameter = rect.Width;
    if (diameter > rect.Height) diameter = rect.Height;

    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}

/* -------------------------------------------------------------------------
 * 窗口过程
 * ------------------------------------------------------------------------- */
static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd; (void)wp; (void)lp;
    if (msg == WM_DESTROY) return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* -------------------------------------------------------------------------
 * 公开接口
 * ------------------------------------------------------------------------- */
BOOL overlay_create(HINSTANCE hInstance)
{
    /* 初始化 GDI+ */
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&s_gdiplusToken, &gdiplusStartupInput, NULL);

    /* 注册窗口类 */
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"VDSSOverlay";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    s_screen_w = GetSystemMetrics(SM_CXSCREEN);
    s_screen_h = GetSystemMetrics(SM_CYSCREEN);

    /* 创建全屏透明 Overlay 窗口 */
    s_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        L"VDSSOverlay", L"",
        WS_POPUP,
        0, 0, s_screen_w, s_screen_h,
        NULL, NULL, hInstance, NULL
    );
    if (!s_hwnd) return FALSE;

    return TRUE;
}

static void update_layered(HDC hdc_mem)
{
    HDC hdc_screen = GetDC(NULL);

    BLENDFUNCTION bf;
    bf.BlendOp             = AC_SRC_OVER;
    bf.BlendFlags          = 0;
    bf.SourceConstantAlpha = g_config.overlay_opacity;
    bf.AlphaFormat         = AC_SRC_ALPHA;

    POINT pt_src  = {0, 0};
    POINT pt_dst  = {0, 0};
    SIZE  sz      = {s_screen_w, s_screen_h};

    BOOL ok = UpdateLayeredWindow(s_hwnd, hdc_screen, &pt_dst, &sz,
                                  hdc_mem, &pt_src, 0, &bf, ULW_ALPHA);
    if (!ok) {
        char buf[80];
        sprintf_s(buf, sizeof(buf), "UpdateLayeredWindow FAILED err=%lu", GetLastError());
        log_write(buf);
    }

    ReleaseDC(NULL, hdc_screen);
}

void overlay_show(const WCHAR* current_name, const WCHAR* target_name)
{
    if (!s_hwnd) return;

    /* 动态重新获取屏幕尺寸与 DPI，支持热插拔与分辨率调整 */
    s_screen_w = GetSystemMetrics(SM_CXSCREEN);
    s_screen_h = GetSystemMetrics(SM_CYSCREEN);

    int dpi = get_monitor_dpi(s_hwnd);
    double scale = (double)dpi / 96.0;

    int box_w    = (int)(BOX_W * scale);
    int box_h    = (int)(BOX_H * scale);
    int corner_r = (int)(CORNER_R * scale);
    int border_w = (int)(BORDER_W * scale);
    int pad_x    = (int)(PADDING_X * scale);
    int pad_y    = (int)(PADDING_Y * scale);

    HDC hdc_screen = GetDC(NULL);
    HDC hdc_mem    = CreateCompatibleDC(hdc_screen);

    BITMAPINFOHEADER bih = {0};
    bih.biSize        = sizeof(bih);
    bih.biWidth       = s_screen_w;
    bih.biHeight      = -s_screen_h;
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;

    void* dib_bits = NULL;
    BITMAPINFO bi  = {0};
    bi.bmiHeader   = bih;
    HBITMAP hbm    = CreateDIBSection(hdc_screen, &bi, DIB_RGB_COLORS, &dib_bits, NULL, 0);
    HBITMAP hbm_old = (HBITMAP)SelectObject(hdc_mem, hbm);

    memset(dib_bits, 0, (size_t)s_screen_w * s_screen_h * 4);

    int box_x = (s_screen_w - box_w) / 2;
    int box_y = (s_screen_h - box_h) / 2;

    /* -----------------------------------------------------------------------
     * 使用 GDI+ 抗锯齿进行统一绘制（背景框 + 文本）
     * --------------------------------------------------------------------- */
    {
        Graphics graphics(hdc_mem);
        
        /* 1. 圆角边缘抗锯齿 */
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);

        /* 2. 文本高清晰度 ClearType 渲染提示 */
        graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        Rect rect(box_x, box_y, box_w, box_h);
        GraphicsPath path;
        get_rounded_rect_path(path, rect, corner_r);

        /* 填充主体（橙色） */
        SolidBrush orangeBrush(COL_ORANGE);
        graphics.FillPath(&orangeBrush, &path);

        /* 描边（青色），使用 Inset 对齐方式确保线宽完全落在框内 */
        Pen cyanPen(COL_CYAN, (REAL)border_w);
        cyanPen.SetAlignment(PenAlignmentInset);
        graphics.DrawPath(&cyanPen, &path);

        /* -------------------------------------------------------------------
         * 绘制文字（GDI+ 模式）
         * ------------------------------------------------------------------- */
        FontFamily fontFamily(L"HONOR Sans Design");
        Font fontSm(&fontFamily, (REAL)(20 * scale), FontStyleBold, UnitPixel);
        Font fontLg(&fontFamily, (REAL)(32 * scale), FontStyleBold, UnitPixel);

        SolidBrush textBrush(COL_BLACK);
        StringFormat stringFormat;
        stringFormat.SetAlignment(StringAlignmentCenter);
        stringFormat.SetLineAlignment(StringAlignmentCenter);

        /* 第一行：当前源 */
        WCHAR line1[128];
        _snwprintf_s(line1, 128, _TRUNCATE, L"当前: %s", current_name ? current_name : L"?");
        RectF r1((REAL)(box_x + pad_x), (REAL)(box_y + pad_y),
                 (REAL)(box_w - pad_x * 2), (REAL)(box_h / 2.0 - pad_y));
        graphics.DrawString(line1, -1, &fontSm, r1, &stringFormat, &textBrush);

        /* 第二行：目标源 */
        WCHAR line2[128];
        if (target_name && wcscmp(target_name, current_name ? current_name : L"") != 0) {
            _snwprintf_s(line2, 128, _TRUNCATE, L"→  %s", target_name);
        } else {
            _snwprintf_s(line2, 128, _TRUNCATE, L"%s", current_name ? current_name : L"?");
        }
        RectF r2((REAL)(box_x + pad_x), (REAL)(box_y + box_h / 2.0),
                 (REAL)(box_w - pad_x * 2), (REAL)(box_h / 2.0 - pad_y));
        graphics.DrawString(line2, -1, &fontLg, r2, &stringFormat, &textBrush);
    }

    /* -----------------------------------------------------------------------
     * 推送 layered 窗口
     * --------------------------------------------------------------------- */
    ShowWindow(s_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(s_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    update_layered(hdc_mem);

    /* 清理 */
    SelectObject(hdc_mem, hbm_old);
    DeleteObject(hbm);
    DeleteDC(hdc_mem);
    ReleaseDC(NULL, hdc_screen);
}

void overlay_hide(void)
{
    if (s_hwnd) {
        ShowWindow(s_hwnd, SW_HIDE);
    }
}

void overlay_destroy(void)
{
    overlay_hide();
    if (s_hwnd)    { DestroyWindow(s_hwnd);   s_hwnd    = NULL; }

    /* 关闭 GDI+ */
    if (s_gdiplusToken) {
        GdiplusShutdown(s_gdiplusToken);
        s_gdiplusToken = 0;
    }
}
