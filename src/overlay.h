/*
 * overlay.h  —  vdsrcswitch
 * 屏幕中心高对比度覆盖层：显示当前/目标输入源名称
 */
#pragma once
#ifndef VDSS_OVERLAY_H
#define VDSS_OVERLAY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 创建覆盖层窗口（WS_EX_LAYERED | TOPMOST | TRANSPARENT | TOOLWINDOW）。
 * 必须在主消息循环启动前调用。
 */
BOOL overlay_create(HINSTANCE hInstance);

/**
 * 显示覆盖层，并显示文字：
 *   line1：当前输入源名（如 "当前: HDMI 1"）
 *   line2：目标输入源名（如 "→  DisplayPort 1"），若与 line1 相同则只显示一行
 */
void overlay_show(const WCHAR* current_name, const WCHAR* target_name);

/**
 * 隐藏覆盖层（不销毁窗口，下次可再次 show）。
 */
void overlay_hide(void);

/**
 * 销毁覆盖层窗口（程序退出时调用）。
 */
void overlay_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* VDSS_OVERLAY_H */
