/*
 * main.c  —  vdsrcswitch
 * 后台守护进程模式：静默在后台，通过 Shift+V+Tab 触发输入源切换。
 * 取消了系统托盘，避免托盘创建失败的兼容性问题。
 */

#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <stdbool.h>

#include "config.h"
#include "monitor.h"
#include "hook.h"
#include "overlay.h"
#include "wsserver.h"

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */
#define APP_NAME        L"vdsrcswitch"
#define MUTEX_NAME      L"Local\\vdsrcswitch_single_instance"

/* -------------------------------------------------------------------------
 * Debug log (written next to exe: vdsrcswitch_debug.log)
 * ------------------------------------------------------------------------- */
static FILE* s_log = NULL;

#include <share.h>

static void log_open(void)
{
    WCHAR tmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmp);
    WCHAR log_path[MAX_PATH];
    _snwprintf_s(log_path, MAX_PATH, _TRUNCATE, L"%svdsrcswitch_debug.log", tmp);
    s_log = _wfsopen(log_path, L"w", _SH_DENYWR);
}

void log_write(const char* msg)
{
    if (!s_log) return;
    fprintf(s_log, "%s\n", msg);
    fflush(s_log);
}

static void log_close(void) { if (s_log) { fclose(s_log); s_log = NULL; } }

/* -------------------------------------------------------------------------
 * Module-level state
 * ------------------------------------------------------------------------- */
static HWND      s_hwnd_main  = NULL;
static HANDLE    s_mutex      = NULL;
static bool      s_overlay_active = false;

/* -------------------------------------------------------------------------
 * DDC/CI Worker Thread
 * All blocking DDC/CI calls (50-300ms each) are offloaded here so the
 * main message loop — and thus WH_KEYBOARD_LL — never stalls.
 * Windows silently unhooks WH_KEYBOARD_LL if the hook proc or its message
 * loop takes longer than ~300ms, which is exactly how long DDC/CI takes.
 * ------------------------------------------------------------------------- */
typedef enum {
    DDCCI_CMD_NONE = 0,
    DDCCI_CMD_SET_INPUT,      /* set monitor idx to value */
    DDCCI_CMD_COMMIT_ALL,     /* commit all selected_index values */
    DDCCI_CMD_GET_AND_SHOW,   /* read current inputs then post CYCLE_NEXT(init) */
    DDCCI_CMD_QUIT
} DdcciCmdType;

typedef struct {
    DdcciCmdType type;
    int          monitor_idx;
    DWORD        value;
} DdcciCmd;

#define DDCCI_QUEUE_SIZE 16

static DdcciCmd  s_ddcci_queue[DDCCI_QUEUE_SIZE];
static int       s_ddcci_head   = 0;
static int       s_ddcci_tail   = 0;
static HANDLE    s_ddcci_sem    = NULL;  /* signals worker there is work */
static HANDLE    s_ddcci_mutex  = NULL;  /* protects queue */
static HANDLE    s_ddcci_thread = NULL;
static HWND      s_ddcci_hwnd   = NULL;  /* main hwnd for post-work broadcast */

static void ddcci_enqueue(DdcciCmd cmd)
{
    WaitForSingleObject(s_ddcci_mutex, INFINITE);
    int next = (s_ddcci_tail + 1) % DDCCI_QUEUE_SIZE;
    if (next != s_ddcci_head) { /* not full */
        s_ddcci_queue[s_ddcci_tail] = cmd;
        s_ddcci_tail = next;
    }
    ReleaseMutex(s_ddcci_mutex);
    ReleaseSemaphore(s_ddcci_sem, 1, NULL);
}

/* Custom message: worker finished, main thread should broadcast WS state */
#define WM_VDSS_DDCCI_DONE (WM_USER + 20)

