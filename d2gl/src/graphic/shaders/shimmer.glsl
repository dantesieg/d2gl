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

// Portal / waypoint shimmer: animated refraction ripple and a soft pulsing glow
// around sources recorded while the world was drawn. Runs in PreFx (world only).

#ifdef VERTEX

layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 TexCoord;

uniform mat4 u_MVP;

out vec2 v_TexCoord;

void main()
{
	gl_Position = u_MVP * vec4(Position, 0.0, 1.0);
	v_TexCoord = TexCoord;
}

// =============================================================
#elif FRAGMENT

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 FragColorMap;
layout(location = 2) out vec4 FragColorMask;

uniform sampler2D u_Texture;
uniform vec4 u_Sources[8]; // xy: game px (y down), z: radius px, w: type
uniform int u_Count;
uniform vec2 u_Size;
uniform float u_Time;

in vec2 v_TexCoord;

void main()
{
	vec2 px = vec2(v_TexCoord.x, 1.0 - v_TexCoord.y) * u_Size;
	vec2 offs = vec2(0.0);
	vec3 glow = vec3(0.0);

	for (int i = 0; i < u_Count; i++) {
		vec4 s = u_Sources[i];
		vec2 d = px - s.xy;
		// portals are upright ovals, waypoints lie flat on the isometric floor
		vec2 e = s.w > 1.5 ? vec2(d.x, d.y * 2.0) : vec2(d.x * 1.3, d.y * 0.75);
		float len = length(e);
		float r = len / s.z;
		if (r < 1.0) {
			float fi = float(i);
			float fall = 1.0 - smoothstep(0.3, 1.0, r);
			float wave = sin(r * 22.0 - u_Time * 4.0 + fi * 2.3) * 0.7 + sin(r * 9.0 + u_Time * 2.6 + fi) * 0.3;
			offs += (d / max(len, 0.001)) * wave * fall * 1.8;

			vec3 col = s.w > 1.5 ? vec3(0.62, 0.8, 1.0) : (s.w > 0.5 ? vec3(1.0, 0.24, 0.14) : vec3(0.28, 0.55, 1.0));
			float pulse = 0.7 + 0.3 * sin(u_Time * 2.4 + fi * 1.7);
			float g = 1.0 - smoothstep(0.0, 1.0, r);
			glow += col * g * g * pulse * 0.38;
		}
	}

	vec3 c = texture(u_Texture, v_TexCoord + vec2(offs.x, -offs.y) / u_Size).rgb;
	c = 1.0 - (1.0 - c) * (1.0 - clamp(glow, 0.0, 1.0));
	FragColor = vec4(c, 1.0);
	FragColorMap = vec4(0.0);
	FragColorMask = vec4(0.0);
}

#endif
