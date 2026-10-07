// Composites Minecraft (passthrough mod) into Escape the Backrooms V. The ETBMinecraft add-on (ETBMinecraft.asi) uploads
// Minecraft's latest frame into the MCWORLD / MCDEPTH / MCOVERLAY textures and sets the plane uniforms.
//
// world: premultiplied RGBA, drawn where Minecraft is nearer than Escape the Backrooms's depth buffer (both in metres).
// overlay: Minecraft's hand, HUD and screens, always on top.
#include "ReShade.fxh"

texture MCWorldTex : MCWORLD;
texture MCDepthTex : MCDEPTH;
texture MCOverlayTex : MCOVERLAY;
sampler sWorld { Texture = MCWorldTex; AddressU = CLAMP; AddressV = CLAMP; };
sampler sDepth { Texture = MCDepthTex; MinFilter = POINT; MagFilter = POINT; AddressU = CLAMP; AddressV = CLAMP; };
sampler sOverlay { Texture = MCOverlayTex; MinFilter = POINT; MagFilter = POINT; AddressU = CLAMP; AddressV = CLAMP; };

// x = near, y = far, z = flags (1: [0,1] depth, 2: rows bottom-up, 4: reversed Z). Set by the add-on.
uniform float3 McPlanes = float3(0.05, 2048.0, 7.0);
// Escape the Backrooms's camera near/far clip. Set by the add-on.
uniform float2 HostPlanes = float2(0.1, 20000.0);

// Set true by the add-on only while it has uploaded a Minecraft frame; until then Escape the Backrooms passes through untouched.
uniform bool McActive = false;
// Only bypass reprojection when both images really share the same captured view.
uniform bool CameraLocked = false;
uniform bool FullMinecraft = false;

uniform bool HostReversedZ < ui_label = "Escape the Backrooms depth is reversed"; > = true;
uniform float DepthBias < ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; ui_label = "Depth bias (m)";
	ui_tooltip = "How far behind Escape the Backrooms's surface Minecraft may still show (blocks resting on the ground)."; > = 0.04;
uniform float SlopeBias < ui_type = "drag"; ui_min = 0.0; ui_max = 40.0; ui_step = 0.1; ui_label = "Depth bias per grazing slope";
	ui_tooltip = "Extra depth bias where Escape the Backrooms's surface is seen at a grazing angle (its depth changes fast down the screen): Minecraft ground laid into Escape the Backrooms's (lava, netherrack) still shows far away."; > = 0.0;
uniform float MaxBias < ui_type = "drag"; ui_min = 0.1; ui_max = 10.0; ui_step = 0.1; ui_label = "Largest depth bias (m)"; > = 3.0;
uniform float HostDepthScale < ui_type = "drag"; ui_min = 0.5; ui_max = 2.0; ui_step = 0.001; ui_label = "Escape the Backrooms depth scale";
	ui_tooltip = "Calibration: multiplies Escape the Backrooms's linearised depth."; > = 1.0;
uniform float LightMatch < ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.01; ui_label = "Match Escape the Backrooms lighting";
	ui_tooltip = "Relight Minecraft by the (blurred) Escape the Backrooms picture around it: darker in shade, tinted by nearby light."; > = 0.0;
uniform float LightReference < ui_type = "drag"; ui_min = 0.1; ui_max = 1.0; ui_step = 0.01; ui_label = "Neutral brightness";
	ui_tooltip = "Escape the Backrooms brightness at which Minecraft keeps its own colours."; > = 0.42;
uniform float LightTint < ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.01; ui_label = "Light colour"; > = 0.0;
uniform float LightBlur < ui_type = "drag"; ui_min = 1.0; ui_max = 6.0; ui_step = 0.1; ui_label = "Light blur (mip)"; > = 3.2;
uniform float GradeMatch < ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.01; ui_label = "Match Escape the Backrooms colour grade";
	ui_tooltip = "Give Minecraft the colour of Escape the Backrooms's whole picture (a red Nether night, an orange sunset), as Escape the Backrooms's own grading does to its world."; > = 0.0;
