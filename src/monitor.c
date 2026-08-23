/*
 * monitor.c  —  vdsrcswitch
 * DDC/CI 显示器输入源控制实现
 *
 * 依赖：dxva2.lib（PhysicalMonitorEnumerationAPI + LowLevelMonitorConfigurationAPI）
 */

#include "monitor.h"
#include "config.h"
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "dxva2.lib")

extern void log_write(const char* msg);

/* -------------------------------------------------------------------------
 * 全局显示器列表
 * ------------------------------------------------------------------------- */
MonitorInfo g_monitors[VDSS_MAX_MONITORS];
int         g_monitor_count = 0;

/* -------------------------------------------------------------------------
 * 内部：解析 capabilities string 中的 VCP 0x60 输入源列表
 *
 * GetCapabilitiesStringLength / CapabilitiesRequestAndCapabilitiesReply 返回如：
 *   "(prot(monitor)type(LCD)...vcpnames(60(11 12 0F 10)...)...)"
 * 我们在其中找 "60(" 再提取括号内的十六进制数值。
 * 返回实际解析到的数量，写入 out_values[]（最多 max_count 个）。
 * ------------------------------------------------------------------------- */
static int parse_vcp60_from_caps(const char* caps, DWORD* out_values, int max_count)
{
    if (!caps || !out_values) return 0;

    /* 找 "60(" */
    const char* p = caps;
    int count = 0;

    while (*p) {
        /* 向前搜索 "60(" */
        if (p[0] == '6' && p[1] == '0' && p[2] == '(') {
            p += 3; /* 跳过 "60(" */
            /* 解析括号内的十六进制数值，直到 ')' */
            while (*p && *p != ')') {
                /* 跳过空格 */
                while (*p == ' ' || *p == '\t') p++;
                if (*p == ')' || *p == '\0') break;
                /* 读一个十六进制数 */
                char hex[8] = {0};
                int hi = 0;
                while ((*p >= '0' && *p <= '9') ||
                       (*p >= 'A' && *p <= 'F') ||
                       (*p >= 'a' && *p <= 'f')) {
                    if (hi < 7) hex[hi++] = *p;
                    p++;
                }
                if (hi > 0 && count < max_count) {
                    out_values[count++] = (DWORD)strtoul(hex, NULL, 16);
                }
            }
            break; /* 只处理第一个 "60(" 段 */
        }
        p++;
    }

    return count;
}

/* -------------------------------------------------------------------------
 * 内部：对单台 HMONITOR 获取物理显示器句柄并执行 DDC/CI 操作
 *
 * 由于每次获取/释放句柄有 DDC/CI 握手开销（约 50-150ms），
 * 我们在需要时临时获取，操作完立即释放。
 * ------------------------------------------------------------------------- */
static BOOL get_physical_monitor(HMONITOR hmon,
                                 PHYSICAL_MONITOR* out_pm)
{
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hmon, &count) || count == 0)
        return FALSE;
    return GetPhysicalMonitorsFromHMONITOR(hmon, 1, out_pm);
}

/* -------------------------------------------------------------------------
 * EnumDisplayMonitors 回调
 * ------------------------------------------------------------------------- */
static BOOL CALLBACK enum_monitor_proc(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lp)
{
    (void)hdc; (void)rect; (void)lp;
    if (g_monitor_count >= VDSS_MAX_MONITORS) return FALSE;

    int idx = g_monitor_count++;
    MonitorInfo* mi = &g_monitors[idx];
    mi->hMonitor       = hmon;
    mi->current_input  = 0;
    mi->input_count    = 0;
    mi->selected_index = 0;

    PHYSICAL_MONITOR pm = {0};
    if (!get_physical_monitor(hmon, &pm)) {
        return TRUE; /* 继续枚举，但该显示器 DDC/CI 不可用 */
    }

    /* 复制描述字符串 */
    wcsncpy_s(mi->description, 128, pm.szPhysicalMonitorDescription, _TRUNCATE);

    /* 读取当前输入源 */
    MC_VCP_CODE_TYPE type = MC_MOMENTARY;
    DWORD cur = 0, max_val = 0;
    if (GetVCPFeatureAndVCPFeatureReply(pm.hPhysicalMonitor, 0x60, &type, &cur, &max_val)) {
        mi->current_input = cur;
    }

    /* 
     * 关键点：如果用户在 INI 中已经定义了输入源（input_count > 0），
     * 我们完全跳过下方的自动探测及合并逻辑，保持 INI 中用户的定制列表不被污染。
     */
    if (idx < g_config.monitor_count && g_config.monitors[idx].input_count > 0) {
        DestroyPhysicalMonitors(1, &pm);
        return TRUE;
    }

    /* 尝试获取 capabilities string 解析支持的输入源列表 */
    DWORD caps_len = 0;
    if (GetCapabilitiesStringLength(pm.hPhysicalMonitor, &caps_len) && caps_len > 0) {
        char* caps_buf = (char*)malloc(caps_len + 1);
        if (caps_buf) {
            memset(caps_buf, 0, caps_len + 1);
            if (CapabilitiesRequestAndCapabilitiesReply(pm.hPhysicalMonitor,
                                                        caps_buf, caps_len)) {
                DWORD parsed[16] = {0};
                int n = parse_vcp60_from_caps(caps_buf, parsed, 16);
                if (n > 0) {
                    mi->input_count = n < 16 ? n : 16;
                    for (int i = 0; i < mi->input_count; i++) {
                        mi->inputs[i] = parsed[i];
                    }
                }
            }
            free(caps_buf);
        }
    }

    /* Fallback：如果 capabilities 解析失败，使用当前值作为唯一已知输入源 */
    if (mi->input_count == 0 && mi->current_input != 0) {
        mi->inputs[0] = mi->current_input;
        mi->input_count = 1;
    }

    /* 将 selected_index 定位到当前输入源 */
    for (int i = 0; i < mi->input_count; i++) {
        if (mi->inputs[i] == mi->current_input) {
            mi->selected_index = i;
            break;
        }
    }

    /* 合并到配置（仅在首次运行无配置时） */
    config_merge_inputs(idx, mi->description, mi->inputs, mi->input_count);

    DestroyPhysicalMonitors(1, &pm);
    return TRUE;
}

