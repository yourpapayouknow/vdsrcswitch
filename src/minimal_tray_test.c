// minimal_tray_test.c — standalone tray icon test, no other modules
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

#define WM_TRAY (WM_USER + 1)
#define TRAY_ID 1

static HWND g_hwnd = NULL;
static FILE* g_log = NULL;

static void log_w(const char* s) {
    if (g_log) { fprintf(g_log, "%s\n", s); fflush(g_log); }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_APP) {
        // Try adding tray icon
        NOTIFYICONDATAW nid = {sizeof(nid)};
        nid.hWnd = hwnd;
        nid.uID  = TRAY_ID;
        nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
        nid.uCallbackMessage = WM_TRAY;
        nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        wcsncpy_s(nid.szTip, ARRAYSIZE(nid.szTip), L"TrayTest", _TRUNCATE);
        BOOL ok = Shell_NotifyIconW(NIM_ADD, &nid);
        char buf[128];
        sprintf_s(buf, sizeof(buf), "WM_APP: NIM_ADD=%d err=%lu hwnd=%p IsWindow=%d",
            ok, GetLastError(), (void*)hwnd, IsWindow(hwnd));
        log_w(buf);
        if (ok) {
            nid.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &nid);
            log_w("NIM_SETVERSION done");
        }
        // Auto-exit after 2s to check result
        SetTimer(hwnd, 1, 2000, NULL);
    }
    if (msg == WM_TIMER) { PostQuitMessage(0); }
    if (msg == WM_DESTROY) { PostQuitMessage(0); }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE h2, LPWSTR cmd, int show) {
    (void)h2; (void)cmd; (void)show;

    WCHAR tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    WCHAR logp[MAX_PATH];
    _snwprintf_s(logp, MAX_PATH, _TRUNCATE, L"%stray_test.log", tmp);
    _wfopen_s(&g_log, logp, L"w");
    log_w("=== tray_test start ===");

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = L"TrayTestClass";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, L"TrayTestClass", L"TrayTest",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 300, 200,
        NULL, NULL, hInst, NULL);

    char buf[64];
    sprintf_s(buf, sizeof(buf), "hwnd=%p IsWindow=%d", (void*)g_hwnd, IsWindow(g_hwnd));
    log_w(buf);

    ShowWindow(g_hwnd, SW_SHOW);
    PostMessageW(g_hwnd, WM_APP, 0, 0);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    NOTIFYICONDATAW nid2 = {sizeof(nid2)};
    nid2.hWnd = g_hwnd; nid2.uID = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid2);
    log_w("done");
    if (g_log) fclose(g_log);
    return 0;
}
