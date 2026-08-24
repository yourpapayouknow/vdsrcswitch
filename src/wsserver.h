#pragma once
#ifndef VDSS_WSSERVER_H
#define VDSS_WSSERVER_H

#include <windows.h>

/* Custom window message for WebSocket-triggered input switches */
#define WM_VDSS_WS_SET_INPUT (WM_USER + 10)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the WebSocket/HTTP server in a background thread.
 * hwnd_main: handle to the main window to receive WM_VDSS_WS_SET_INPUT messages.
 * Return TRUE if successful.
 */
BOOL wsserver_start(HWND hwnd_main);

/**
 * Stop the WebSocket/HTTP server.
 */
void wsserver_stop(void);

/**
 * Broadcast the current monitor state to all connected WebSocket clients.
 */
void wsserver_broadcast_state(void);

#ifdef __cplusplus
}
#endif

#endif /* VDSS_WSSERVER_H */
