#include "compositor.h"
#include "input.h"
#include "upload_layout.h"
#include <chrono>
#include <vector>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <reshade.hpp>

using namespace reshade::api;

namespace
{
	constexpr const wchar_t *kMappingName = L"Local\\ETBMinecraftFrameV2";
	constexpr uint32_t kMagic = 0x5450434D; // "MCPT"
	constexpr int kHeader = 4096;
	constexpr int kSlotDesc = 256;
	constexpr int kSlotDescBytes = 128;
	constexpr const char *kEffect = "ETBMinecraft.fx";

	std::atomic<bool> g_registered{false};
	std::atomic<bool> g_active{false};
	bool g_inputRequested=false, g_fullWorld=false;
	ULONGLONG g_lastFrameTime=0;
	int g_mcPid=0;
	std::vector<uint8_t> g_staging;
	std::atomic<float> g_hostNear{0.15f};
	std::atomic<float> g_hostFar{10000.0f};
	std::atomic<uint32_t> g_bbWidth{0}, g_bbHeight{0};
	std::atomic<bool> g_cameraLocked{false};
	std::atomic<float> g_lookLight{-1.0f}, g_lookBias{-1.0f}, g_lookSlope{-1.0f};
	std::atomic<float> g_shakeX{0.0f}, g_shakeY{0.0f}, g_shakeRoll{0.0f}, g_portalWarp{0.0f};
	float g_savedLight = -1.0f, g_savedBias = -1.0f, g_savedSlope = -1.0f;

	struct Pose
	{
		float yaw = 0, pitch = 0, roll = 0, fov = 70;
		double x = 0, y = 0, z = 0;
		bool valid = false;
	};
	std::mutex g_poseLock;
	// the host's recent camera poses, newest last. The script reads the camera the host is about to render (the next frame),
	// while the picture being presented was rendered with the one before: re-project to that (g_poseLag back).
	Pose g_hostPoses[4];
	unsigned g_hostPoseCount = 0;
	std::atomic<int> g_poseLag{0}; // measured: 0 matches best (third and first person)
	Pose g_mcPose;

	HANDLE g_mapping = nullptr;
	const uint8_t *g_view = nullptr;
	int32_t g_slots = 0;
	int64_t g_stride = 0;
	int64_t g_lastPublish = -1;
	DWORD g_nextOpenAttempt = 0;

	struct Layer
	{
		resource tex = {0};
		resource_view srv = {0};
	};
    struct UploadSlot { resource buffer={0}; uint64_t retired=0; };
    UploadSlot g_upload[3];
    fence g_uploadFence={0};
    uint64_t g_uploadSerial=0;
    unsigned g_statUploads=0,g_statPresents=0,g_statDropped=0;
    double g_statUploadMs=0;
    ULONGLONG g_statStarted=0;
    int64_t g_lastMcFrame=-1;
    void destroy_uploads(device *dev) {
        for(auto &slot:g_upload) {
            if(slot.buffer.handle) dev->destroy_resource(slot.buffer);
            slot={};
        }
        if(g_uploadFence.handle) dev->destroy_fence(g_uploadFence);
        g_uploadFence={0};g_uploadSerial=0;
    }
    Layer g_world, g_depth, g_overlay;
	uint32_t g_width = 0, g_height = 0;
	bool g_hasFrame = false;
	float g_mcNear = 0.05f, g_mcFar = 2048.0f;
	int32_t g_mcFlags = 7;

	template <typename T>
	T read(const uint8_t *p)
	{
		T v;
		std::memcpy(&v, p, sizeof(T));
		return v;
	}

	bool open_mapping()
	{
		if (g_view != nullptr)
			return true;
		if (GetTickCount() < g_nextOpenAttempt)
			return false;
		g_nextOpenAttempt = GetTickCount() + 1000;
		g_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
		if (g_mapping == nullptr)
			return false;
		const auto *header = static_cast<const uint8_t *>(MapViewOfFile(g_mapping, FILE_MAP_READ, 0, 0, kHeader));
		if (header == nullptr || (read<uint32_t>(header) != kMagic || read<int>(header+4)!=2 || read<int>(header+8)!=kHeader))
		{
			if (header != nullptr)
				UnmapViewOfFile(header);
			CloseHandle(g_mapping);
			g_mapping = nullptr;
			return false;
		}
		g_slots = read<int32_t>(header + 12);
		g_stride = read<int64_t>(header + 16);
        if(g_slots!=3 || g_stride<=0 || g_stride>3840LL*2160*12) { UnmapViewOfFile(header); CloseHandle(g_mapping); g_mapping=nullptr; return false; }
		UnmapViewOfFile(header);
		g_view = static_cast<const uint8_t *>(MapViewOfFile(g_mapping, FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(kHeader + g_stride * g_slots)));
		if (g_view == nullptr)
		{
			CloseHandle(g_mapping);
			g_mapping = nullptr;
			return false;
		}
		reshade::log::message(reshade::log::level::info, "ETBMinecraft: connected to Minecraft's frame export");
		return true;
	}

