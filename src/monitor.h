/*
 * monitor.h  —  vdsrcswitch
 * DDC/CI 显示器输入源控制封装
 */
#pragma once
#ifndef VDSS_MONITOR_H
#define VDSS_MONITOR_H

#include <windows.h>

#define VDSS_MAX_MONITORS 8

/* 单台物理显示器的运行时状态 */
typedef struct {
    HMONITOR hMonitor;              /* 逻辑显示器句柄 */
    WCHAR    description[128];      /* 显示器描述字符串 */
    DWORD    current_input;         /* 当前 VCP 0x60 值（运行时读取） */
    int      input_count;           /* 探测到的可用输入源数量 */
    DWORD    inputs[16];            /* 可用输入源 VCP 数值列表 */
    int      selected_index;        /* 当前在 overlay 中选中的 index */
} MonitorInfo;

#ifdef __cplusplus
extern "C" {
#endif

/* 全局显示器列表 */
extern MonitorInfo g_monitors[VDSS_MAX_MONITORS];
extern int         g_monitor_count;

/**
 * 枚举所有物理显示器，读取当前输入源（VCP 0x60），
 * 并尝试解析 capabilities string 获取可用输入列表。
 * 结果写入 g_monitors / g_monitor_count 以及 g_config。
 */
void monitor_enumerate(void);

/**
 * 读取指定显示器的当前输入源值（VCP 0x60）。
 * 返回 TRUE 成功，并将值写入 *out_value。
 */
BOOL monitor_get_input(int monitor_idx, DWORD* out_value);

/**
 * 将指定显示器的输入源设置为 value（VCP 0x60）。
 * 返回 TRUE 成功。
 */
BOOL monitor_set_input(int monitor_idx, DWORD value);

/**
 * 循环：在当前显示器的 inputs[] 列表中将 selected_index 前进一步。
 * 如果 inputs 列表为空，则在当前值基础上 ±1 步进（fallback 模式）。
 * 返回选中的 VCP 数值（供 overlay 显示）。
 */
DWORD monitor_cycle_next(int monitor_idx);

/**
 * 对所有显示器提交当前 selected_index 对应的输入源切换。
 * 在主线程收到 WM_VDSS_COMMIT 后调用。
 */
void monitor_commit_all(void);

#ifdef __cplusplus
}
#endif

#endif /* VDSS_MONITOR_H */