static DWORD WINAPI ddcci_worker_proc(LPVOID lpParam)
{
    (void)lpParam;
    log_write("ddcci_worker: thread started");
    for (;;) {
        WaitForSingleObject(s_ddcci_sem, INFINITE);

        WaitForSingleObject(s_ddcci_mutex, INFINITE);
        if (s_ddcci_head == s_ddcci_tail) {
            ReleaseMutex(s_ddcci_mutex);
            continue;
        }
        DdcciCmd cmd = s_ddcci_queue[s_ddcci_head];
        s_ddcci_head = (s_ddcci_head + 1) % DDCCI_QUEUE_SIZE;
        ReleaseMutex(s_ddcci_mutex);

        if (cmd.type == DDCCI_CMD_QUIT) break;

        if (cmd.type == DDCCI_CMD_SET_INPUT) {
            char buf[128];
            sprintf_s(buf, sizeof(buf), "ddcci_worker: SET_INPUT monitor=%d value=%lu", cmd.monitor_idx, cmd.value);
            log_write(buf);
            if (cmd.monitor_idx >= 0 && cmd.monitor_idx < g_monitor_count) {
                monitor_set_input(cmd.monitor_idx, cmd.value);
                /* update selected_index */
                for (int i = 0; i < g_monitors[cmd.monitor_idx].input_count; i++) {
                    if (g_monitors[cmd.monitor_idx].inputs[i] == cmd.value) {
                        g_monitors[cmd.monitor_idx].selected_index = i;
                        break;
                    }
                }
            }
            PostMessageW(s_ddcci_hwnd, WM_VDSS_DDCCI_DONE, 0, 0);
        } else if (cmd.type == DDCCI_CMD_COMMIT_ALL) {
            log_write("ddcci_worker: COMMIT_ALL");
            monitor_commit_all();
            PostMessageW(s_ddcci_hwnd, WM_VDSS_DDCCI_DONE, 0, 0);
        } else if (cmd.type == DDCCI_CMD_GET_AND_SHOW) {
            log_write("ddcci_worker: GET_AND_SHOW (refresh current inputs)");
            for (int i = 0; i < g_monitor_count; i++) {
                DWORD cur = 0;
                if (monitor_get_input(i, &cur)) {
                    g_monitors[i].current_input = cur;
                    for (int j = 0; j < g_monitors[i].input_count; j++) {
                        if (g_monitors[i].inputs[j] == cur) {
                            g_monitors[i].selected_index = j;
                            break;
                        }
                    }
                }
            }
            /* After refresh, post init-overlay message (lp=0xFFFFFFFF = show only) */
            PostMessageW(s_ddcci_hwnd, WM_VDSS_CYCLE_NEXT, 0, (LPARAM)0xFFFFFFFF);
        }
    }
    log_write("ddcci_worker: thread exiting");
    return 0;
}

static BOOL ddcci_worker_start(HWND hwnd)
{
    s_ddcci_hwnd  = hwnd;
    s_ddcci_head  = s_ddcci_tail = 0;
    s_ddcci_sem   = CreateSemaphoreW(NULL, 0, DDCCI_QUEUE_SIZE, NULL);
    s_ddcci_mutex = CreateMutexW(NULL, FALSE, NULL);
    if (!s_ddcci_sem || !s_ddcci_mutex) return FALSE;
    s_ddcci_thread = CreateThread(NULL, 0, ddcci_worker_proc, NULL, 0, NULL);
    return (s_ddcci_thread != NULL);
}

static void ddcci_worker_stop(void)
{
    if (s_ddcci_thread) {
        DdcciCmd quit = { DDCCI_CMD_QUIT, 0, 0 };
        ddcci_enqueue(quit);
        WaitForSingleObject(s_ddcci_thread, 5000);
        CloseHandle(s_ddcci_thread);
        s_ddcci_thread = NULL;
    }
    if (s_ddcci_sem)   { CloseHandle(s_ddcci_sem);   s_ddcci_sem   = NULL; }
    if (s_ddcci_mutex) { CloseHandle(s_ddcci_mutex); s_ddcci_mutex = NULL; }
}

/* -------------------------------------------------------------------------
 * Autorun / Task Scheduler Elevation Helper
 * ------------------------------------------------------------------------- */
static void run_command_silent(const WCHAR* cmd, const WCHAR* args)
{
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    sei.lpFile = cmd;
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 3000);
            CloseHandle(sei.hProcess);
        }
    }
}

static void autorun_register(void)
{
    WCHAR exe_path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    WCHAR args[MAX_PATH * 2];
    /* Creates a task in Task Scheduler to run with highest privileges on logon without prompting UAC */
    _snwprintf_s(args, ARRAYSIZE(args), _TRUNCATE,
                 L"/create /tn \"vdsrcswitch\" /tr \"\\\"%s\\\"\" /sc onlogon /rl highest /f",
                 exe_path);

    run_command_silent(L"schtasks.exe", args);
}

static void autorun_unregister(void)
{
    run_command_silent(L"schtasks.exe", L"/delete /tn \"vdsrcswitch\" /f");
}