/* -------------------------------------------------------------------------
 * 公开接口
 * ------------------------------------------------------------------------- */
void monitor_enumerate(void)
{
    g_monitor_count = 0;
    memset(g_monitors, 0, sizeof(g_monitors));
    EnumDisplayMonitors(NULL, NULL, enum_monitor_proc, 0);

    /* 
     * 重大修复：将从 INI 载入的配置优先应用回 g_monitors 列表
     * 如果用户在 INI 中手动配置了具体的输入源映射，我们强制以 INI 的输入源为准
     */
    for (int i = 0; i < g_monitor_count; i++) {
        if (i < g_config.monitor_count && g_config.monitors[i].input_count > 0) {
            g_monitors[i].input_count = g_config.monitors[i].input_count;
            for (int j = 0; j < g_monitors[i].input_count; j++) {
                g_monitors[i].inputs[j] = g_config.monitors[i].inputs[j].value;
            }
            /* 重新定位 selected_index */
            g_monitors[i].selected_index = 0;
            for (int j = 0; j < g_monitors[i].input_count; j++) {
                if (g_monitors[i].inputs[j] == g_monitors[i].current_input) {
                    g_monitors[i].selected_index = j;
                    break;
                }
            }
            log_write("monitor_enumerate: Loaded manual override from INI successfully");
        }
    }

    /* 枚举后保存配置（如果是第一次生成，则存回） */
    config_save();
}

BOOL monitor_get_input(int idx, DWORD* out_value)
{
    if (idx < 0 || idx >= g_monitor_count) return FALSE;
    MonitorInfo* mi = &g_monitors[idx];

    PHYSICAL_MONITOR pm = {0};
    if (!get_physical_monitor(mi->hMonitor, &pm)) return FALSE;

    MC_VCP_CODE_TYPE type = MC_MOMENTARY;
    DWORD cur = 0, max_val = 0;
    BOOL ok = GetVCPFeatureAndVCPFeatureReply(pm.hPhysicalMonitor, 0x60,
                                              &type, &cur, &max_val);
    if (ok && out_value) *out_value = cur;

    DestroyPhysicalMonitors(1, &pm);
    return ok;
}

BOOL monitor_set_input(int idx, DWORD value)
{
    if (idx < 0 || idx >= g_monitor_count) return FALSE;
    MonitorInfo* mi = &g_monitors[idx];

    PHYSICAL_MONITOR pm = {0};
    if (!get_physical_monitor(mi->hMonitor, &pm)) {
        log_write("monitor_set_input: Failed to get physical monitor handle");
        return FALSE;
    }

    /* 记录当前试图写入的数值 */
    char buf[128];
    sprintf_s(buf, sizeof(buf), "monitor_set_input: Writing 0x%02X (%lu) to monitor %d", (unsigned)value, value, idx);
    log_write(buf);

    BOOL ok = SetVCPFeature(pm.hPhysicalMonitor, 0x60, value);
    if (ok) {
        mi->current_input = value;
        log_write("monitor_set_input: SetVCPFeature succeeded");
    } else {
        sprintf_s(buf, sizeof(buf), "monitor_set_input: SetVCPFeature FAILED, err=%lu", GetLastError());
        log_write(buf);
    }

    DestroyPhysicalMonitors(1, &pm);
    return ok;
}

DWORD monitor_cycle_next(int idx)
{
    if (idx < 0 || idx >= g_monitor_count) return 0;
    MonitorInfo* mi = &g_monitors[idx];

    if (mi->input_count > 1) {
        /* 正常循环模式 */
        mi->selected_index = (mi->selected_index + 1) % mi->input_count;
        return mi->inputs[mi->selected_index];
    } else if (mi->input_count == 1) {
        /* 只有一个已知源，fallback：在当前值 ±1 范围内试探 */
        DWORD next = mi->current_input + 1;
        if (next > 0x1F) next = 1;
        mi->inputs[0] = next;
        mi->selected_index = 0;
        return next;
    }
    return mi->current_input;
}

void monitor_commit_all(void)
{
    char log_buf[128];
    sprintf_s(log_buf, sizeof(log_buf), "monitor_commit_all: total monitor count = %d", g_monitor_count);
    log_write(log_buf);

    for (int i = 0; i < g_monitor_count; i++) {
        MonitorInfo* mi = &g_monitors[i];
        
        DWORD target;
        if (mi->input_count > 0 && mi->selected_index < mi->input_count) {
            target = mi->inputs[mi->selected_index];
        } else {
            target = mi->current_input;
        }

        sprintf_s(log_buf, sizeof(log_buf), "monitor %d: current=0x%02X (%lu), target=0x%02X (%lu)", 
                  i, (unsigned)mi->current_input, mi->current_input, (unsigned)target, target);
        log_write(log_buf);

        /* 只有目标与当前不同时才发送 DDC/CI 命令 */
        if (target != mi->current_input) {
            monitor_set_input(i, target);
        } else {
            log_write("monitor_commit_all: target is same as current. Skip command.");
        }
    }
}