uniform float HazeStart < ui_type = "drag"; ui_min = 0.0; ui_max = 200.0; ui_step = 1.0; ui_label = "Haze start (m)"; > = 15.0;
uniform float HazeDistance < ui_type = "drag"; ui_min = 10.0; ui_max = 1000.0; ui_step = 1.0; ui_label = "Haze distance (m)";
	ui_tooltip = "Far away, Minecraft fades into the Escape the Backrooms picture around it (Escape the Backrooms's haze and fog don't reach Minecraft otherwise)."; > = 160.0;
uniform float HazeStrength < ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.01; ui_label = "Haze strength"; > = 0.0;
uniform float EdgeSoftness < ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.01; ui_label = "Soften Minecraft's edges";
	ui_tooltip = "Anti-alias where Minecraft meets Escape the Backrooms (Escape the Backrooms's own anti-aliasing ran before Minecraft was added)."; > = 0.0;
uniform float ContactShadow < ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.01; ui_label = "Contact shadows";
	ui_tooltip = "Darken Escape the Backrooms's surfaces right next to Minecraft's (under mobs' feet, around blocks)."; > = 0.0;
uniform float ContactRadius < ui_type = "drag"; ui_min = 0.1; ui_max = 3.0; ui_step = 0.05; ui_label = "Contact shadow size (m)"; > = 0.7;
uniform float BloomStrength < ui_type = "drag"; ui_min = 0.0; ui_max = 2.0; ui_step = 0.01; ui_label = "Minecraft glow";
	ui_tooltip = "Bright Minecraft things (lava, fire, the portal) glow over Escape the Backrooms's picture, as Escape the Backrooms's own lights do."; > = 0.0;
uniform float BloomThreshold < ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.01; ui_label = "Glow threshold"; > = 0.62;
// Set by the add-on: the camera shake, applied to the finished picture so Escape the Backrooms and Minecraft shake together
// (x, y: fraction of the screen height; z: roll, radians), and the nether portal's warp (0..1) while passing through.
uniform float3 Shake = float3(0.0, 0.0, 0.0);
uniform float PortalWarp = 0.0;
uniform float Timer < source = "timer"; >;
uniform int DebugView < ui_type = "combo"; ui_items = "Composite\0Escape the Backrooms depth (1 m bands)\0Minecraft depth (1 m bands)\0Depth difference\0"; > = 0;
uniform bool Reproject < ui_label = "Re-project to Escape the Backrooms's camera"; ui_tooltip = "Rotate Minecraft's (slightly older) frame onto Escape the Backrooms's current camera."; > = true;
uniform float PosePrediction < ui_type = "drag"; ui_min = -2.0; ui_max = 3.0; ui_step = 0.05; ui_label = "Pose prediction (frames)";
	ui_tooltip = "Extrapolate Escape the Backrooms's camera rotation by this many frames before re-projecting."; > = 0.0;

// Set by the add-on: rows of R_mc^T R_host (Escape the Backrooms camera ray -> Minecraft camera ray), and
// x = tan(Escape the Backrooms vertical fov / 2), y = tan(Minecraft vertical fov / 2), z = Minecraft aspect.
uniform float3 WarpRow0 = float3(1.0, 0.0, 0.0);
uniform float3 WarpRow1 = float3(0.0, 1.0, 0.0);
uniform float3 WarpRow2 = float3(0.0, 0.0, 1.0);
uniform float3 WarpTan = float3(0.7, 0.7, 1.7777);
// Set by the add-on: Escape the Backrooms's camera position relative to Minecraft's, in Minecraft's camera space.
uniform float3 WarpT = float3(0.0, 0.0, 0.0);

// Escape the Backrooms's picture at quarter size with mips: its blurred levels stand in for the light around each pixel.
texture HostLightTex { Width = BUFFER_WIDTH / 4; Height = BUFFER_HEIGHT / 4; Format = RGBA8; MipLevels = 7; };
sampler sHostLight { Texture = HostLightTex; AddressU = CLAMP; AddressV = CLAMP; };

