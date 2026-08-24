/*
 * config.c  —  vdsrcswitch
 * INI 配置文件读写实现
 *
 * INI 格式示例：
 *   [settings]
 *   activation_hold_ms=300
 *   overlay_opacity=230
 *
 *   [monitor_0]
 *   monitor_name=DELL U2720Q
 *   input_count=2
 *   input_0_value=17
 *   input_0_name=HDMI 1
 *   input_1_value=15
 *   input_1_name=DisplayPort 1
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/* 全局配置实例 */
AppConfig g_config = {0};

/* -------------------------------------------------------------------------
 * 辅助宏：读写 INI
 * ------------------------------------------------------------------------- */
#define INI_GET_INT(section, key, def) \
    (int)GetPrivateProfileIntW(section, key, (def), g_config.ini_path)

#define INI_GET_STR(section, key, def, buf, sz) \
    GetPrivateProfileStringW(section, key, def, buf, sz, g_config.ini_path)

#define INI_SET(section, key, value) \
    WritePrivateProfileStringW(section, key, value, g_config.ini_path)

/* -------------------------------------------------------------------------
 * 内部：构造 INI 文件路径（exe 所在目录）
 * ------------------------------------------------------------------------- */
static void build_ini_path(void)
{
    WCHAR exe_path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    /* 找到最后一个反斜杠，截断文件名 */
    WCHAR* last_sep = wcsrchr(exe_path, L'\\');
    if (last_sep) {
        *(last_sep + 1) = L'\0';
    }
    _snwprintf_s(g_config.ini_path, MAX_PATH, _TRUNCATE,
                 L"%svdsrcswitch.ini", exe_path);
}

/* -------------------------------------------------------------------------
 * config_init
 * ------------------------------------------------------------------------- */
void config_init(void)
{
    build_ini_path();

    /* --- [settings] --- */
    g_config.activation_hold_ms = (DWORD)INI_GET_INT(L"settings", L"activation_hold_ms", 300);
    g_config.overlay_opacity    = (BYTE) INI_GET_INT(L"settings", L"overlay_opacity",    230);
    g_config.ws_port            = (int)  INI_GET_INT(L"settings", L"ws_port",            59060);

    /* 范围校验 */
    if (g_config.activation_hold_ms < 50)  g_config.activation_hold_ms = 50;
    if (g_config.activation_hold_ms > 2000) g_config.activation_hold_ms = 2000;
    if (g_config.ws_port <= 0 || g_config.ws_port > 65535) g_config.ws_port = 59060;

    /* --- [monitor_N] --- */
    g_config.monitor_count = INI_GET_INT(L"settings", L"monitor_count", 0);
    if (g_config.monitor_count > VDSS_MAX_MONITORS_CFG) g_config.monitor_count = VDSS_MAX_MONITORS_CFG;

    for (int m = 0; m < g_config.monitor_count; m++) {
        WCHAR section[32];
        _snwprintf_s(section, 32, _TRUNCATE, L"monitor_%d", m);

        MonitorConfig* mc = &g_config.monitors[m];
        INI_GET_STR(section, L"monitor_name", L"", mc->monitor_name, 128);

        mc->input_count = INI_GET_INT(section, L"input_count", 0);
        if (mc->input_count > MAX_INPUTS_PER_MONITOR) mc->input_count = MAX_INPUTS_PER_MONITOR;

        for (int i = 0; i < mc->input_count; i++) {
            WCHAR key[32];
            _snwprintf_s(key, 32, _TRUNCATE, L"input_%d_value", i);
            mc->inputs[i].value = (DWORD)INI_GET_INT(section, key, 0);

            _snwprintf_s(key, 32, _TRUNCATE, L"input_%d_name", i);
            INI_GET_STR(section, key, L"Unknown", mc->inputs[i].name, 64);
        }
    }

    /* 如果 INI 不存在，写入默认值 */
    config_save();
}

/* -------------------------------------------------------------------------
 * config_save
 * ------------------------------------------------------------------------- */
