#include <windows.h>
#include <reshade.hpp>
#include "compositor.h"

extern "C" __declspec(dllexport) const char *NAME = "Escape the Backrooms Minecraft Bridge";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Depth-composites the local Minecraft 26.3 Fabric scene into Escape the Backrooms.";

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
	if (!compositor::try_register(addon_module, reshade_module))
		return false;
	compositor::set_host_planes(0.1f, 20000.0f);
	compositor::set_active(false);
	compositor::set_pose_lag(0);
	return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module)
{
	compositor::unregister(addon_module, reshade_module);
}
