#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <mutex>
#include <vector>
#include <string>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "monitor.h"
#include "config.h"
#include "wsserver.h"

#pragma comment(lib, "ws2_32.lib")

extern "C" void log_write(const char* msg);

static HWND s_hwnd_main = NULL;
static std::atomic<bool> s_server_running(false);
static HANDLE s_server_thread = NULL;
static SOCKET s_listen_sock = INVALID_SOCKET;

// Client management
struct HttpClient {
    SOCKET s;
    bool is_ws;
};

static std::vector<SOCKET> s_ws_clients;
static std::mutex s_clients_mutex;

// Raw HTML code for the responsive dark-themed controller page
const char* g_index_html = R"html(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>vdsrcswitch Controller</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: #121212;
            color: #e0e0e0;
            margin: 0;
            padding: 16px;
            user-select: none;
            -webkit-user-select: none;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
        }
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid #333;
            padding-bottom: 12px;
            margin-bottom: 16px;
        }
        h1 {
            margin: 0;
            font-size: 18px;
            font-weight: 600;
            color: #ffffff;
        }
        .status {
            display: flex;
            align-items: center;
            font-size: 13px;
            background-color: #1e1e1e;
            padding: 4px 10px;
            border-radius: 12px;
            border: 1px solid #333;
        }
        .dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            margin-right: 6px;
            background-color: #ff5722;
        }
        .dot.connected {
            background-color: #4caf50;
        }
        .monitor-card {
            background-color: #1e1e1e;
            border-radius: 10px;
            padding: 14px;
            margin-bottom: 14px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.2);
            border: 1px solid #2d2d2d;
        }
        .monitor-name {
            font-size: 15px;
            font-weight: 600;
            margin-bottom: 12px;
            color: #ffffff;
            border-left: 3px solid #007acc;
            padding-left: 8px;
        }
        .input-list {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(110px, 1fr));
            gap: 8px;
        }
        .btn {
            background-color: #2a2a2a;
            color: #cccccc;
            border: 1px solid #3a3a3a;
            padding: 12px 6px;
            border-radius: 6px;
            cursor: pointer;
            font-size: 13px;
            transition: all 0.15s ease;
            text-align: center;
            outline: none;
            -webkit-tap-highlight-color: transparent;
        }
        .btn:active {
            background-color: #3a3a3a;
            transform: scale(0.97);
        }
        .btn.active {
            background-color: #007acc;
            color: #ffffff;
            border-color: #0098ff;
            font-weight: 600;
            box-shadow: 0 0 8px rgba(0, 122, 204, 0.4);
        }
        .btn:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>vdsrcswitch Controller</h1>
            <div class="status">
                <span class="dot" id="status-dot"></span>
                <span id="status-text">Disconnected</span>
            </div>
        </header>
        <div id="monitors-container"></div>
    </div>
    <script>
        let ws;
        const dot = document.getElementById('status-dot');
        const text = document.getElementById('status-text');
        const container = document.getElementById('monitors-container');

        function connect() {
            text.innerText = "Connecting...";
            dot.className = "dot";
            
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = `${protocol}//${window.location.host}/ws`;
            ws = new WebSocket(wsUrl);

            ws.onopen = () => {
                dot.className = "dot connected";
                text.innerText = "Connected";
            };

            ws.onclose = () => {
                dot.className = "dot";
                text.innerText = "Disconnected";
                setTimeout(connect, 2000);
            };

            ws.onerror = (err) => {
                console.error(err);
            };

            ws.onmessage = (event) => {
                try {
                    const msg = JSON.parse(event.data);
                    if (msg.type === 'state') {
                        renderMonitors(msg.monitors);
                    }
                } catch (e) {
                    console.error("Failed to parse msg", e);
                }
            };
        }

        function renderMonitors(monitors) {
            container.innerHTML = '';
            if (monitors.length === 0) {
                container.innerHTML = '<div style="text-align:center;color:#888;margin-top:40px;">No active monitors found</div>';
                return;
            }
            monitors.forEach(m => {
                const card = document.createElement('div');
                card.className = 'monitor-card';

                const title = document.createElement('div');
                title.className = 'monitor-name';
                title.innerText = m.name || `Monitor #${m.id + 1}`;
                card.appendChild(title);

                const list = document.createElement('div');
                list.className = 'input-list';

                m.inputs.forEach(input => {
                    const btn = document.createElement('button');
                    btn.className = 'btn';
                    if (input.value === m.current_input) {
                        btn.className += ' active';
                    }
                    btn.innerText = input.name || `Input 0x${input.value.toString(16).toUpperCase()}`;
                    btn.onclick = () => {
                        ws.send(JSON.stringify({
                            type: 'set_input',
                            monitor_idx: m.id,
                            value: input.value
                        }));
                    };
                    list.appendChild(btn);
                });

                card.appendChild(list);
                container.appendChild(card);
            });
        }

        connect();
    </script>
