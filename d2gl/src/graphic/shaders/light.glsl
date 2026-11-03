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

// Screen-space dynamic lighting: bright, saturated (mostly warm) pixels of
// the world are treated as emitters, blurred wide at low resolution and used
// to light the surrounding pixels. Runs in the PreFx stage, so UI is untouched.
// u_Source 1: the light comes instead from the game's own vertex lighting
// (GlideVertex pargb of opaque world draws, captured by glide.glsl into
// u_VLightTexture). Only light above the ambient level is kept, so uniform
// ambient light changes nothing; coloured light is boosted.

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
uniform sampler2D u_LightTexture1;
uniform sampler2D u_LightTexture2;
uniform sampler2D u_VLightTexture; // full res: rgb = vertex light, a = opaque coverage
uniform vec2 u_RelSize;
uniform float u_Intensity;
uniform int u_Source;

in vec2 v_TexCoord;
flat in uvec4 v_Flags;

vec3 Emission(vec2 uv)
{
	vec3 c = texture(u_Texture, uv).rgb;
	float mx = max(c.r, max(c.g, c.b));
	float mn = min(c.r, min(c.g, c.b));
	float sat = (mx - mn) / max(mx, 0.0001);
	float bright = smoothstep(0.55, 0.92, mx);
	float warm = clamp((c.r - c.b) * 2.0, 0.0, 1.0);
	float w = bright * (smoothstep(0.2, 0.55, sat) * mix(0.45, 1.0, warm) + smoothstep(0.9, 1.0, mn) * 0.5);
	return c * w;
}

vec3 Extract()
{
	vec2 o = u_RelSize * 0.25;
	vec3 e = Emission(v_TexCoord + vec2(-o.x, -o.y));
	e += Emission(v_TexCoord + vec2(o.x, -o.y));
	e += Emission(v_TexCoord + vec2(-o.x, o.y));
	e += Emission(v_TexCoord + vec2(o.x, o.y));
	return e * 0.25;
}

// Ambient = darkest covered sample of a 5x5 grid over the screen (the game light
// never drops below ambient). Uncovered (void) samples are skipped.
vec3 Ambient()
{
	vec3 amb = vec3(1.0);
	float lo = 2.0;
	for (int y = 0; y < 5; y++) {
		for (int x = 0; x < 5; x++) {
			vec4 s = texture(u_VLightTexture, (vec2(x, y) + 0.5) * 0.2);
			float l = dot(s.rgb, vec3(0.299, 0.587, 0.114));
			if (s.a > 0.5 && l < lo) {
				lo = l;
				amb = s.rgb;
			}
		}
	}
	return amb;
}

vec3 VExtract()
{
	vec2 o = u_RelSize * 0.25;
	vec4 s = texture(u_VLightTexture, v_TexCoord + vec2(-o.x, -o.y));
	s += texture(u_VLightTexture, v_TexCoord + vec2(o.x, -o.y));
	s += texture(u_VLightTexture, v_TexCoord + vec2(-o.x, o.y));
	s += texture(u_VLightTexture, v_TexCoord + vec2(o.x, o.y));
	s *= 0.25;
	vec3 e = max(s.rgb - Ambient() * s.a, 0.0);
	float mx = max(e.r, max(e.g, e.b));
	float sat = (mx - min(e.r, min(e.g, e.b))) / max(mx, 0.0001);
	return e * (0.25 + 1.75 * sat);
}

vec3 Blur(vec2 dir)
{
	vec3 res = texture(u_LightTexture2, v_TexCoord).rgb * 0.1964825501511404;
	res += (texture(u_LightTexture2, v_TexCoord + dir * 1.411764705882353).rgb + texture(u_LightTexture2, v_TexCoord - dir * 1.411764705882353).rgb) * 0.2969069646728344;
	res += (texture(u_LightTexture2, v_TexCoord + dir * 3.2941176470588234).rgb + texture(u_LightTexture2, v_TexCoord - dir * 3.2941176470588234).rgb) * 0.09447039785044732;
	res += (texture(u_LightTexture2, v_TexCoord + dir * 5.176470588235294).rgb + texture(u_LightTexture2, v_TexCoord - dir * 5.176470588235294).rgb) * 0.010381362401148057;
	return res;
}

void main()
{
	switch (v_Flags.x) {
		case 0u: FragColor = vec4(u_Source == 1 ? VExtract() : Extract(), 1.0); break;
		case 1u: FragColor = vec4(Blur(vec2(u_RelSize.x, 0.0)), 1.0); break;
		case 2u: FragColor = vec4(Blur(vec2(0.0, u_RelSize.y)), 1.0); break;
		case 3u:
			vec3 base = texture(u_Texture, v_TexCoord).rgb;
			vec3 light = texture(u_LightTexture1, v_TexCoord).rgb * u_Intensity;
			if (u_Source == 0)
				light = mix(light, light * vec3(1.0, 0.78, 0.5), 0.35);
			vec3 lit = base + base * light * 2.5 + light * 0.18;
			FragColor = vec4(clamp(lit, 0.0, 1.0), 1.0);
		break;
		case 4u: // debug: raw light buffer, ambient swatch in the top-left corner
			FragColor = vec4(v_TexCoord.x < 0.06 && v_TexCoord.y < 0.06 ? Ambient() : texture(u_VLightTexture, v_TexCoord).rgb, 1.0);
		break;
	}

	FragColorMap = vec4(0.0);
	FragColorMask = vec4(0.0);
}

#endif