	void destroy_layers(device *dev)
	{
		for (Layer *layer : {&g_world, &g_depth, &g_overlay})
		{
			if (layer->srv.handle != 0)
				dev->destroy_resource_view(layer->srv);
			if (layer->tex.handle != 0)
				dev->destroy_resource(layer->tex);
			*layer = Layer();
		}
        destroy_uploads(dev);
        g_width = g_height = 0;
        g_hasFrame = false;
	}

	bool create_layer(device *dev, Layer &layer, uint32_t w, uint32_t h, format fmt)
	{
		if (!dev->create_resource(
				resource_desc(w, h, 1, 1, fmt, 1, memory_heap::default_, resource_usage::shader_resource | resource_usage::copy_dest),
				nullptr, resource_usage::shader_resource, &layer.tex))
			return false;
		return dev->create_resource_view(layer.tex, resource_usage::shader_resource, resource_view_desc(fmt), &layer.srv);
	}

	void bind(effect_runtime *runtime)
	{
		runtime->update_texture_bindings("MCWORLD", g_world.srv, g_world.srv);
		runtime->update_texture_bindings("MCDEPTH", g_depth.srv, g_depth.srv);
		runtime->update_texture_bindings("MCOVERLAY", g_overlay.srv, g_overlay.srv);
	}

    // D3D12 update_texture_region allocates and flushes(true) on every call. Instead,
    // reuse fenced upload buffers and submit all three copies together without a CPU wait.
    bool upload_layers(effect_runtime *runtime, uint32_t w, uint32_t h, size_t layer) {
        device *dev=runtime->get_device();
        if(dev->get_api()!=device_api::d3d12) {
            Layer *layers[3]={&g_world,&g_depth,&g_overlay};
            for(unsigned i=0;i<3;i++) {
                subresource_data data{g_staging.data()+i*layer,w*4,uint32_t(layer)};
                dev->update_texture_region(data,layers[i]->tex,0);
            }
            return true;
        }
        if(!g_uploadFence.handle && !dev->create_fence(0,fence_flags::none,&g_uploadFence)) return false;
        const uint64_t completed=dev->get_completed_fence_value(g_uploadFence);
        UploadSlot *slot=nullptr;
        for(auto &candidate:g_upload) if(bridge_upload::reusable(candidate.retired,completed)) { slot=&candidate;break; }
        if(!slot) { ++g_statDropped;return false; } // keep the previous frame; never wait or overwrite in-flight data
        const auto layout=bridge_upload::layout(w,h);
        if(!slot->buffer.handle && !dev->create_resource(resource_desc(layout.total_bytes,memory_heap::upload,resource_usage::copy_source),
                nullptr,resource_usage::cpu_access,&slot->buffer)) return false;
        void *mapped=nullptr;
        if(!dev->map_buffer_region(slot->buffer,0,layout.total_bytes,map_access::write_only,&mapped)) return false;
        for(unsigned i=0;i<3;i++) for(uint32_t y=0;y<h;y++)
            std::memcpy(static_cast<uint8_t *>(mapped)+i*layout.layer_stride+uint64_t(y)*layout.row_pitch,
                g_staging.data()+i*layer+size_t(y)*layout.row_bytes,layout.row_bytes);
        dev->unmap_buffer_region(slot->buffer);
        auto *queue=runtime->get_command_queue();
        auto *cmd=queue->get_immediate_command_list();
        const resource textures[3]={g_world.tex,g_depth.tex,g_overlay.tex};
        const resource_usage sampled[3]={resource_usage::shader_resource,resource_usage::shader_resource,resource_usage::shader_resource};
        const resource_usage copies[3]={resource_usage::copy_dest,resource_usage::copy_dest,resource_usage::copy_dest};
        cmd->barrier(3,textures,sampled,copies);
        for(unsigned i=0;i<3;i++)
            cmd->copy_buffer_to_texture(slot->buffer,i*layout.layer_stride,layout.row_pitch/4,h,textures[i],0);
        cmd->barrier(3,textures,copies,sampled);
        const uint64_t serial=++g_uploadSerial;
        if(!queue->signal(g_uploadFence,serial)) {
            slot->retired=UINT64_MAX;
            reshade::log::message(reshade::log::level::error,"ETB upload fence signal failed; buffer quarantined");
            return false;
        }
        slot->retired=serial;
        return true;
    }