</body>
</html>
)html";

// Helper to remove WS clients from global list
static void remove_ws_client(SOCKET s) {
    std::lock_guard<std::mutex> lock(s_clients_mutex);
    auto it = std::find(s_ws_clients.begin(), s_ws_clients.end(), s);
    if (it != s_ws_clients.end()) {
        s_ws_clients.erase(it);
    }
}

// Compute SHA-1 hash via Windows CryptoAPI
static BOOL sha1_hash(const char* input, int input_len, BYTE* output) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BOOL result = FALSE;
    
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
            if (CryptHashData(hHash, (const BYTE*)input, input_len, 0)) {
                DWORD hash_len = 20;
                if (CryptGetHashParam(hHash, HP_HASHVAL, output, &hash_len, 0)) {
                    result = TRUE;
                }
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    return result;
}

// Simple Base64 encoder
static void base64_encode(const BYTE* src, int len, char* dst) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, j;
    for (i = 0, j = 0; i < len; i += 3) {
        BYTE a = src[i];
        BYTE b = (i + 1 < len) ? src[i + 1] : 0;
        BYTE c = (i + 2 < len) ? src[i + 2] : 0;
        
        dst[j++] = table[a >> 2];
        dst[j++] = table[((a & 3) << 4) | (b >> 4)];
        dst[j++] = (i + 1 < len) ? table[((b & 15) << 2) | (c >> 6)] : '=';
        dst[j++] = (i + 2 < len) ? table[c & 63] : '=';
    }
    dst[j] = '\0';
}

static int recv_all(SOCKET s, char* buf, int len) {
    int total = 0;
    while (total < len) {
        int r = recv(s, buf + total, len - total, 0);
        if (r <= 0) return r; // Disconnected or error
        total += r;
    }
    return total;
}

static int ws_send_text(SOCKET s, const char* text, int len) {
    char header[10];
    int header_len = 0;
    header[0] = (char)0x81; // FIN + text opcode
    if (len < 126) {
        header[1] = (char)len;
        header_len = 2;
    } else {
        header[1] = (char)126;
        header[2] = (char)((len >> 8) & 0xFF);
        header[3] = (char)(len & 0xFF);
        header_len = 4;
    }
    if (send(s, header, header_len, 0) == SOCKET_ERROR) return -1;
    if (send(s, text, len, 0) == SOCKET_ERROR) return -1;
    return 0;
}

// Build monitor status JSON string
static std::string build_state_json() {
    std::string json = "{\"type\":\"state\",\"monitors\":[";
    for (int i = 0; i < g_monitor_count; i++) {
        char name_utf8[256] = {0};
        int needed = WideCharToMultiByte(CP_UTF8, 0, g_monitors[i].description, -1, name_utf8, sizeof(name_utf8), NULL, NULL);
        if (needed <= 0) {
            name_utf8[0] = '\0';
        }
        
        // Escape json characters in name_utf8
        std::string escaped_name;
        for (int c = 0; name_utf8[c] != '\0'; c++) {
            if (name_utf8[c] == '"' || name_utf8[c] == '\\') {
                escaped_name += '\\';
            }
            escaped_name += name_utf8[c];
        }
        
        if (i > 0) json += ",";
        json += "{\"id\":" + std::to_string(i) + ",";
        json += "\"name\":\"" + escaped_name + "\",";
        json += "\"current_input\":" + std::to_string(g_monitors[i].current_input) + ",";
        json += "\"inputs\":[";
        
        for (int j = 0; j < g_monitors[i].input_count; j++) {
            DWORD val = g_monitors[i].inputs[j];
            const WCHAR* wname = config_get_input_name(i, val);
            char val_name_utf8[128] = {0};
            needed = WideCharToMultiByte(CP_UTF8, 0, wname, -1, val_name_utf8, sizeof(val_name_utf8), NULL, NULL);
            if (needed <= 0) {
                val_name_utf8[0] = '\0';
            }
            
            std::string escaped_val_name;
            for (int c = 0; val_name_utf8[c] != '\0'; c++) {
                if (val_name_utf8[c] == '"' || val_name_utf8[c] == '\\') {
                    escaped_val_name += '\\';
                }
                escaped_val_name += val_name_utf8[c];
            }
            
            if (j > 0) json += ",";
            json += "{\"value\":" + std::to_string(val) + ",\"name\":\"" + escaped_val_name + "\"}";
        }
        json += "]}";
    }
    json += "]}";
    return json;
}

