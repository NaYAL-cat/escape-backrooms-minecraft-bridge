#pragma once
#include <windows.h>
namespace bridge_input {
void tick(HWND window, bool connected);
void shutdown();
}