	/// Upload the newest published Minecraft frame, if there is one we haven't shown yet.
	void upload(effect_runtime *runtime)
	{
		const int pid=read<int>(g_view+44);
        if(pid!=g_mcPid) { g_mcPid=pid; g_lastPublish=-1; g_lastMcFrame=-1; g_hasFrame=false; }
        const int64_t published = read<int64_t>(g_view + 32);
		if (published == g_lastPublish)
			return;
		const int32_t slot = read<int32_t>(g_view + 40);
		if (slot < 0 || slot >= g_slots)
			return;
		const uint8_t *desc = g_view + kSlotDesc + kSlotDescBytes * slot;
		const int64_t seq = read<int64_t>(desc);
		if (seq <= 0 || (seq & 1))
			return;
		uint8_t metadata[kSlotDescBytes];
        std::memcpy(metadata,desc,sizeof(metadata));
        const int64_t frame=read<int64_t>(metadata+8);
        if(frame<=g_lastMcFrame) return;
        const uint32_t w=read<uint32_t>(metadata+24),h=read<uint32_t>(metadata+28);
        if(w==0 || h==0 || w>3840 || h>2160 || size_t(w)*h*12>size_t(g_stride)) return;
        const size_t layer=size_t(w)*h*4;
        g_staging.resize(layer*3);
        std::memcpy(g_staging.data(),g_view+kHeader+g_stride*slot,layer*3);
        MemoryBarrier();
        if(read<int64_t>(desc)!=seq || read<int64_t>(metadata)!=seq) return;
        // Only a fully validated CPU snapshot can reach the GPU.
        desc=metadata;
		device *dev = runtime->get_device();
		if (w != g_width || h != g_height)
        {
            // Only a resize waits for the GPU before replacing resources.
            runtime->get_command_queue()->wait_idle();
            destroy_layers(dev);
            if (!create_layer(dev, g_world, w, h, format::r8g8b8a8_unorm) ||
				!create_layer(dev, g_depth, w, h, format::r32_float) ||
				!create_layer(dev, g_overlay, w, h, format::r8g8b8a8_unorm))
			{
				destroy_layers(dev);
				return;
			}
			g_width = w;
			g_height = h;
			bind(runtime);
		}
        const auto uploadStart=std::chrono::steady_clock::now();
        if(!upload_layers(runtime,w,h,layer)) return;
        g_statUploadMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count();
        ++g_statUploads;g_lastMcFrame=frame;
		g_lastPublish = published;
		g_mcNear = read<float>(desc + 32);
		g_mcFar = read<float>(desc + 36);
		g_mcFlags = read<int32_t>(desc + 44);
		g_mcPose.fov = read<float>(desc + 40);
		g_mcPose.yaw = read<float>(desc + 72);
		g_mcPose.pitch = read<float>(desc + 76);
		g_mcPose.roll = read<float>(desc + 80);
		g_mcPose.x = read<double>(desc + 48);
		g_mcPose.y = read<double>(desc + 56);
		g_mcPose.z = read<double>(desc + 64);
		g_mcPose.valid = true;
		g_hasFrame = true;
        g_lastFrameTime=GetTickCount64();
	}

