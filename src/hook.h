/*
 * hook.h  —  vdsrcswitch
 * 全局键盘钩子 + 状态机：检测 Shift+V（持住）+ Tab 循环，
 * 三键全部松开时提交切换。
 */
#pragma once
#ifndef VDSS_HOOK_H
#define VDSS_HOOK_H

#include <windows.h>

/* 自定义窗口消息，由钩子回调 PostMessage 给主窗口 */
#define WM_VDSS_CYCLE_NEXT   (WM_USER + 1)  /* Tab DOWN：循环到下一源 */
#define WM_VDSS_COMMIT       (WM_USER + 2)  /* 三键全抬：提交切换     */
#define WM_VDSS_CANCEL       (WM_USER + 3)  /* Shift/V 先抬（未选）：取消 */
#define WM_VDSS_START_TIMING (WM_USER + 4)  /* 开始判定长按 300ms */
#define WM_VDSS_STOP_TIMING  (WM_USER + 5)  /* 停止判定长按 */

/**
 * 安装全局 WH_KEYBOARD_LL 钩子。
 * hwnd_main：主窗口句柄，用于 PostMessage。
 * 返回 TRUE 成功，FALSE 失败。
 */
BOOL hook_install(HWND hwnd_main);

/**
 * 卸载钩子（程序退出前调用）。
 */
void hook_uninstall(void);

/**
 * 由主线程调用，通知 300ms 计时器到期，状态可升级为激活。
 */
void hook_notify_timer_elapsed(void);

#endif /* VDSS_HOOK_H */