extern "C" void wsserver_broadcast_state(void) {
    if (!s_server_running) return;
    
    std::string json = build_state_json();
    
    std::lock_guard<std::mutex> lock(s_clients_mutex);
    for (auto it = s_ws_clients.begin(); it != s_ws_clients.end(); ) {
        if (ws_send_text(*it, json.c_str(), (int)json.length()) < 0) {
            closesocket(*it);
            it = s_ws_clients.erase(it);
        } else {
            ++it;
        }
    }
}

static int handle_ws_data(SOCKET s) {
    uint8_t header[2];
    if (recv_all(s, (char*)header, 2) <= 0) return -1;
    
    int opcode = header[0] & 0x0F;
    if (opcode == 8) { // Connection close frame
        return 0;
    }
    
    int masked = (header[1] & 0x80) != 0;
    int len_field = header[1] & 0x7F;
    
    uint64_t actual_len = 0;
    if (len_field == 126) {
        uint8_t ext_len[2];
        if (recv_all(s, (char*)ext_len, 2) <= 0) return -1;
        actual_len = (ext_len[0] << 8) | ext_len[1];
    } else if (len_field == 127) {
        uint8_t ext_len[8];
        if (recv_all(s, (char*)ext_len, 8) <= 0) return -1;
        actual_len = 0;
        for (int i = 0; i < 8; i++) {
            actual_len = (actual_len << 8) | ext_len[i];
        }
    } else {
        actual_len = len_field;
    }
    
    uint8_t mask_key[4] = {0};
    if (masked) {
        if (recv_all(s, (char*)mask_key, 4) <= 0) return -1;
    }
    
    if (actual_len > 4096) {
        return -1; // Protect memory
    }
    
    std::vector<char> payload((size_t)actual_len + 1, 0);
    if (recv_all(s, payload.data(), (int)actual_len) <= 0) return -1;
    
    if (masked) {
        for (size_t i = 0; i < actual_len; i++) {
            payload[i] ^= mask_key[i % 4];
        }
    }
    
    if (opcode == 1) { // Text frame
        std::string text(payload.data(), (size_t)actual_len);
        
        // Parse simple JSON values
        if (text.find("\"set_input\"") != std::string::npos) {
            auto m_pos = text.find("\"monitor_idx\":");
            auto v_pos = text.find("\"value\":");
            if (m_pos != std::string::npos && v_pos != std::string::npos) {
                int monitor_idx = std::atoi(text.c_str() + m_pos + 14);
                DWORD value = (DWORD)std::strtoul(text.c_str() + v_pos + 8, nullptr, 10);
                
                if (s_hwnd_main) {
                    PostMessageW(s_hwnd_main, WM_VDSS_WS_SET_INPUT, (WPARAM)monitor_idx, (LPARAM)value);
                }
            }
        }
    }
    
    return 1; // Keep open
}

static int handle_http_data(SOCKET s, HttpClient* client) {
    char req[4096];
    int r = recv(s, req, sizeof(req) - 1, 0);
    if (r <= 0) return r;
    req[r] = '\0';
    
    std::string req_str(req);
    
    if (req_str.find("GET /ws") != std::string::npos && req_str.find("Upgrade: websocket") != std::string::npos) {
        // Upgrade to WebSocket
        auto key_pos = req_str.find("Sec-WebSocket-Key:");
        if (key_pos != std::string::npos) {
            key_pos += 18;
            while (key_pos < req_str.length() && (req_str[key_pos] == ' ' || req_str[key_pos] == '\t')) {
                key_pos++;
            }
            std::string key;
            while (key_pos < req_str.length() && req_str[key_pos] != '\r' && req_str[key_pos] != '\n') {
                key += req_str[key_pos++];
            }
            
            std::string concat = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            BYTE hash[20];
            if (sha1_hash(concat.c_str(), (int)concat.length(), hash)) {
                char accept_key[64];
                base64_encode(hash, 20, accept_key);
                
                std::string resp = 
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: " + std::string(accept_key) + "\r\n\r\n";
                    
                send(s, resp.c_str(), (int)resp.length(), 0);
                
                client->is_ws = true;
                
                // Add to global broadcast list
                {
                    std::lock_guard<std::mutex> lock(s_clients_mutex);
                    s_ws_clients.push_back(s);
                }
                
                // Send current state immediately
                std::string json = build_state_json();
                ws_send_text(s, json.c_str(), (int)json.length());
                
                return 1; // Keep connection open
            }
        }
        
        const char* bad_resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        send(s, bad_resp, (int)strlen(bad_resp), 0);
        return 0; // Close
    } else if (req_str.find("GET / ") != std::string::npos || req_str.find("GET /index.html") != std::string::npos) {
        // Serve index.html
        int html_len = (int)strlen(g_index_html);
        std::string resp_hdr = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(html_len) + "\r\n"
            "Connection: close\r\n\r\n";
            
        send(s, resp_hdr.c_str(), (int)resp_hdr.length(), 0);
        send(s, g_index_html, html_len, 0);
        return 0; // Close
    } else {
        const char* nf_resp = "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        send(s, nf_resp, (int)strlen(nf_resp), 0);
        return 0; // Close
    }
}