/* -------------------------------------------------------------------------
 * WM_VDSS_CYCLE_NEXT handler
 * lParam == 0xFFFFFFFF  => init overlay (show current, don't cycle) — called
 *                          by ddcci_worker after it finishes GET_AND_SHOW
 * lParam == 0           => cycle to next
 * ------------------------------------------------------------------------- */
static void on_cycle_next(LPARAM lp)
{
    bool init_only = (lp == (LPARAM)0xFFFFFFFF);

    if (!s_overlay_active) {
        if (init_only) {
            /* Worker already refreshed current_input values — just activate */
            s_overlay_active = true;
        } else {
            /* First key activation: kick off a non-blocking DDC/CI read on
             * the worker thread. Worker will post WM_VDSS_CYCLE_NEXT(init)
             * back here when done. Don't show overlay yet. */
            DdcciCmd cmd = { DDCCI_CMD_GET_AND_SHOW, 0, 0 };
            ddcci_enqueue(cmd);
            s_overlay_active = true; /* mark active so we don't re-enqueue */
            return;
        }
    }

    DWORD target_val = 0;
    if (g_monitor_count > 0) {
        if (init_only) {
            int si = g_monitors[0].selected_index;
            if (g_monitors[0].input_count > 0 && si < g_monitors[0].input_count)
                target_val = g_monitors[0].inputs[si];
            else
                target_val = g_monitors[0].current_input;
        } else {
            target_val = monitor_cycle_next(0);
            for (int i = 1; i < g_monitor_count; i++)
                monitor_cycle_next(i);
        }
    }

    const WCHAR* cur_name = (g_monitor_count > 0)
        ? config_get_input_name(0, g_monitors[0].current_input)
        : L"?";
    const WCHAR* tgt_name = (g_monitor_count > 0)
        ? config_get_input_name(0, target_val)
        : L"?";

    overlay_show(cur_name, tgt_name);
}

/* -------------------------------------------------------------------------
 * WM_VDSS_COMMIT — apply the switch (DDC/CI sent from worker thread)
 * ------------------------------------------------------------------------- */
static void on_commit(void)
{
    overlay_hide();
    s_overlay_active = false;
    if (g_monitor_count > 0) {
        DdcciCmd cmd = { DDCCI_CMD_COMMIT_ALL, 0, 0 };
        ddcci_enqueue(cmd);
        /* WS broadcast happens in WM_VDSS_DDCCI_DONE handler */
    }
}

/* -------------------------------------------------------------------------
 * WM_VDSS_CANCEL — discard
 * ------------------------------------------------------------------------- */
static void on_cancel(void)
{
    overlay_hide();
    s_overlay_active = false;
    for (int i = 0; i < g_monitor_count; i++) {
        for (int j = 0; j < g_monitors[i].input_count; j++) {
            if (g_monitors[i].inputs[j] == g_monitors[i].current_input) {
                g_monitors[i].selected_index = j;
                break;
            }
        }
    }
}

#define HOLD_TIMER_ID 101

