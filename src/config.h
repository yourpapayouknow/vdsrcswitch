/*
 * config.h  —  vdsrcswitch
 * INI 配置文件读写：输入源名称/数值映射 + 运行时参数
 */
#pragma once
#ifndef VDSS_CONFIG_H
#define VDSS_CONFIG_H

#include <windows.h>

/* Max input sources per monitor */
#define MAX_INPUTS_PER_MONITOR  16
/* Max physical monitors (renamed from MAX_MONITORS to avoid ddeml.h collision) */
#define VDSS_MAX_MONITORS_CFG   8

/*---------------------------------------------------------------------------
 * 单条输入源描述
 *---------------------------------------------------------------------------*/
typedef struct {
    DWORD  value;           /* VCP 0x60 的原始数值，如 0x11 = 17 */
    WCHAR  name[64];        /* 显示名称，如 L"HDMI 1" */
} InputSource;

/*---------------------------------------------------------------------------
 * 单台显示器的输入源配置
 *---------------------------------------------------------------------------*/
typedef struct {
    WCHAR        monitor_name[128]; /* 显示器描述字符串（来自 PHYSICAL_MONITOR） */
    int          input_count;
    InputSource  inputs[MAX_INPUTS_PER_MONITOR];
} MonitorConfig;

/*---------------------------------------------------------------------------
 * 全局程序配置
 *---------------------------------------------------------------------------*/
typedef struct {
    /* 热键行为 */
    DWORD  activation_hold_ms;  /* V 键按住多少 ms 后激活切换模式，默认 300 */
    BYTE   overlay_opacity;     /* 覆盖层不透明度 0-255，默认 230 */

    /* 显示器配置数组 */
    int          monitor_count;
    MonitorConfig monitors[VDSS_MAX_MONITORS_CFG];

    /* INI 文件路径（运行时填充） */
    WCHAR  ini_path[MAX_PATH];
} AppConfig;

#ifdef __cplusplus
extern "C" {
#endif

/* 全局配置实例（定义在 config.c） */
extern AppConfig g_config;

/*---------------------------------------------------------------------------
 * 公开接口
 *---------------------------------------------------------------------------*/

/**
 * 初始化：确定 INI 路径（exe 同目录），加载现有配置，
 * 如果文件不存在则写入默认值。
 */
void config_init(void);

/**
 * 将 g_config 序列化回 INI 文件。
 * 在自动探测到新输入源后调用，以便用户可以编辑名称。
 */
void config_save(void);

/**
 * 在第 monitor_idx 台显示器的配置中，
 * 根据 VCP 数值 value 查找输入源名称。
 * 若未找到，返回 L"Input <value>"。
 */
const WCHAR* config_get_input_name(int monitor_idx, DWORD value);

/**
 * 将探测到的输入源列表合并写入配置（只新增不存在的条目）。
 * 在 monitor_enumerate() 之后调用。
 */
void config_merge_inputs(int monitor_idx, const WCHAR* monitor_name,
                         const DWORD* values, int count);

#ifdef __cplusplus
}
#endif

#endif /* VDSS_CONFIG_H */