// The composited world (Escape the Backrooms + Minecraft, no overlay) and, per pixel: Minecraft's coverage, its depth and Escape the Backrooms's.
texture CompositeTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
sampler sComposite { Texture = CompositeTex; AddressU = CLAMP; AddressV = CLAMP; };
texture McInfoTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA16F; };
sampler sMcInfo { Texture = McInfoTex; MinFilter = POINT; MagFilter = POINT; AddressU = CLAMP; AddressV = CLAMP; };
// Minecraft's bright parts at quarter size with mips (the glow).
texture McBrightTex { Width = BUFFER_WIDTH / 4; Height = BUFFER_HEIGHT / 4; Format = RGBA16F; MipLevels = 6; };
sampler sMcBright { Texture = McBrightTex; AddressU = CLAMP; AddressV = CLAMP; };

float luma(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 PS_Light(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	return float4(tex2D(ReShade::BackBuffer, uv).rgb, 1.0);
}

/// Minecraft renders at full daylight, so treat its colour as albedo and light it with Escape the Backrooms's local light; then give it
/// Escape the Backrooms's colour grade (the whole picture's colour) and, far away, Escape the Backrooms's haze (fade into the picture around it).
float3 relight(float3 c, float2 uv, float zm)
{
	const float3 L = tex2Dlod(sHostLight, float4(uv, 0, LightBlur)).rgb;
	const float lum = luma(L);
	const float gain = clamp(lum / LightReference, 0.42, 1.3);
	const float3 tint = clamp(lerp(1.0, L / max(lum, 1e-3), LightTint), 0.55, 1.5);
	c = lerp(c, c * gain * tint, LightMatch);
	// the picture's grade, gentled (Escape the Backrooms's own grade keeps some colour in everything) and kept off lights (bright
	// things keep their colour: fire, lava, the portal)
	const float3 M = tex2Dlod(sHostLight, float4(0.5, 0.5, 0, 6.0)).rgb;
	const float3 grade = clamp(lerp(1.0, M / max(luma(M), 1e-3), 0.6), 0.6, 1.6);
	c = lerp(c, c * grade, GradeMatch * (1.0 - saturate((max(c.r, max(c.g, c.b)) - 0.45) * 2.5)));
	const float h = saturate((1.0 - exp(-max(zm - HazeStart, 0.0) / HazeDistance)) * HazeStrength);
	const float3 H = tex2Dlod(sHostLight, float4(uv, 0, LightBlur + 1.0)).rgb;
	return lerp(c, H, h);
}

float mc_linear(float d)
{
	const float n = McPlanes.x, f = McPlanes.y;
	if (d <= 0.0)
		return 1e9;
	return n * f / (n + d * (f - n));
}

float host_linear(float d)
{
	const float n = HostPlanes.x, f = HostPlanes.y;
	float z;
	if (HostReversedZ)
		z = d > 0.0 ? n * f / (n + d * (f - n)) : 1e9;
	else
		z = d < 1.0 ? n * f / (f - d * (f - n)) : 1e9;
	return z * HostDepthScale;
}

float3 bands(float z)
{
	const float t = frac(z);
	return lerp(float3(0.1, 0.1, 0.1), float3(1.0, 0.85, 0.3), step(0.5, t)) * saturate(1.5 - z / 100.0);
}

// Forward visibility maps. A fresh map is rebuilt every host frame; no temporal history.
// Separate depth and source passes avoid races between the winning depth and its colour.
texture WarpDepthTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R32U; };
texture WarpSourceTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R32U; };
storage2D<uint> uWarpDepth { Texture = WarpDepthTex; };
storage2D<uint> uWarpSource { Texture = WarpSourceTex; };
sampler2D<uint> sWarpDepth { Texture = WarpDepthTex; };
sampler2D<uint> sWarpSource { Texture = WarpSourceTex; };