static DWORD WINAPI wsserver_thread_proc(LPVOID lpParam) {
    (void)lpParam;
    
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log_write("wsserver: WSAStartup FAILED");
        s_server_running = false;
        return 1;
    }
    
    s_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listen_sock == INVALID_SOCKET) {
        log_write("wsserver: socket() FAILED");
        WSACleanup();
        s_server_running = false;
        return 1;
    }
    
    char optval = 1;
    setsockopt(s_listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)g_config.ws_port);
    
    if (bind(s_listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        char err_msg[128];
        sprintf_s(err_msg, sizeof(err_msg), "wsserver: bind() FAILED on port %d, err=%d", g_config.ws_port, WSAGetLastError());
        log_write(err_msg);
        closesocket(s_listen_sock);
        s_listen_sock = INVALID_SOCKET;
        WSACleanup();
        s_server_running = false;
        return 1;
    }
    
    if (listen(s_listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        log_write("wsserver: listen() FAILED");
        closesocket(s_listen_sock);
        s_listen_sock = INVALID_SOCKET;
        WSACleanup();
        s_server_running = false;
        return 1;
    }
    
    char ok_msg[128];
    sprintf_s(ok_msg, sizeof(ok_msg), "wsserver: listening on port %d", g_config.ws_port);
    log_write(ok_msg);
    
    std::vector<HttpClient> clients;
    
    while (s_server_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(s_listen_sock, &read_fds);
        
        SOCKET max_s = s_listen_sock;
        for (const auto& c : clients) {
            FD_SET(c.s, &read_fds);
            if (c.s > max_s) {
                max_s = c.s;
            }
        }
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int sel = select(0, &read_fds, NULL, NULL, &tv);
        if (sel == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (s_server_running) {
                char err_msg[64];
                sprintf_s(err_msg, sizeof(err_msg), "wsserver: select() FAILED, err=%d", err);
                log_write(err_msg);
            }
            break;
        }
        
        if (!s_server_running) break;
        if (sel == 0) continue; // Timeout check
        
        // 1. Accept new connection
        if (FD_ISSET(s_listen_sock, &read_fds)) {
            SOCKET client = accept(s_listen_sock, NULL, NULL);
            if (client != INVALID_SOCKET) {
                if (clients.size() < 32) {
                    clients.push_back({client, false});
                } else {
                    closesocket(client);
                }
            }
        }
        
        // 2. Process clients
        for (auto it = clients.begin(); it != clients.end(); ) {
            SOCKET s = it->s;
            if (FD_ISSET(s, &read_fds)) {
                int ret;
                if (it->is_ws) {
                    ret = handle_ws_data(s);
                } else {
                    ret = handle_http_data(s, &(*it));
                }
                
                if (ret <= 0) {
                    closesocket(s);
                    if (it->is_ws) {
                        remove_ws_client(s);
                    }
                    it = clients.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }
    
    // Cleanup active sockets
    for (const auto& c : clients) {
        closesocket(c.s);
        if (c.is_ws) {
            remove_ws_client(c.s);
        }
    }
    
    if (s_listen_sock != INVALID_SOCKET) {
        closesocket(s_listen_sock);
        s_listen_sock = INVALID_SOCKET;
    }
    
    WSACleanup();
    log_write("wsserver: thread stopped");
    return 0;
}

extern "C" BOOL wsserver_start(HWND hwnd_main) {
    if (s_server_running) return TRUE;
    
    s_hwnd_main = hwnd_main;
    s_server_running = true;
    s_ws_clients.clear();
    
    s_server_thread = CreateThread(NULL, 0, wsserver_thread_proc, NULL, 0, NULL);
    if (!s_server_thread) {
        log_write("wsserver: failed to create thread");
        s_server_running = false;
        return FALSE;
    }
    
    return TRUE;
}

extern "C" void wsserver_stop(void) {
    if (!s_server_running) return;
    
    s_server_running = false;
    
    // Close the listen socket so select wakes up instantly
    if (s_listen_sock != INVALID_SOCKET) {
        closesocket(s_listen_sock);
    }
    
    if (s_server_thread) {
        WaitForSingleObject(s_server_thread, INFINITE);
        CloseHandle(s_server_thread);
        s_server_thread = NULL;
    }
    
    {
        std::lock_guard<std::mutex> lock(s_clients_mutex);
        for (SOCKET s : s_ws_clients) {
            closesocket(s);
        }
        s_ws_clients.clear();
    }
}
