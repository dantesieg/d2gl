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

uniform mat4 u_MVP;

out vec2 v_TexCoord;
out vec4 v_Color1;
out vec4 v_Color2;
flat out ivec2 v_TexIds;
flat out uvec4 v_Flags;

void main()
{
	gl_Position = u_MVP * vec4(Position, 0.0, 1.0);
	v_TexCoord = TexCoord;
	v_Color1 = Color1.bgra;
	v_Color2 = Color2.abgr;
	v_TexIds = TexIds;
	v_Flags = Flags;
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
uniform float u_SpellGlow; // additive draws feed the light buffer at this strength
uniform float u_EmitRGB; // [HDR] float_pipeline + diffuse_shoulder: FragEmit = rgb share of additive light in FragColor
uniform float u_AddScale; // [HDR] float_pipeline: additive world draws scaled above 1.0 (0/1 = off)

in vec2 v_TexCoord;
in vec4 v_Color1;
in vec4 v_Color2;
flat in ivec2 v_TexIds;
flat in uvec4 v_Flags;

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
		if (u_EmitRGB > 0.5 && u_BlendIndex == 1)
			FragEmit = vec4(1.0); // multiply by 1 keeps the additive share
		return;
	}

	if (v_Flags.y == 1u)
		FragColor = v_Color2;
	else if (v_Flags.y == 2u) // local = iterated (per-vertex colour)
		FragColor = v_Color1;
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
	FragColor.a = v_Flags.z == 1u ? v_Color2.a : (v_Flags.z == 2u ? v_Color1.a : 1.0);

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
			FragLight = vec4(FragColor.rgb * u_SpellGlow, 0.0);
	}

	// Emission attachment: additive world sprites (spells, fire, lightning, auras) add their luma;
	// opaque draws write 0 (One_Zero, occludes earlier glow); alpha draws write 0 at their coverage
	// (SAlpha_OneMinusSAlpha, attenuates); multiply adds 0 (One_One).
	if (u_BlendIndex == 2) {
		if (v_Flags.w == 0u || v_Flags.w == 11u)
			FragEmit = vec4(dot(FragColor.rgb, vec3(0.299, 0.587, 0.114)), 0.0, 0.0, 0.0);
	} else if (u_BlendIndex == 3 && sk != 4u) // alpha shadows paint no colour
		FragEmit = vec4(0.0, 0.0, 0.0, FragColor.a);

	// Float pipeline: additive world light (spells, fire) lands above 1.0 in the float game buffer.
	if (u_AddScale > 1.0 && u_BlendIndex == 2 && (v_Flags.w == 0u || v_Flags.w == 11u))
		FragColor.rgb *= u_AddScale;

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

	// Float pipeline diffuse shoulder: FragEmit tracks exactly the additive share of FragColor, so the
	// PreFx shoulder can split colour = diffuse + additive. Its blends mirror the colour blends:
	// opaque replaces (0), alpha attenuates (rgb 0, a = coverage), additive adds, multiply multiplies.
	if (u_EmitRGB > 0.5) {
		if (u_BlendIndex == 2)
			FragEmit = (v_Flags.w == 0u || v_Flags.w == 11u) ? vec4(FragColor.rgb, 0.0) : vec4(0.0);
		else if (u_BlendIndex == 1)
			FragEmit = vec4(FragColor.rgb, 1.0);
		else if (u_BlendIndex == 3)
			FragEmit = vec4(0.0, 0.0, 0.0, FragColor.a);
		else
			FragEmit = vec4(0.0);
	}
}

#endif