float3 to_host(float3 mcPoint)
{
    float3 p = mcPoint - WarpT;
    return float3(dot(p,float3(WarpRow0.x,WarpRow1.x,WarpRow2.x)),
                  dot(p,float3(WarpRow0.y,WarpRow1.y,WarpRow2.y)),
                  dot(p,float3(WarpRow0.z,WarpRow1.z,WarpRow2.z)));
}
float3 source_point(float2 uv, float z)
{
    return float3((uv*2.0-1.0)*float2(WarpTan.y*WarpTan.z,WarpTan.y),-1.0)*z;
}
float2 host_pixel(float3 p)
{
    float2 n = p.xy / -p.z / float2(WarpTan.x*BUFFER_WIDTH*BUFFER_RCP_HEIGHT,WarpTan.x);
    return (n*float2(.5,-.5)+.5)*float2(BUFFER_WIDTH,BUFFER_HEIGHT);
}
float3 source_ray(float2 uv)
{
    float3 ray=float3((uv*float2(2,-2)+float2(-1,1))*
        float2(WarpTan.x*BUFFER_WIDTH*BUFFER_RCP_HEIGHT,WarpTan.x),-1.0);
    return float3(dot(WarpRow0,ray),dot(WarpRow1,ray),dot(WarpRow2,ray));
}

void CS_Clear(uint3 id : SV_DispatchThreadID)
{
    if (McActive && !FullMinecraft && Reproject && !CameraLocked && id.x < BUFFER_WIDTH && id.y < BUFFER_HEIGHT) {
        tex2Dstore(uWarpDepth,int2(id.xy),0xffffffffu);
        tex2Dstore(uWarpSource,int2(id.xy),0xffffffffu);
    }
}

// Splat the footprint of an existing source texel. Unlike inverse searching, this
// still finds the object when the target pixel used to contain empty background.
float slope(int2 src, int2 axis, float q)
{
    int2 size=tex2Dsize(sDepth);
    int2 a=src-axis,b=src+axis;
    float da=all(a>=0)&&all(a<size)?tex2Dfetch(sDepth,a).r:0.0;
    float db=all(b>=0)&&all(b<size)?tex2Dfetch(sDepth,b).r:0.0;
    float ga=da>0.0?q-1.0/mc_linear(da):1e9;
    float gb=db>0.0?1.0/mc_linear(db)-q:1e9;
    // One-sided differences preserve silhouettes/depth discontinuities. Reciprocal
    // depth is affine over a planar face, so grazing walls do not develop cracks.
    float g=abs(ga)<abs(gb)?ga:gb;
    return abs(g)<q*.2?g:0.0;
}
bool footprint(uint index, out int2 first, out int2 last, out float2 centre, out float3 plane)
{
    first=0;last=-1;centre=0;plane=0;
    int2 size=tex2Dsize(sDepth);
    int2 src=int2(index % uint(size.x), index / uint(size.x));
    float raw=tex2Dfetch(sDepth,src).r;
    if (raw<=0.0 || tex2Dfetch(sWorld,src).a<=0.0) return false;
    float depth=mc_linear(raw);
    centre=(float2(src)+.5)/float2(size);
    float3 hp=to_host(source_point(centre,depth));
    if (-hp.z<=.01) return false;
    float q=1.0/depth;
    float2 gradient=float2(slope(src,int2(1,0),q),slope(src,int2(0,1),q));
    float a=q+dot(gradient,float2(size)*.5-float2(src)-.5);
    plane=float3(gradient*float2(size)/(2.0*float2(WarpTan.y*WarpTan.z,WarpTan.y)),-a);
    // A quarter texel of conservative overlap covers raster quantization cracks.
    float2 halfPixel=.75/float2(size);
    float2 low=float2(BUFFER_WIDTH,BUFFER_HEIGHT),high=0.0;
    [loop] for(int c=0;c<4;c++) {
        float2 uv=centre+halfPixel*float2((c&1)?1:-1,(c&2)?1:-1);
        float cornerQ=q+dot(gradient,(uv-centre)*float2(size));
        if(cornerQ<=0.0) return false;
        float3 corner=to_host(source_point(uv,1.0/cornerQ));
        if (-corner.z<=.01) return false;
        float2 pixel=host_pixel(corner);
        low=min(low,pixel);high=max(high,pixel);
    }
    first=max(int2(ceil(low-.5)),int2(0,0));
    last=min(int2(floor(high-.5)),int2(BUFFER_WIDTH-1,BUFFER_HEIGHT-1));
    return all(first<=last);
}
uint candidate_depth(int2 dst, float2 centre, float3 plane)
{
    float3 ray=source_ray((float2(dst)+.5)*BUFFER_PIXEL_SIZE);
    if (ray.z>=-1e-5) return 0xffffffffu;
    float denominator=dot(plane,ray);
    if(denominator<=1e-7) return 0xffffffffu;
    float z=(1.0-dot(plane,WarpT))/denominator;
    if (z<=.01) return 0xffffffffu;
    float3 hitPoint=ray*z+WarpT;
    float2 uv=hitPoint.xy/max(-hitPoint.z,1e-5)/float2(WarpTan.y*WarpTan.z,WarpTan.y)*.5+.5;
    if(any(abs((uv-centre)*float2(tex2Dsize(sDepth)))>.751)) return 0xffffffffu;
    return asuint(z);
}
// Keep read and write resource sets separate: binding a texture as SRV and UAV
// in the same pass is invalid on D3D11 and has conflicting states on D3D12.
void CS_Depth(uint3 id : SV_DispatchThreadID)
{
    if(!McActive || FullMinecraft || !Reproject || CameraLocked || id.x>=BUFFER_WIDTH || id.y>=BUFFER_HEIGHT) return;
    int2 size=tex2Dsize(sDepth);
    [loop] for(uint i=id.y*BUFFER_WIDTH+id.x;i<uint(size.x*size.y);i+=BUFFER_WIDTH*BUFFER_HEIGHT) {
        int2 first,last;float2 centre;float3 plane;
        if(!footprint(i,first,last,centre,plane)) continue;
        [loop] for(int y=first.y;y<=last.y;y++) [loop] for(int x=first.x;x<=last.x;x++)
            atomicMin(uWarpDepth,int2(x,y),candidate_depth(int2(x,y),centre,plane));
    }
}
void CS_Source(uint3 id : SV_DispatchThreadID)
{
    if(!McActive || FullMinecraft || !Reproject || CameraLocked || id.x>=BUFFER_WIDTH || id.y>=BUFFER_HEIGHT) return;
    int2 size=tex2Dsize(sDepth);
    [loop] for(uint i=id.y*BUFFER_WIDTH+id.x;i<uint(size.x*size.y);i+=BUFFER_WIDTH*BUFFER_HEIGHT) {
        int2 first,last;float2 centre;float3 plane;
        if(!footprint(i,first,last,centre,plane)) continue;
        [loop] for(int y=first.y;y<=last.y;y++) [loop] for(int x=first.x;x<=last.x;x++) {
            int2 dst=int2(x,y);uint key=candidate_depth(dst,centre,plane);
            if(key!=0xffffffffu && tex2Dfetch(sWarpDepth,dst)==key) atomicMin(uWarpSource,dst,i);
        }
    }
}

