/*
 * hook.c  —  vdsrcswitch
 * WH_KEYBOARD_LL 优化实现
 */

#include "hook.h"
#include "config.h"
#include <stdbool.h>
#include <stdio.h>

/* 外部调试日志接口 */
extern void log_write(const char* msg);

typedef enum {
    MODE_IDLE = 0,
    MODE_TIMING,    /* Shift+V 已被按下，等待长按阈值 (300ms) */
    MODE_ACTIVE,    /* 悬浮窗已拉起，可以按 Tab 循环 */
    MODE_SELECTING, /* 至少按过一次 Tab 进行了切换 */
    MODE_CANCELLING /* 用户中途松开了 Shift/V，但 Tab 还没松 */
} ModeState;

static HHOOK     s_hook       = NULL;
static HWND      s_hwnd_main  = NULL;
static ModeState s_mode       = MODE_IDLE;

static bool s_shift_down = false;
static bool s_v_down     = false;
static bool s_tab_down   = false;

static void reset_hook_state(void)
{
    s_mode = MODE_IDLE;
}

static bool all_keys_released(void)
{
    return !s_shift_down && !s_v_down && !s_tab_down;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0)
        return CallNextHookEx(s_hook, nCode, wParam, lParam);

    KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
    DWORD vk = kb->vkCode;
    bool is_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool is_up   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

    bool is_shift = (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT);
    bool is_v     = (vk == 'V');
    bool is_tab   = (vk == VK_TAB);

    /* 原始调试日志：无任何过滤，抓取 Windows 派发的每一条按键消息 */
    if (is_shift || is_v || is_tab) {
        char raw_msg[128];
        sprintf_s(raw_msg, sizeof(raw_msg),
            "RAW Key Event: VK=%02X (%s) | Current State: Shift=%d V=%d Tab=%d | Mode=%d",
            (unsigned)vk, is_down ? "DOWN" : "UP",
            s_shift_down, s_v_down, s_tab_down, s_mode);
        log_write(raw_msg);
    }

    /* 过滤自动重复：如果按键已经是按下状态，说明是系统的自动重复 DOWN 消息 */
    bool is_repeat = false;
    if (is_down) {
        if (is_shift && s_shift_down) is_repeat = true;
        if (is_v && s_v_down)         is_repeat = true;
        if (is_tab && s_tab_down)     is_repeat = true;
    }

    /* 如果都不是我们关心的键，直接放行 */
    if (!is_shift && !is_v && !is_tab) {
        return CallNextHookEx(s_hook, nCode, wParam, lParam);
    }

    /* 更新实时键态（在 repeat 判定之后） */
    if (is_shift) { s_shift_down = is_down; }
    if (is_v)     { s_v_down     = is_down; }
    if (is_tab)   { s_tab_down   = is_down; }

    /* -------------------------------------------------------------------------
     * 核心判定状态机
     * ------------------------------------------------------------------------- */
    if (is_down && !is_repeat) {
        /* 当 Shift 和 V 同时被按下时，尝试启动 300ms 计时判定 */
        if (s_shift_down && s_v_down && s_mode == MODE_IDLE) {
            s_mode = MODE_TIMING;
            log_write("--> Action: Triggered Shift+V hold timer (300ms)");
            PostMessageW(s_hwnd_main, WM_VDSS_START_TIMING, 0, 0);
        }

        /* 如果处于激活或选择模式，且按下 Tab 键 */
        if (is_tab && (s_mode == MODE_ACTIVE || s_mode == MODE_SELECTING)) {
            s_mode = MODE_SELECTING;
            log_write("Cycle requested via Tab DOWN");
            PostMessageW(s_hwnd_main, WM_VDSS_CYCLE_NEXT, 0, 0);
            return 1; /* 吞掉 Tab 键，防焦点丢失 */
        }
    }
    else if (is_up) {
        /* 如果长按判定期间松开了 Shift 或 V */
        if ((is_shift || is_v) && s_mode == MODE_TIMING) {
            log_write("Cancel: Shift or V released during timer. Stop timer.");
            s_mode = MODE_IDLE;
            PostMessageW(s_hwnd_main, WM_VDSS_STOP_TIMING, 0, 0);
        }
        /* 如果在没有按过 Tab 的激活阶段松开了 Shift 或 V */
        else if ((is_shift || is_v) && s_mode == MODE_ACTIVE) {
            log_write("Cancel: Shift/V released without selection. Closing overlay.");
            s_mode = MODE_IDLE;
            PostMessageW(s_hwnd_main, WM_VDSS_CANCEL, 0, 0);
        }

        /* 关键点：每次有按键抬起时检查是否三键全松 */
        if (all_keys_released()) {
            if (s_mode == MODE_SELECTING) {
                log_write("Commit: All keys released! Writing target source.");
                s_mode = MODE_IDLE;
                PostMessageW(s_hwnd_main, WM_VDSS_COMMIT, 0, 0);
            } 
            else if (s_mode == MODE_ACTIVE) {
                log_write("Cancel: All keys released from active state.");
                s_mode = MODE_IDLE;
                PostMessageW(s_hwnd_main, WM_VDSS_CANCEL, 0, 0);
            }
        }
    }

    /* -------------------------------------------------------------------------
     * 行为阻断机制
     * 只要当前处于长按判定或激活模式，就吞掉 Shift/V/Tab 键以防打扰其他应用程序。
     * ------------------------------------------------------------------------- */
    if (s_mode >= MODE_TIMING) {
        if (is_shift || is_v || is_tab) {
            return 1;
        }
    }

    return CallNextHookEx(s_hook, nCode, wParam, lParam);
}

/* 外部接口，供 main.c 在定时器到期时提升状态为 ACTIVE */
void hook_notify_timer_elapsed(void)
{
    if (s_mode == MODE_TIMING) {
        s_mode = MODE_ACTIVE;
        log_write("Timer elapsed! Mode is now ACTIVE.");
    }
}

BOOL hook_install(HWND hwnd_main)
{
    s_hwnd_main = hwnd_main;
    s_mode = MODE_IDLE;
    s_shift_down = s_v_down = s_tab_down = false;

    s_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    {
        char buf[128];
        sprintf_s(buf, sizeof(buf), "hook_install: s_hook=%p, LastError=%lu", (void*)s_hook, GetLastError());
        log_write(buf);
    }
    return (s_hook != NULL);
}

void hook_uninstall(void)
{
    if (s_hook) {
        UnhookWindowsHookEx(s_hook);
        s_hook = NULL;
    }
}
