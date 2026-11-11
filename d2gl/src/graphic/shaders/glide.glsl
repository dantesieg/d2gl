/*
	D2GL: Diablo 2 LoD Glide/DDraw to OpenGL Wrapper.
	Copyright (C) 2023  Bayaraa

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifdef VERTEX

layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 2) in vec4 Color1;
layout(location = 3) in vec4 Color2;
layout(location = 4) in ivec2 TexIds;
layout(location = 5) in uvec4 Flags;
layout(location = 7) in uvec4 TexRect;

uniform mat4 u_MVP;

out vec2 v_TexCoord;
out vec4 v_Color1;
out vec4 v_Color2;
flat out ivec2 v_TexIds;
flat out uvec4 v_Flags;
flat out uvec4 v_TexRect;

void main()
{
	gl_Position = u_MVP * vec4(Position, 0.0, 1.0);
	v_TexCoord = TexCoord;
	v_Color1 = Color1.bgra;
	v_Color2 = Color2.abgr;
	v_TexIds = TexIds;
	v_Flags = Flags;
	v_TexRect = TexRect;
}

// =============================================================
#elif FRAGMENT

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 FragColorMap;
layout(location = 2) out vec4 FragColorMask;
layout(location = 3) out vec4 FragColorShadow; // r: shadow/AO darkness, g: occluder (additive)
layout(location = 4) out vec4 FragLight; // game vertex lighting (world), see dynamic_light_source
layout(location = 5) out vec4 FragEmit; // r: HDR emission of additive world sprites ([HDR] emissive)

layout(std140) uniform ubo_Colors {
	vec4 u_Palette[256];
	vec4 u_Gamma[256];
};

uniform sampler2DArray u_Texture;
uniform int u_BlendIndex; // pipeline blend index: 0 opaque, 1 multiply, 2 additive, 3 alpha
uniform float u_LightFlicker; // [Graphic] light_flicker: 0 = off, spell/fire light pulses up to +-45 %
uniform float u_Time;         // seconds, wraps every hour
uniform float u_HotPixels;    // [HDR] hot_pixels: bright saturated warm pixels of opaque world sprites emit (0 = off)
uniform float u_UnitEmit;     // [HDR] unit_emissive: additive draws on players/monsters (auras, glows) scale emission and floor light (1 = as spells)
uniform float u_OrbGlow;      // [HDR] orb_glow: health/mana orb liquid (flag z bit 6, needs emissive)
uniform float u_ItemGlow;     // [HDR] item_glow: emission of valuable ground items (needs emissive)
uniform float u_SpellGlow; // additive draws feed the light buffer at this strength

in vec2 v_TexCoord;
in vec4 v_Color1;
in vec4 v_Color2;
flat in ivec2 v_TexIds;
flat in uvec4 v_Flags;
flat in uvec4 v_TexRect;

uniform vec2 u_NativeScale; // [Graphic] native_render: screen px per game px per axis (<= 1 = off)
uniform int u_NativeFilter;  // 0 nearest, 1 sharp (pixel-exact inside, blended only at texel borders), 2 smooth

// native_render: palette sprites are scaled per sprite as they are drawn. Filtering runs on palette
// colours (4 texelFetch, clamped to the sprite's own atlas rect so neighbours never bleed in);
// colour-keyed texels drop out of the blend, coverage < 0.5 is discarded (crisp, fringe-free outlines).
bool nativeSample(out vec4 col)
{
	vec2 p = v_TexCoord * 512.0 - 0.5;
	vec2 i0 = floor(p);
	vec2 f = p - i0;
	if (u_NativeFilter == 1)
		f = clamp((f - 0.5) * u_NativeScale + 0.5, 0.0, 1.0);
	vec2 lo = vec2(v_TexRect.xy);
	vec2 hi = lo + vec2(v_TexRect.zw) - 1.0;
	bool key = (v_Flags.x & 15u) == 1u;
	vec4 acc = vec4(0.0);
	float wsum = 0.0;
	for (int k = 0; k < 4; k++) {
		vec2 o = vec2(k & 1, k >> 1);
		float w = (o.x > 0.5 ? f.x : 1.0 - f.x) * (o.y > 0.5 ? f.y : 1.0 - f.y);
		ivec2 t = ivec2(clamp(i0 + o, lo, hi));
		float red = texelFetch(u_Texture, ivec3(t, v_TexIds.x), 0).r;
		if (key && red == 0.0)
			continue;
		acc += w * u_Palette[int(red * 255.0 + 0.5)];
		wsum += w;
	}
	if (wsum < 0.5)
		return false;
	col = acc / wsum;
	return true;
}

void main()
{
	// v_Flags.x high nibble: 1 multiply shadow, 2 opaque occluder, 3 AO ellipse, 4 alpha shadow, 5 alpha occluder
	uint sk = v_Flags.x >> 4u;
	FragColorShadow = vec4(0.0);
	FragLight = vec4(0.0);
	FragEmit = vec4(0.0);
	if (sk == 3u) {
		float e = 1.0 - smoothstep(0.15, 1.0, length(v_TexCoord * 2.0 - 1.0));
		FragColor = vec4(1.0, 1.0, 1.0, 0.0);
		FragColorMap = vec4(0.0);
		FragColorMask = vec4(0.0);
		FragColorShadow = vec4(e * v_Color2.a, 0.0, 0.0, 0.0);
		return;
	}

	if (v_Flags.y == 1u)
		FragColor = v_Color2;
	else if (v_Flags.y == 2u) // local = iterated (per-vertex colour)
		FragColor = v_Color1;
	else if (max(u_NativeScale.x, u_NativeScale.y) > 1.0 && u_NativeFilter > 0 && v_TexRect.z > 0u)
	{
		vec4 pal;
		if (!nativeSample(pal))
			discard;
		FragColor = v_Color1 * pal;
	}
	else
	{
		float red = texture(u_Texture, vec3(v_TexCoord, v_TexIds.x)).r;

		if ((v_Flags.x & 15u) == 1u && red == 0.0)
			discard;

		FragColor = v_Color1 * u_Palette[int(red * 255)];
	}

	FragColor.r = u_Gamma[int(FragColor.r * 255)].r;
	FragColor.g = u_Gamma[int(FragColor.g * 255)].g;
	FragColor.b = u_Gamma[int(FragColor.b * 255)].b;
	FragColor.a = (v_Flags.z & 15u) == 1u ? v_Color2.a : ((v_Flags.z & 15u) == 2u ? v_Color1.a : 1.0); // z >> 4: item glow level

	FragColorMap = vec4(0.0);
	if (v_Flags.w > 0u && v_Flags.w < 10u)
	{
		FragColorMap = vec4(FragColor.rgb, 0.9);
		if (v_Flags.w > 1u)
			FragColor = vec4(0.0);
	}

	// Mask: 10 = cursor (r > 0.1). Water floors (11) write a low 0.05 marker;
	// later world draws (0) cover it again with their own alpha.
	FragColorMask = vec4(0.0, 0.0, 0.0, v_Flags.w == 0u ? FragColor.a : 0.0);
	if(v_Flags.w == 10u)
		FragColorMask = FragColor;
	else if(v_Flags.w == 11u)
		FragColorMask = vec4(0.05, 0.0, 0.0, FragColor.a);

	// Light attachment: opaque textured draws store their per-vertex light (pargb,
	// no texture); additive draws (spells, fire) add their colour as emitted light.
	// Attachment blend is One_Zero for opaque and One_One otherwise, so 0 = no change.
	// World (0) and water floors (11) count; FragColor is read before shadow edits.
	if ((v_Flags.w == 0u || v_Flags.w == 11u) && v_Flags.y == 0u) {
		if (u_BlendIndex == 0)
			FragLight = vec4(v_Color1.rgb, 1.0);
		else if (u_BlendIndex == 2)
		{
			float fl = 1.0;
			if (u_LightFlicker > 0.0) {
				// smooth value noise in time, varied over 160 px screen cells; lightning (blue) flickers faster
				vec2 cell = gl_FragCoord.xy / 160.0;
				vec2 ci0 = floor(cell);
				vec2 cf = smoothstep(0.0, 1.0, cell - ci0);
				float rate = FragColor.b > FragColor.r * 1.2 ? 17.0 : 7.0;
				float t = u_Time * rate;
				float ti = floor(t);
				float tf = smoothstep(0.0, 1.0, t - ti);
				float n00 = mix(fract(sin(dot(ci0, vec2(12.9898, 78.233)) + ti * 0.731) * 43758.5453), fract(sin(dot(ci0, vec2(12.9898, 78.233)) + (ti + 1.0) * 0.731) * 43758.5453), tf);
				float n10 = mix(fract(sin(dot(ci0 + vec2(1, 0), vec2(12.9898, 78.233)) + ti * 0.731) * 43758.5453), fract(sin(dot(ci0 + vec2(1, 0), vec2(12.9898, 78.233)) + (ti + 1.0) * 0.731) * 43758.5453), tf);
				float n01 = mix(fract(sin(dot(ci0 + vec2(0, 1), vec2(12.9898, 78.233)) + ti * 0.731) * 43758.5453), fract(sin(dot(ci0 + vec2(0, 1), vec2(12.9898, 78.233)) + (ti + 1.0) * 0.731) * 43758.5453), tf);
				float n11 = mix(fract(sin(dot(ci0 + vec2(1, 1), vec2(12.9898, 78.233)) + ti * 0.731) * 43758.5453), fract(sin(dot(ci0 + vec2(1, 1), vec2(12.9898, 78.233)) + (ti + 1.0) * 0.731) * 43758.5453), tf);
				float n = mix(mix(n00, n10, cf.x), mix(n01, n11, cf.x), cf.y);
				fl = 1.0 + (n * 2.0 - 1.0) * 0.45 * u_LightFlicker;
			}
			FragLight = vec4(FragColor.rgb * u_SpellGlow * fl * ((v_Flags.z & 128u) != 0u ? u_UnitEmit : 1.0), 0.0);
		}
	}

	// Emission attachment: additive world sprites (spells, fire, lightning, auras) add their luma;
	// opaque draws write 0 (One_Zero, occludes earlier glow); alpha draws write 0 at their coverage
	// (SAlpha_OneMinusSAlpha, attenuates); multiply adds 0 (One_One).
	if (u_BlendIndex == 2) {
		if (v_Flags.w == 0u || v_Flags.w == 11u)
			FragEmit = vec4(dot(FragColor.rgb, vec3(0.299, 0.587, 0.114)) * ((v_Flags.z & 128u) != 0u ? u_UnitEmit : 1.0), 0.0, 0.0, 0.0);
	} else if (u_BlendIndex == 3 && sk != 4u) // alpha shadows paint no colour
		FragEmit = vec4(0.0, 0.0, 0.0, FragColor.a);
	// [HDR] hot_pixels: flames, braziers, lava (opaque or alpha world sprites, not shadows)
	if (u_HotPixels > 0.0 && (u_BlendIndex == 0 || u_BlendIndex == 3) && sk == 0u && v_Flags.w == 0u && v_Flags.y == 0u) {
		float hmx = max(FragColor.r, max(FragColor.g, FragColor.b));
		float hmn = min(FragColor.r, min(FragColor.g, FragColor.b));
		float hs = hmx > 0.0 ? (hmx - hmn) / hmx : 0.0;
		float hw = clamp((FragColor.r - FragColor.b) * 2.0, 0.0, 1.0);
		float he = smoothstep(0.65, 0.95, hmx) * smoothstep(0.4, 0.7, hs) * hw * u_HotPixels;
		if (he > 0.0)
			FragEmit = vec4(he, 0.0, 0.0, u_BlendIndex == 3 ? FragColor.a : 1.0);
	}
	// [HDR] item_glow: valuable ground items (z >> 4 = 1 rune, 2 set, 3 unique) emit their own colour's luma
	uint ig = (v_Flags.z >> 4u) & 3u;
	if (ig > 0u && u_ItemGlow > 0.0 && (u_BlendIndex == 0 || u_BlendIndex == 3) && sk == 0u)
		FragEmit = vec4(dot(FragColor.rgb, vec3(0.299, 0.587, 0.114)) * u_ItemGlow * (0.5 + 0.25 * float(ig)), 0.0, 0.0, u_BlendIndex == 3 ? FragColor.a : 1.0);
	// [HDR] orb_glow: the orb liquid emits by its brightest channel (blue mana counts as much as red life)
	if ((v_Flags.z & 64u) != 0u && u_OrbGlow > 0.0 && u_BlendIndex == 0)
		FragEmit = vec4(max(FragColor.r, max(FragColor.g, FragColor.b)) * u_OrbGlow, 0.0, 0.0, 1.0);

	if (sk == 1u) {
		FragColorShadow.r = 1.0 - dot(FragColor.rgb, vec3(0.299, 0.587, 0.114));
		FragColor = vec4(1.0);
	} else if (sk == 4u) {
		FragColorShadow.r = FragColor.a * (1.0 - dot(FragColor.rgb, vec3(0.299, 0.587, 0.114)));
		FragColor.a = 0.0;
	} else if (sk == 2u)
		FragColorShadow.g = 1.0;
	else if (sk == 5u)
		FragColorShadow.g = FragColor.a;
}

#endif
