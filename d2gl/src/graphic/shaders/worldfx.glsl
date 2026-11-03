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


// World-only effects run in the PreFx stage (UI is untouched):
//   0: palette debanding (edge-preserving smoothing of low-contrast steps + dither)
//   1: water reflections (pixels marked 0.05 in the mask attachment by water floors)

#ifdef VERTEX

layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 5) in uvec4 Flags;

uniform mat4 u_MVP;

out vec2 v_TexCoord;
flat out uvec4 v_Flags;

void main()
{
	gl_Position = u_MVP * vec4(Position, 0.0, 1.0);
	v_TexCoord = TexCoord;
	v_Flags = Flags;
}

// =============================================================
#elif FRAGMENT

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 FragColorMap;
layout(location = 2) out vec4 FragColorMask;

uniform sampler2D u_Texture;
uniform sampler2D u_MaskTexture;
uniform float u_Strength;
uniform float u_Time;

in vec2 v_TexCoord;
flat in uvec4 v_Flags;

// Interleaved gradient noise: cheap, blue-noise-like.
float Noise(vec2 p)
{
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec3 Deband()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	ivec2 sz = textureSize(u_Texture, 0) - 1;
	vec3 c = texelFetch(u_Texture, p, 0).rgb;
	float n = Noise(gl_FragCoord.xy);
	float a = n * 6.2831853;
	ivec2 o = ivec2(vec2(cos(a), sin(a)) * mix(3.0, 9.0, fract(n * 7.31)));
	vec3 s1 = texelFetch(u_Texture, clamp(p + o, ivec2(0), sz), 0).rgb;
	vec3 s2 = texelFetch(u_Texture, clamp(p - o, ivec2(0), sz), 0).rgb;
	vec3 s3 = texelFetch(u_Texture, clamp(p + ivec2(-o.y, o.x), ivec2(0), sz), 0).rgb;
	vec3 s4 = texelFetch(u_Texture, clamp(p - ivec2(-o.y, o.x), ivec2(0), sz), 0).rgb;
	vec3 avg = (s1 + s2 + s3 + s4) * 0.25;
	vec3 d = max(max(abs(s1 - c), abs(s2 - c)), max(abs(s3 - c), abs(s4 - c)));
	float th = mix(0.012, 0.04, u_Strength);
	// Only smooth where every tap is within a palette step (no outlines / text).
	float w = 1.0 - smoothstep(th * 0.5, th, max(d.r, max(d.g, d.b)));
	vec3 res = mix(c, avg, w);
	res += (Noise(gl_FragCoord.xy + 17.0) - 0.5) * (1.5 / 255.0) * w;
	return res;
}

bool IsWater(ivec2 p)
{
	float m = texelFetch(u_MaskTexture, p, 0).r;
	return m > 0.02 && m < 0.09;
}

vec3 Water()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	ivec2 sz = textureSize(u_Texture, 0) - 1;
	vec3 c = texelFetch(u_Texture, p, 0).rgb;
	if (!IsWater(p))
		return c;

	// Find the shore above (screen up = +y), then mirror about it.
	int edge = -1;
	for (int i = 1; i <= 16; i++) {
		int y = p.y + i * 4;
		if (y > sz.y)
			break;
		if (!IsWater(ivec2(p.x, y))) {
			edge = y;
			break;
		}
	}
	if (edge < 0)
		return c;

	float dist = float(edge - p.y);
	vec2 f = gl_FragCoord.xy;
	vec2 rip = vec2(sin(f.y * 0.35 + u_Time * 2.1) + 0.5 * sin(f.x * 0.09 + f.y * 0.13 - u_Time * 1.4), 0.6 * sin(f.x * 0.05 - u_Time * 1.7));
	rip *= 1.0 + dist * 0.03;
	ivec2 r = clamp(ivec2(vec2(p.x, edge + int(dist * 0.8)) + rip * 1.5), ivec2(0), sz);
	vec3 refl = texelFetch(u_Texture, r, 0).rgb;

	float w = u_Strength * 0.45 * (1.0 - dist / 68.0);
	w *= 1.0 - smoothstep(0.08, 0.3, c.r - c.b); // keep lava / blood out
	w *= 0.85 + 0.15 * sin(f.x * 0.21 + f.y * 0.5 + u_Time * 3.0);
	return mix(c, refl, clamp(w, 0.0, 1.0));
}

void main()
{
	FragColor = vec4(v_Flags.x == 0u ? Deband() : Water(), 1.0);
	FragColorMap = vec4(0.0);
	FragColorMask = vec4(0.0);
}

#endif
