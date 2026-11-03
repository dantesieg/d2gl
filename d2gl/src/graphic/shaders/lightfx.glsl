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


// Extensions to dynamic_light (light.glsl), run in the PreFx stage (world only).
// Flag 0 (1/8 res): light shafts. From each texel, march up the gradient of the
//   blurred light buffer (toward brighter light, so it works with any number of
//   lights), gathering light that is masked by world occluders (dark pixels).
// Flag 1 (full res): combine. Same as light.glsl case 3, plus pseudo normals
//   from the world luminance (Sobel, as a height map) shaded by N.L, where L is
//   the light buffer gradient, and the shafts added on top.

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

uniform sampler2D u_Texture;       // world (full res)
uniform sampler2D u_LightTexture1; // shafts (1/8 res)
uniform sampler2D u_LightTexture2; // blurred light (1/8 res)
uniform vec2 u_RelSize;            // 1 / light buffer size
uniform float u_Intensity;
uniform float u_Normals;           // 0 = off
uniform float u_Shafts;            // 0 = off

in vec2 v_TexCoord;
flat in uvec4 v_Flags;

const vec3 LUMA = vec3(0.299, 0.587, 0.114);

float L(vec2 uv) { return dot(texture(u_LightTexture2, uv).rgb, LUMA); }

vec2 LightGrad(vec2 uv)
{
	return vec2(L(uv + vec2(u_RelSize.x, 0.0)) - L(uv - vec2(u_RelSize.x, 0.0)), L(uv + vec2(0.0, u_RelSize.y)) - L(uv - vec2(0.0, u_RelSize.y)));
}

vec3 Shafts()
{
	vec2 uv = v_TexCoord;
	vec3 acc = vec3(0.0);
	float w = 1.0;
	for (int i = 0; i < 12; i++) {
		vec2 g = LightGrad(uv);
		float gl = length(g);
		if (gl < 0.0005)
			break;
		uv += g / gl * u_RelSize * 1.5;
		float occ = smoothstep(0.04, 0.3, dot(texture(u_Texture, uv).rgb, LUMA));
		acc += texture(u_LightTexture2, uv).rgb * occ * w;
		w *= 0.9;
	}
	return acc * 0.12;
}

float H(vec2 uv) { return dot(texture(u_Texture, uv).rgb, LUMA); }

void main()
{
	if (v_Flags.x == 0u) {
		FragColor = vec4(Shafts(), 1.0);
	} else {
		vec3 base = texture(u_Texture, v_TexCoord).rgb;
		vec3 light = texture(u_LightTexture2, v_TexCoord).rgb * u_Intensity;
		light = mix(light, light * vec3(1.0, 0.78, 0.5), 0.35);
		float shade = 1.0;
		if (u_Normals > 0.0) {
			vec2 px = 1.0 / vec2(textureSize(u_Texture, 0));
			float tl = H(v_TexCoord + vec2(-px.x, -px.y)), t = H(v_TexCoord + vec2(0.0, -px.y)), tr = H(v_TexCoord + vec2(px.x, -px.y));
			float l = H(v_TexCoord + vec2(-px.x, 0.0)), r = H(v_TexCoord + vec2(px.x, 0.0));
			float bl = H(v_TexCoord + vec2(-px.x, px.y)), b = H(v_TexCoord + vec2(0.0, px.y)), br = H(v_TexCoord + vec2(px.x, px.y));
			vec2 n = -vec2(tr + 2.0 * r + br - tl - 2.0 * l - bl, bl + 2.0 * b + br - tl - 2.0 * t - tr);
			vec2 ld = LightGrad(v_TexCoord);
			ld /= max(length(ld), 0.002);
			shade = clamp(1.0 + dot(n, ld) * u_Normals * 1.5, 0.15, 2.0);
		}
		vec3 lit = base + base * light * 2.5 * shade + light * 0.18;
		if (u_Shafts > 0.0)
			lit += texture(u_LightTexture1, v_TexCoord).rgb * u_Shafts * u_Intensity * (0.15 + base * 0.85);
		FragColor = vec4(clamp(lit, 0.0, 1.0), 1.0);
	}

	FragColorMap = vec4(0.0);
	FragColorMask = vec4(0.0);
}

#endif