void config_save(void)
{
    WCHAR buf[32];

    /* [settings] */
    _snwprintf_s(buf, 32, _TRUNCATE, L"%lu", g_config.activation_hold_ms);
    INI_SET(L"settings", L"activation_hold_ms", buf);

    _snwprintf_s(buf, 32, _TRUNCATE, L"%u", (unsigned)g_config.overlay_opacity);
    INI_SET(L"settings", L"overlay_opacity", buf);

    _snwprintf_s(buf, 32, _TRUNCATE, L"%d", g_config.ws_port);
    INI_SET(L"settings", L"ws_port", buf);

    _snwprintf_s(buf, 32, _TRUNCATE, L"%d", g_config.monitor_count);
    INI_SET(L"settings", L"monitor_count", buf);

    for (int m = 0; m < g_config.monitor_count; m++) {
        WCHAR section[32];
        _snwprintf_s(section, 32, _TRUNCATE, L"monitor_%d", m);

        MonitorConfig* mc = &g_config.monitors[m];
        INI_SET(section, L"monitor_name", mc->monitor_name);

        _snwprintf_s(buf, 32, _TRUNCATE, L"%d", mc->input_count);
        INI_SET(section, L"input_count", buf);

        for (int i = 0; i < mc->input_count; i++) {
            WCHAR key[32], val[64];

            _snwprintf_s(key, 32, _TRUNCATE, L"input_%d_value", i);
            _snwprintf_s(val, 64, _TRUNCATE, L"%lu", mc->inputs[i].value);
            INI_SET(section, key, val);

            _snwprintf_s(key, 32, _TRUNCATE, L"input_%d_name", i);
            INI_SET(section, key, mc->inputs[i].name);
        }
    }
}

/* -------------------------------------------------------------------------
 * config_get_input_name
 * ------------------------------------------------------------------------- */
static WCHAR s_fallback_name[64];

const WCHAR* config_get_input_name(int monitor_idx, DWORD value)
{
    if (monitor_idx < 0 || monitor_idx >= g_config.monitor_count)
        goto fallback;

    MonitorConfig* mc = &g_config.monitors[monitor_idx];
    for (int i = 0; i < mc->input_count; i++) {
        if (mc->inputs[i].value == value)
            return mc->inputs[i].name;
    }

fallback:
    _snwprintf_s(s_fallback_name, 64, _TRUNCATE, L"Input %lu", value);
    return s_fallback_name;
}

/* -------------------------------------------------------------------------
 * config_merge_inputs
 * 将探测到的输入源合并进配置（不重复添加，不覆盖已有名称）
 * ------------------------------------------------------------------------- */
void config_merge_inputs(int monitor_idx, const WCHAR* monitor_name,
                         const DWORD* values, int count)
{
    /* 确保 monitor_idx 在范围内，必要时扩展数组 */
    while (g_config.monitor_count <= monitor_idx && g_config.monitor_count < VDSS_MAX_MONITORS_CFG) {
        g_config.monitor_count++;
    }
    if (monitor_idx >= VDSS_MAX_MONITORS_CFG) return;

    MonitorConfig* mc = &g_config.monitors[monitor_idx];

    /* 更新显示器名称（如果还没有） */
    if (mc->monitor_name[0] == L'\0' && monitor_name) {
        wcsncpy_s(mc->monitor_name, 128, monitor_name, _TRUNCATE);
    }

    for (int i = 0; i < count; i++) {
        DWORD v = values[i];
        /* 检查是否已存在 */
        BOOL found = FALSE;
        for (int j = 0; j < mc->input_count; j++) {
            if (mc->inputs[j].value == v) { found = TRUE; break; }
        }
        if (!found && mc->input_count < MAX_INPUTS_PER_MONITOR) {
            mc->inputs[mc->input_count].value = v;
            /* 默认名称，用户可在 INI 中手工修改 */
            _snwprintf_s(mc->inputs[mc->input_count].name, 64, _TRUNCATE,
                         L"Input %lu", v);
            mc->input_count++;
        }
    }
}