void PS_Composite(float4 pos : SV_Position, float2 uv : TEXCOORD, out float4 outColor : SV_Target0, out float4 outInfo : SV_Target1)
{
	const float3 host = tex2D(ReShade::BackBuffer, uv).rgb;
	outInfo = 0.0;
	outColor = float4(host, 1.0);
	if (!McActive)
		return;
	const float2 ouv = float2(uv.x, 1.0 - uv.y); // Minecraft's rows are bottom-up

	if (FullMinecraft) { outColor=float4(tex2D(sWorld,ouv).rgb,1.0); return; }
    // Where the measured host pixel's view ray lands in the captured Minecraft frame.
	float2 muv = ouv;
	const float zh = host_linear(tex2Dlod(ReShade::DepthBuffer, float4(uv, 0, 0)).x);
	// how far behind Escape the Backrooms's surface Minecraft may still show: more where that surface is seen at a grazing angle
	const float allow = min(DepthBias + SlopeBias * abs(ddy(zh)), max(MaxBias, DepthBias));
	float zm = 1e9;
    float4 world=0.0;
    if (Reproject && !CameraLocked) {
        uint index=tex2Dfetch(sWarpSource,int2(pos.xy));
        int2 size=tex2Dsize(sDepth);
        if(index!=0xffffffffu && index<uint(size.x*size.y)) {
            int2 src=int2(index%uint(size.x),index/uint(size.x));
            // Colour and depth belong to the very same winning source texel.
            world=tex2Dfetch(sWorld,src);
            zm=asfloat(tex2Dfetch(sWarpDepth,int2(pos.xy)));
        }
    } else {
        world=tex2D(sWorld,muv);
        zm=mc_linear(tex2D(sDepth,muv).r);
    }

	outInfo = float4(0.0, 0.0, zh, 0.0);
	if (DebugView == 1)
	{
		outColor = float4(bands(zh), 1.0);
		return;
	}
	if (DebugView == 2)
	{
		outColor = float4(world.a > 0.0 ? bands(zm) : host * 0.3, 1.0);
		return;
	}
	if (DebugView == 3)
	{
		outColor = float4(world.a > 0.0 ? float3(saturate((zh - zm) * 0.5 + 0.5), saturate(-(zh - zm) * 0.5 + 0.5), 0.0) : host * 0.3, 1.0);
		return;
	}

	const float visible = zm < zh + allow ? 1.0 : 0.0;
	const float cover = world.a * visible;
	outColor = float4(relight(world.rgb, uv, zm) * visible + host * (1.0 - cover), 1.0);
	outInfo = float4(cover, cover > 0.0 ? zm : 0.0, zh, 0.0);
}