	/// Camera-to-world rotation, as Minecraft builds it: rotationYXZ(pi - yaw, -pitch, roll) (camera looks down -z).
	void camera_rotation(const Pose &p, float m[3][3])
	{
		const float d2r = 3.14159265f / 180.0f;
		const float a = 3.14159265f - p.yaw * d2r, b = -p.pitch * d2r, c = p.roll * d2r;
		const float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b), cc = std::cos(c), sc = std::sin(c);
		// Ry(a) * Rx(b) * Rz(c)
		const float ry[3][3] = {{ca, 0, sa}, {0, 1, 0}, {-sa, 0, ca}};
		const float rx[3][3] = {{1, 0, 0}, {0, cb, -sb}, {0, sb, cb}};
		const float rz[3][3] = {{cc, -sc, 0}, {sc, cc, 0}, {0, 0, 1}};
		float t[3][3] = {};
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				for (int k = 0; k < 3; ++k)
					t[i][j] += ry[i][k] * rx[k][j];
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
			{
				m[i][j] = 0;
				for (int k = 0; k < 3; ++k)
					m[i][j] += t[i][k] * rz[k][j];
			}
	}

	/// Rows of R_mc^T * R_host: turns a ray in the host's camera space into Minecraft's camera space.
	void warp_matrix(const Pose &host, const Pose &mc, float out[3][3])
	{
		float rh[3][3], rm[3][3];
		camera_rotation(host, rh);
		camera_rotation(mc, rm);
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
			{
				out[i][j] = 0;
				for (int k = 0; k < 3; ++k)
					out[i][j] += rm[k][i] * rh[k][j];
			}
	}

	/// Reload ETBMinecraft.fx when the file changes (ReShade doesn't watch it), so tweaks don't need a the host restart.
	void watch_effect_file(effect_runtime *runtime)
	{
		static DWORD next = 0;
		static FILETIME last = {};
		static wchar_t path[MAX_PATH] = {};
		if (GetTickCount() < next)
			return;
		next = GetTickCount() + 1000;
		if (path[0] == 0)
		{
			GetModuleFileNameW(nullptr, path, MAX_PATH);
			wchar_t *slash = wcsrchr(path, L'\\');
			if (slash)
				wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"reshade-shaders\\Shaders\\ETBMinecraft.fx");
		}
		WIN32_FILE_ATTRIBUTE_DATA info;
		if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info))
			return;
		if (last.dwLowDateTime != 0 && CompareFileTime(&info.ftLastWriteTime, &last) != 0)
			runtime->reload_effect_next_frame(kEffect);
		last = info.ftLastWriteTime;
	}

	void read_mapped_pose();
    void on_present(effect_runtime *runtime)
	{
        ++g_statPresents;
        const ULONGLONG now=GetTickCount64();
        if(!g_statStarted) g_statStarted=now;
        if(now-g_statStarted>=5000) {
            char report[256];const double seconds=double(now-g_statStarted)/1000.0;
            std::snprintf(report,sizeof(report),"ETB render stats: present %.1f/s, upload %.1f/s, upload CPU %.2f ms, busy drops %u",
                g_statPresents/seconds,g_statUploads/seconds,g_statUploadMs/std::max(1u,g_statUploads),g_statDropped);
            reshade::log::message(reshade::log::level::info,report);
            g_statPresents=g_statUploads=g_statDropped=0;g_statUploadMs=0;g_statStarted=now;
        }
        read_mapped_pose();
        bridge_input::tick(static_cast<HWND>(runtime->get_hwnd()),g_inputRequested && g_active && g_hasFrame && GetTickCount64()-g_lastFrameTime<1000);
		watch_effect_file(runtime); // every frame, even when the effect failed to compile
	}

    void read_mapped_pose() {
        static wchar_t path[MAX_PATH]={};
        if(!path[0]) {
            GetModuleFileNameW(nullptr,path,MAX_PATH);
            wchar_t *slash=wcsrchr(path,L'\\'); if(!slash) return;
            wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"ue4ss\\Mods\\BackroomsMinecraftBridge\\render-v2.csv");
        }
        WIN32_FILE_ATTRIBUTE_DATA info{};
        FILETIME now{}; GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER n{},m{}; n.LowPart=now.dwLowDateTime; n.HighPart=now.dwHighDateTime;
        if(!GetFileAttributesExW(path,GetFileExInfoStandard,&info)) { g_active=false; g_inputRequested=false; return; }
        m.LowPart=info.ftLastWriteTime.dwLowDateTime; m.HighPart=info.ftLastWriteTime.dwHighDateTime;
        if(n.QuadPart>m.QuadPart+10'000'000ULL) { g_active=false; g_inputRequested=false; return; }
        FILE *file=nullptr;
        if(_wfopen_s(&file,path,L"r")!=0 || !file) return;
        char line[1024]={}; bool ok=std::fgets(line,sizeof(line),file)!=nullptr; std::fclose(file);
        if(!ok || !std::strchr(line,'\n')) return;
        int version=0,locked=0,active=0,input=0,full=0; char session[65]={};
        unsigned long long seq=0,tail=0; double x=0,y=0,z=0; float yaw=0,pitch=0,roll=0,fov=70;
        int count=sscanf_s(line,"%d,%64[^,],%llu,%lf,%lf,%lf,%f,%f,%f,%f,%d,%d,%d,%d,%llu",
            &version,session,unsigned(sizeof(session)),&seq,&x,&y,&z,&yaw,&pitch,&roll,&fov,&locked,&active,&input,&full,&tail);
        if(count==15 && version==2 && seq==tail && std::isfinite(x+y+z) && std::isfinite(yaw+pitch+roll) && fov>1 && fov<179) {
            compositor::set_host_pose(yaw,pitch,roll,fov,x,y,z);
            g_cameraLocked=locked!=0; g_active=active!=0; g_inputRequested=input!=0; g_fullWorld=full!=0;
        }
    }
	void on_begin_effects(effect_runtime *runtime, command_list *, resource_view, resource_view)
	{
		read_mapped_pose();
		uint32_t bw = 0, bh = 0;
		runtime->get_screenshot_width_and_height(&bw, &bh);
		g_bbWidth = bw;
		g_bbHeight = bh;
		bool on = g_active && open_mapping();
		if (on)
			upload(runtime);
        const bool linked=on && g_hasFrame && GetTickCount64()-g_lastFrameTime<1000;
        on=linked && GetTickCount64()-g_lastFrameTime<250;
        bridge_input::tick(static_cast<HWND>(runtime->get_hwnd()),g_inputRequested && linked);
		// The technique stays enabled (preset); McActive gates it, so the host passes through untouched until a
		// Minecraft frame is here. (Toggling techniques from inside this callback crashes ReShade.)
		if (const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, "McActive"); v.handle != 0)
			runtime->set_uniform_value_bool(v, on);
		if (!on) return;
        if(const auto v=runtime->find_uniform_variable(kEffect,"FullMinecraft");v.handle!=0)
            runtime->set_uniform_value_bool(v,(g_mcFlags&8)!=0 && g_fullWorld);
		if (const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, "McPlanes"); v.handle != 0)
			runtime->set_uniform_value_float(v, g_mcNear, g_mcFar, float(g_mcFlags));
		if (const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, "CameraLocked"); v.handle != 0)
			runtime->set_uniform_value_bool(v, g_cameraLocked.load());
		// a scene's look overrides the preset's light matching and depth bias; the preset's values come back after
		auto look = [&](const char *name, float want, float &saved) {
			const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, name);
			if (v.handle == 0)
				return;
			if (want >= 0.0f)
			{
				if (saved < 0.0f)
					runtime->get_uniform_value_float(v, &saved, 1);
				runtime->set_uniform_value_float(v, want);
			}
			else if (saved >= 0.0f)
			{
				runtime->set_uniform_value_float(v, saved);
				saved = -1.0f;
			}
		};
		look("LightMatch", g_lookLight.load(), g_savedLight);
		look("DepthBias", g_lookBias.load(), g_savedBias);
		look("SlopeBias", g_lookSlope.load(), g_savedSlope);
		if (const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, "Shake"); v.handle != 0)
			runtime->set_uniform_value_float(v, g_shakeX.load(), g_shakeY.load(), g_shakeRoll.load());
		if (const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, "PortalWarp"); v.handle != 0)
			runtime->set_uniform_value_float(v, g_portalWarp.load());
		if (const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, "HostPlanes"); v.handle != 0)
			runtime->set_uniform_value_float(v, g_hostNear.load(), g_hostFar.load());

		// Re-projection from Minecraft's pose to the host's latest (extrapolated by the effect's PosePrediction frames).
		Pose host, prev;
		{
			std::lock_guard<std::mutex> lock(g_poseLock);
			const unsigned lag = unsigned(std::clamp(g_poseLag.load(), 0, 2));
			if (g_hostPoseCount > lag)
				host = g_hostPoses[(g_hostPoseCount - 1 - lag) & 3];
			if (g_hostPoseCount > lag + 1)
				prev = g_hostPoses[(g_hostPoseCount - 2 - lag) & 3];
		}
		float predict = 0.0f;
		if (const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, "PosePrediction"); v.handle != 0)
			runtime->get_uniform_value_float(v, &predict, 1);
		const bool warp = host.valid && g_mcPose.valid && !g_cameraLocked;
		if (warp && prev.valid && predict != 0.0f)
		{
			auto delta = [](float a, float b) { float d = std::fmod(a - b + 540.0f, 360.0f) - 180.0f; return d; };
			host.yaw += delta(host.yaw, prev.yaw) * predict;
			host.pitch += (host.pitch - prev.pitch) * predict;
			host.roll += (host.roll - prev.roll) * predict;
			host.x += (host.x - prev.x) * predict;
			host.y += (host.y - prev.y) * predict;
			host.z += (host.z - prev.z) * predict;
		}
		float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
		float t[3] = {0, 0, 0};
		if (warp)
		{
			warp_matrix(host, g_mcPose, m);
			// T = R_mc^T (host position - Minecraft's camera position), in Minecraft's camera space
			float rm[3][3];
			camera_rotation(g_mcPose, rm);
			const float d[3] = {float(host.x - g_mcPose.x), float(host.y - g_mcPose.y), float(host.z - g_mcPose.z)};
			for (int i = 0; i < 3; ++i)
				t[i] = rm[0][i] * d[0] + rm[1][i] * d[1] + rm[2][i] * d[2];
		}
		const char *rows[3] = {"WarpRow0", "WarpRow1", "WarpRow2"};
		for (int i = 0; i < 3; ++i)
			if (const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, rows[i]); v.handle != 0)
				runtime->set_uniform_value_float(v, m[i][0], m[i][1], m[i][2]);
		if (const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, "WarpT"); v.handle != 0)
			runtime->set_uniform_value_float(v, t[0], t[1], t[2]);
		const float d2r = 3.14159265f / 180.0f;
		const float tanHost = std::tan((warp ? host.fov : g_mcPose.fov) * d2r * 0.5f), tanMc = std::tan(g_mcPose.fov * d2r * 0.5f);
		if (const effect_uniform_variable v = runtime->find_uniform_variable(kEffect, "WarpTan"); v.handle != 0)
			runtime->set_uniform_value_float(v, tanHost, tanMc, float(g_width) / float(g_height));
	}

	void on_reloaded_effects(effect_runtime *runtime)
	{
		if (g_width != 0)
			bind(runtime);
	}

	void on_destroy_effect_runtime(effect_runtime *runtime)
	{
        runtime->get_command_queue()->wait_idle();
        destroy_layers(runtime->get_device());
        bridge_input::shutdown();
	}
}

