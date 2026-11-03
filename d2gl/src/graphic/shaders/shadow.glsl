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

// Soft shadows / ambient occlusion. The glide shader accumulates unit shadow
// and AO darkness in the red channel of game attachment 3 (instead of drawing
// shadows into the frame) and coverage of sprites drawn after them in green.
// Darkness is downsampled, blurred and multiplied into the world at the UI
// stage; green masks it sharply so sprites are never darkened by it.

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

uniform sampler2D u_ShadowMask;
uniform sampler2D u_ShadowTexture1;
uniform sampler2D u_ShadowTexture2;
uniform vec2 u_RelSize;
uniform float u_Softness;

in vec2 v_TexCoord;
flat in uvec4 v_Flags;

float Blur(vec2 dir)
{
	float res = texture(u_ShadowTexture2, v_TexCoord).r * 0.1964825501511404;
	res += (texture(u_ShadowTexture2, v_TexCoord + dir * 1.411764705882353).r + texture(u_ShadowTexture2, v_TexCoord - dir * 1.411764705882353).r) * 0.2969069646728344;
	res += (texture(u_ShadowTexture2, v_TexCoord + dir * 3.2941176470588234).r + texture(u_ShadowTexture2, v_TexCoord - dir * 3.2941176470588234).r) * 0.09447039785044732;
	res += (texture(u_ShadowTexture2, v_TexCoord + dir * 5.176470588235294).r + texture(u_ShadowTexture2, v_TexCoord - dir * 5.176470588235294).r) * 0.010381362401148057;
	return res;
}

void main()
{
	switch (v_Flags.x) {
		case 0u: {
			vec2 o = u_RelSize * 0.25;
			float d = texture(u_ShadowMask, v_TexCoord + vec2(-o.x, -o.y)).r;
			d += texture(u_ShadowMask, v_TexCoord + vec2(o.x, -o.y)).r;
			d += texture(u_ShadowMask, v_TexCoord + vec2(-o.x, o.y)).r;
			d += texture(u_ShadowMask, v_TexCoord + vec2(o.x, o.y)).r;
			FragColor = vec4(d * 0.25, 0.0, 0.0, 1.0);
		} break;
		case 1u: FragColor = vec4(Blur(vec2(u_RelSize.x * u_Softness, 0.0)), 0.0, 0.0, 1.0); break;
		case 2u: FragColor = vec4(Blur(vec2(0.0, u_RelSize.y * u_Softness)), 0.0, 0.0, 1.0); break;
		case 3u: {
			float dark = clamp(texture(u_ShadowTexture1, v_TexCoord).r, 0.0, 0.9);
			float occ = clamp(texture(u_ShadowMask, v_TexCoord).g, 0.0, 1.0);
			float f = 1.0 - dark * (1.0 - occ);
			FragColor = vec4(f, f, f, 1.0);
		} break;
	}
}

#endif
