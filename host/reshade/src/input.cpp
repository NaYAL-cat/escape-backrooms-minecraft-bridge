#include "input.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <reshade.hpp>

namespace {
constexpr unsigned capacity = 2048;
struct Event { volatile LONG64 stamp; int kind, a, b, modifiers; LONG64 milliseconds; };
struct Header {
    unsigned magic, version, count, stride;
    volatile LONG64 published;
    volatile LONG flags;
    unsigned width, height, reserved;
    LONG64 boot;
    char padding[16];
};
static_assert(sizeof(Header) == 64 && sizeof(Event) == 32);
struct Ring { Header header; Event events[capacity]; };
HANDLE mapping = nullptr;
Ring* ring = nullptr;
HWND window = nullptr;
WNDPROC previous = nullptr;
std::mutex lock;
std::atomic<bool> connected{false}, enabled{true};
LONG64 serial = 0;
bool wasCapturing = false;
bool rawReady=false, rawMouseButtons=false, registeredMouse=false;
RECT oldClip{}; bool clipped=false;
int modifiers() {
    return ((GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0) |
        ((GetKeyState(VK_CONTROL) & 0x8000) ? 64 : 0) |
        ((GetKeyState(VK_MENU) & 0x8000) ? 256 : 0); // SDL3 KMOD values
}
void emit(int kind, int a = 0, int b = 0) {
    std::lock_guard<std::mutex> guard(lock);
    if (!ring) return;
    const LONG64 id = ++serial;
    Event& e = ring->events[(id - 1) % capacity];
    InterlockedExchange64(&e.stamp, id * 2 + 1);
    e.kind = kind; e.a = a; e.b = b; e.modifiers = modifiers();
    e.milliseconds = static_cast<LONG64>(GetTickCount64());
    MemoryBarrier();
    InterlockedExchange64(&e.stamp, id * 2);
    InterlockedExchange64(&ring->header.published, id);
}
bool capturing() { return rawReady && connected && enabled && GetForegroundWindow() == window; }
LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_F8 && !(lp & (1LL << 30))) {
        enabled = !enabled; emit(6); return 0;
    }
    if (msg == WM_KILLFOCUS) emit(6);
    if (capturing()) {
        switch (msg) {
        case WM_INPUT: {
            RAWINPUT raw{}; UINT bytes = sizeof(raw);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &bytes, sizeof(RAWINPUTHEADER)) != UINT(-1)
                && raw.header.dwType == RIM_TYPEMOUSE) {
                if(!(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) emit(1, raw.data.mouse.lLastX, raw.data.mouse.lLastY);
                if(rawMouseButtons) {
                    const USHORT flags=raw.data.mouse.usButtonFlags;
                    const USHORT downs[3]={RI_MOUSE_LEFT_BUTTON_DOWN,RI_MOUSE_RIGHT_BUTTON_DOWN,RI_MOUSE_MIDDLE_BUTTON_DOWN};
                    const USHORT ups[3]={RI_MOUSE_LEFT_BUTTON_UP,RI_MOUSE_RIGHT_BUTTON_UP,RI_MOUSE_MIDDLE_BUTTON_UP};
                    for(int b=0;b<3;b++) { if(flags&downs[b]) emit(3,b,1); if(flags&ups[b]) emit(3,b,0); }
                    if(flags&RI_MOUSE_WHEEL) emit(4,static_cast<SHORT>(raw.data.mouse.usButtonData));
                }
            }
            return DefWindowProcW(hwnd, msg, wp, lp); // foreground raw-input cleanup; do not pass it to UE
        }
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            if (wp == VK_F4 && (GetKeyState(VK_MENU) & 0x8000)) break;
            emit(2, static_cast<int>(wp), (lp & (1LL << 30)) ? 2 : 1); return 0;
        case WM_KEYUP: case WM_SYSKEYUP: emit(2, static_cast<int>(wp), 0); return 0;
        case WM_CHAR: emit(5, static_cast<int>(wp)); return 0;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: emit(3, 0, 1); return 0;
        case WM_LBUTTONUP: emit(3, 0, 0); return 0;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: emit(3, 1, 1); return 0;
        case WM_RBUTTONUP: emit(3, 1, 0); return 0;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: emit(3, 2, 1); return 0;
        case WM_MBUTTONUP: emit(3, 2, 0); return 0;
        case WM_MOUSEWHEEL: emit(4, static_cast<short>(HIWORD(wp))); return 0;
        case WM_SETCURSOR: SetCursor(nullptr); return TRUE;
        case WM_MOUSEMOVE: return 0;
        }
    }
    return previous ? CallWindowProcW(previous, hwnd, msg, wp, lp) : DefWindowProcW(hwnd,msg,wp,lp);
}
}
namespace bridge_input {
void tick(HWND hwnd, bool isConnected) {
    if (!hwnd) return;
    if (!ring) {
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Ring), L"Local\\ETBBridgeInputV2");
        if (!mapping) return;
        ring = static_cast<Ring*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Ring)));
        if (!ring) { CloseHandle(mapping); mapping = nullptr; return; }
        std::memset(ring, 0, sizeof(Ring));
        ring->header.magic = 0x32494245; ring->header.version = 2;
        ring->header.count = capacity; ring->header.stride = sizeof(Event);
        ring->header.boot = static_cast<LONG64>(GetTickCount64());
    }
    if (!window) {
        window = hwnd;
        SetLastError(0);
        previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndproc)));
        if(!previous && GetLastError()!=0) { window=nullptr; reshade::log::message(reshade::log::level::error,"ETB input: cannot subclass host window"); return; }
        // UE normally registers raw mouse itself. Register only when no mouse
        // registration exists, so we never steal another receiver in this process.
        RAWINPUTDEVICE devices[32]; UINT count = 32; bool mouse = false;
        const UINT result = GetRegisteredRawInputDevices(devices, &count, sizeof(RAWINPUTDEVICE));
        if (result != UINT(-1)) for (UINT i = 0; i < result; ++i) {
            if(devices[i].usUsagePage==1 && devices[i].usUsage==2) {
                mouse=true; rawReady=devices[i].hwndTarget==hwnd || devices[i].hwndTarget==nullptr;
                rawMouseButtons=(devices[i].dwFlags & RIDEV_NOLEGACY)==RIDEV_NOLEGACY;
            }
        }
        if (!mouse) { RAWINPUTDEVICE d{1,2,0,hwnd}; rawReady=RegisterRawInputDevices(&d,1,sizeof(d))!=FALSE; registeredMouse=rawReady; }
        if(!rawReady) reshade::log::message(reshade::log::level::error,"ETB input: raw mouse belongs to another window; capture disabled");
    }
    connected = isConnected;
    bool cap = capturing();
    if (wasCapturing && !cap) emit(6);
    wasCapturing = cap;
    if(cap) {
        if(!clipped) { GetClipCursor(&oldClip); clipped=true; }
        RECT rect{}; GetClientRect(hwnd,&rect);
        POINT a{rect.left,rect.top},b{rect.right,rect.bottom}; ClientToScreen(hwnd,&a); ClientToScreen(hwnd,&b);
        RECT screen{a.x,a.y,b.x,b.y}; ClipCursor(&screen);
    } else if(clipped) { ClipCursor(GetForegroundWindow()==window ? &oldClip : nullptr); clipped=false; SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512))); }
    RECT client{}; GetClientRect(hwnd, &client);
    ring->header.width = client.right; ring->header.height = client.bottom;
    InterlockedExchange(&ring->header.flags, (cap ? 1 : 0) | (GetForegroundWindow() == hwnd ? 2 : 0) | (!enabled ? 4 : 0) | (rawReady ? 8 : 0));
}
void shutdown() {
    connected = false;
    if(clipped) { ClipCursor(GetForegroundWindow()==window ? &oldClip : nullptr); clipped=false; }
    if(registeredMouse) { RAWINPUTDEVICE d{1,2,RIDEV_REMOVE,nullptr}; RegisterRawInputDevices(&d,1,sizeof(d)); registeredMouse=false; }
    rawReady=false;
    if (window && IsWindow(window) && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC)) == wndproc)
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
    window = nullptr;
    std::lock_guard<std::mutex> guard(lock);
    if (ring) { InterlockedExchange(&ring->header.flags,0); UnmapViewOfFile(ring); ring = nullptr; }
    if (mapping) { CloseHandle(mapping); mapping = nullptr; }
}
}