/* -------------------------------------------------------------------------
 * Main window procedure
 * ------------------------------------------------------------------------- */
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_VDSS_WS_SET_INPUT: {
        int idx = (int)wp;
        DWORD val = (DWORD)lp;
        {
            char log_msg[128];
            sprintf_s(log_msg, sizeof(log_msg), "MainWndProc: WM_VDSS_WS_SET_INPUT idx=%d, val=%lu -> dispatch to worker", idx, val);
            log_write(log_msg);
        }
        if (idx >= 0 && idx < g_monitor_count) {
            DdcciCmd cmd = { DDCCI_CMD_SET_INPUT, idx, val };
            ddcci_enqueue(cmd);
            /* WS broadcast happens in WM_VDSS_DDCCI_DONE */
        }
        return 0;
    }

    case WM_VDSS_DDCCI_DONE:
        /* Worker finished a DDC/CI operation — broadcast updated state to WS clients */
        wsserver_broadcast_state();
        return 0;

    case WM_VDSS_CYCLE_NEXT:
        on_cycle_next(lp);
        return 0;

    case WM_VDSS_COMMIT:
        KillTimer(hwnd, HOLD_TIMER_ID);
        on_commit();
        return 0;

    case WM_VDSS_CANCEL:
        KillTimer(hwnd, HOLD_TIMER_ID);
        on_cancel();
        return 0;

    case WM_VDSS_START_TIMING:
        KillTimer(hwnd, HOLD_TIMER_ID);
        SetTimer(hwnd, HOLD_TIMER_ID, g_config.activation_hold_ms, NULL);
        return 0;

    case WM_VDSS_STOP_TIMING:
        KillTimer(hwnd, HOLD_TIMER_ID);
        on_cancel();
        return 0;

    case WM_TIMER:
        if (wp == HOLD_TIMER_ID) {
            KillTimer(hwnd, HOLD_TIMER_ID);
            hook_notify_timer_elapsed();
            /* Dispatch DDC/CI read to worker; worker posts WM_VDSS_CYCLE_NEXT(init) when done */
            DdcciCmd cmd = { DDCCI_CMD_GET_AND_SHOW, 0, 0 };
            ddcci_enqueue(cmd);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

typedef BOOL (WINAPI *SetProcessDpiAwarenessContextProc)(HANDLE);
#define VDSS_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)

static void enable_dpi_awareness(void)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        SetProcessDpiAwarenessContextProc setAware = 
            (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (setAware) {
            setAware(VDSS_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAware();
        }
    }
}

/* -------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance; (void)nCmdShow;

    /* Enable DPI awareness for sharp native fonts and crisp overlay boundaries */
    enable_dpi_awareness();

    /* --uninstall flag */
    if (lpCmdLine && wcsstr(lpCmdLine, L"--uninstall")) {
        autorun_unregister();
        MessageBoxW(NULL, L"vdsrcswitch removed from startup.", APP_NAME,
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    /* Open debug log FIRST */
    log_open();
    log_write("=== vdsrcswitch starting (daemon mode) ===");

    /* Single-instance mutex */
    s_mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    DWORD mutex_err = GetLastError();
    log_write("Mutex created");
    if (mutex_err == ERROR_ALREADY_EXISTS) {
        log_write("Already running - exit");
        log_close();
        if (s_mutex) CloseHandle(s_mutex);
        return 0;
    }
    log_write("Single instance OK");

    /* Autorun */
    autorun_register();
    log_write("Autorun registered");

    /* Config */
    config_init();
    log_write("Config init done");

    /* Monitor enumeration (DDC/CI handshake) */
    log_write("Starting monitor_enumerate...");
    monitor_enumerate();
    log_write("monitor_enumerate done");

    /* Register window class */
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"VDSSMain";
    RegisterClassExW(&wc);
    log_write("Window class registered");

    /* Create hidden message window (WS_POPUP style, offscreen/invisible) */
    s_hwnd_main = CreateWindowExW(0, L"VDSSMain", APP_NAME,
                                  WS_POPUP,
                                  0, 0, 1, 1,
                                  NULL, NULL, hInstance, NULL);
    if (!s_hwnd_main) {
        char buf[64];
        sprintf_s(buf, sizeof(buf), "CreateWindowExW FAILED err=%lu", GetLastError());
        log_write(buf);
        MessageBoxW(NULL, L"Failed to create main window.", APP_NAME, MB_OK | MB_ICONERROR);
        log_close();
        return 1;
    }
    log_write("Main window created");

    /* Start WebSocket/HTTP server */
    if (wsserver_start(s_hwnd_main)) {
        log_write("WebSocket server started successfully");
    } else {
        log_write("WebSocket server failed to start");
    }

    /* DDC/CI worker thread — keeps main message loop unblocked */
    if (ddcci_worker_start(s_hwnd_main)) {
        log_write("DDC/CI worker thread started");
    } else {
        log_write("DDC/CI worker thread FAILED to start");
    }

    /* Overlay window */
    if (!overlay_create(hInstance)) {
        log_write("overlay_create FAILED");
        MessageBoxW(NULL, L"Failed to create overlay window.", APP_NAME, MB_OK | MB_ICONERROR);
        log_close();
        return 1;
    }
    log_write("Overlay created");

    /* Keyboard hook */
    if (!hook_install(s_hwnd_main)) {
        char buf[64];
        sprintf_s(buf, sizeof(buf), "hook_install FAILED err=%lu", GetLastError());
        log_write(buf);
        MessageBoxW(NULL, L"Failed to install keyboard hook.\nTry running as Administrator.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        log_close();
        return 1;
    }
    log_write("Hook installed");

    log_write("Entering message loop");

    /* Message loop */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* Cleanup */
    log_write("Exiting");
    ddcci_worker_stop();
    wsserver_stop();
    hook_uninstall();
    overlay_destroy();
    if (s_mutex) { ReleaseMutex(s_mutex); CloseHandle(s_mutex); }
    log_close();

    return (int)msg.wParam;
}