namespace compositor
{
	bool try_register(void *module, void *reshade_module)
	{
		if (g_registered)
			return true;
		if (!reshade::register_addon(module, reshade_module))
			return false;
		reshade::register_event<reshade::addon_event::reshade_begin_effects>(on_begin_effects);
		reshade::register_event<reshade::addon_event::reshade_present>(on_present);
		reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(on_reloaded_effects);
		reshade::register_event<reshade::addon_event::destroy_effect_runtime>(on_destroy_effect_runtime);
		g_registered = true;
		reshade::log::message(reshade::log::level::info, "ETBMinecraft: registered");
		return true;
	}

	void unregister(void *module, void *reshade_module)
	{
		bridge_input::shutdown();
        if(g_view) { UnmapViewOfFile(g_view); g_view=nullptr; }
        if(g_mapping) { CloseHandle(g_mapping); g_mapping=nullptr; }
		if (g_registered.exchange(false))
			reshade::unregister_addon(module, reshade_module);
	}

	void set_active(bool active)
	{
		g_active = active;
	}

	void set_host_planes(float near_clip, float far_clip)
	{
		g_hostNear = near_clip;
		g_hostFar = far_clip;
	}

	void backbuffer_size(int &width, int &height)
	{
		width = int(g_bbWidth.load());
		height = int(g_bbHeight.load());
	}

	void set_camera_locked(bool locked)
	{
		g_cameraLocked = locked;
	}

	void set_screen_fx(float shake_x, float shake_y, float shake_roll, float portal_warp)
	{
		g_shakeX = shake_x;
		g_shakeY = shake_y;
		g_shakeRoll = shake_roll;
		g_portalWarp = portal_warp;
	}

	void set_look(float light_match, float depth_bias, float slope_bias)
	{
		g_lookLight = light_match;
		g_lookBias = depth_bias;
		g_lookSlope = slope_bias;
	}

	void set_host_pose(float yaw, float pitch, float roll, float fov, double x, double y, double z)
	{
		std::lock_guard<std::mutex> lock(g_poseLock);
		g_hostPoses[g_hostPoseCount & 3] = {yaw, pitch, roll, fov, x, y, z, true};
		++g_hostPoseCount;
	}

	void set_pose_lag(int frames)
	{
		g_poseLag = frames;
	}
}