/// Minecraft's bright parts (lava, fire, the portal, blazes): what glows.
float4 PS_Bright(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	const float cover = tex2D(sMcInfo, uv).x;
	const float3 c = tex2D(sComposite, uv).rgb;
	const float l = luma(c);
	return float4(c * (cover * saturate((l - BloomThreshold) / max(1.0 - BloomThreshold, 1e-3)) / max(l, 1e-3)), 1.0);
}

/// The finished picture: the world (shaken as one: Escape the Backrooms and Minecraft together), Minecraft's edges softened, contact
/// shadows on Escape the Backrooms's surfaces next to Minecraft's, Minecraft's glow, the portal's warp, and Minecraft's hand and HUD on
/// top (neither shaken nor warped).
float3 PS_Final(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	if (!McActive)
		return tex2D(ReShade::BackBuffer, uv).rgb;
	const float aspect = BUFFER_WIDTH * BUFFER_RCP_HEIGHT;
	const float t = Timer * 0.001;
	// shake: rotate and move the picture a little, zoomed in just enough that no edge shows
	const float amount = length(Shake.xy) + abs(Shake.z) * 0.5;
	const float zoom = 1.0 + amount * 2.2 + PortalWarp * 0.06;
	float2 d = (uv - 0.5) * float2(aspect, 1.0) / zoom;
	// the portal: a swirl and waves, strongest in the middle
	if (PortalWarp > 0.0)
	{
		const float r = length(d);
		const float swirl = PortalWarp * 1.1 * exp(-r * 2.2) * sin(t * 1.7 + 1.0);
		const float cs = cos(swirl), sn = sin(swirl);
		d = float2(d.x * cs - d.y * sn, d.x * sn + d.y * cs);
		d += PortalWarp * 0.014 * float2(sin(d.y * 38.0 + t * 7.0), cos(d.x * 31.0 + t * 6.0));
	}
	const float cr = cos(Shake.z), sr = sin(Shake.z);
	d = float2(d.x * cr - d.y * sr, d.x * sr + d.y * cr) + Shake.xy;
	const float2 wuv = d / float2(aspect, 1.0) + 0.5;

	const float2 px = BUFFER_PIXEL_SIZE;
	float3 color = tex2D(sComposite, wuv).rgb;
	const float4 info = tex2D(sMcInfo, wuv);
	// soften where Minecraft meets Escape the Backrooms (Escape the Backrooms's anti-aliasing never saw Minecraft)
	if (EdgeSoftness > 0.0)
	{
		const float c1 = tex2D(sMcInfo, wuv + float2(px.x, 0)).x, c2 = tex2D(sMcInfo, wuv - float2(px.x, 0)).x;
		const float c3 = tex2D(sMcInfo, wuv + float2(0, px.y)).x, c4 = tex2D(sMcInfo, wuv - float2(0, px.y)).x;
		const float edge = saturate(abs(c1 - info.x) + abs(c2 - info.x) + abs(c3 - info.x) + abs(c4 - info.x));
		if (edge > 0.0)
		{
			const float3 n = tex2D(sComposite, wuv + float2(px.x, 0)).rgb + tex2D(sComposite, wuv - float2(px.x, 0)).rgb
				+ tex2D(sComposite, wuv + float2(0, px.y)).rgb + tex2D(sComposite, wuv - float2(0, px.y)).rgb;
			color = lerp(color, (color * 2.0 + n) / 6.0, edge * EdgeSoftness);
		}
	}
	// contact shadows: Escape the Backrooms surface pixels with Minecraft surfaces close by (in depth), mostly above them
	if (ContactShadow > 0.0 && info.x < 0.5 && info.z < 200.0)
	{
		const float zh = info.z;
		const float r = clamp(ContactRadius / max(zh, 0.5) * BUFFER_HEIGHT / (2.0 * WarpTan.x), 2.0, 48.0);
		float occ = 0.0, weight = 0.0;
		[unroll] for (int i = 0; i < 10; ++i)
		{
			const float a = i * 2.39996 + 0.6; // golden-angle spiral
			const float rr = r * sqrt((i + 0.5) / 10.0);
			const float2 o = float2(cos(a), sin(a)) * rr;
			const float4 sm = tex2Dlod(sMcInfo, float4(wuv + o * px, 0, 0));
			const float w = o.y < 0.0 ? 1.0 : 0.35; // things stand on the ground: their shadow is below them
			weight += w;
			if (sm.x > 0.5 && abs(sm.y - zh) < ContactRadius * 1.5)
				occ += w * (1.0 - rr / (r + 1.0));
		}
		color *= 1.0 - ContactShadow * saturate(occ / max(weight, 1e-3) * 2.2);
	}
	// Minecraft's glow
	if (BloomStrength > 0.0)
	{
		const float3 b = tex2Dlod(sMcBright, float4(wuv, 0, 1.0)).rgb * 0.5 + tex2Dlod(sMcBright, float4(wuv, 0, 2.5)).rgb * 0.8
			+ tex2Dlod(sMcBright, float4(wuv, 0, 4.0)).rgb;
		color += b * BloomStrength;
	}
	// the portal's purple
	if (PortalWarp > 0.0)
	{
		const float r = length((uv - 0.5) * float2(aspect, 1.0));
		const float3 purple = color * float3(0.72, 0.38, 1.25) + float3(0.12, 0.0, 0.22) * (0.6 + 0.4 * sin(t * 3.0 + r * 9.0));
		color = lerp(color, purple, saturate(PortalWarp * (0.55 + r * 0.6)));
	}
	const float4 overlay = tex2D(sOverlay, float2(uv.x, 1.0 - uv.y)); // hand and HUD are screen-space: never shaken
	return overlay.rgb + saturate(color) * (1.0 - overlay.a);
}

technique ETBMinecraft < ui_tooltip = "Minecraft passthrough: enabled automatically while the Minecraft link is up."; >
{
    pass ClearVisibility {
        ComputeShader = CS_Clear<8,8>;
        DispatchSizeX = (BUFFER_WIDTH+7)/8; DispatchSizeY = (BUFFER_HEIGHT+7)/8;
    }
    pass ProjectDepth {
        ComputeShader = CS_Depth<8,8>;
        DispatchSizeX = (BUFFER_WIDTH+7)/8; DispatchSizeY = (BUFFER_HEIGHT+7)/8;
    }
    pass ProjectSource {
        ComputeShader = CS_Source<8,8>;
        DispatchSizeX = (BUFFER_WIDTH+7)/8; DispatchSizeY = (BUFFER_HEIGHT+7)/8;
    }
	pass Light
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_Light;
		RenderTarget = HostLightTex;
	}
	pass Composite
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_Composite;
		RenderTarget0 = CompositeTex;
		RenderTarget1 = McInfoTex;
	}
	pass Bright
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_Bright;
		RenderTarget = McBrightTex;
	}
	pass Final
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_Final;
	}
}
