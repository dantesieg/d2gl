/*
	D2GL: Diablo 2 LoD Glide/DDraw to OpenGL Wrapper.
	Copyright (C) 2023  Bayaraa

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
*/

// HDR emissive highlights: writes default-framebuffer alpha only (colour mask r,g,b off).
// alpha = 1 - emission: 1 = no boost, so later UI / HD text draws blending alpha toward 1 cancel it.
// hdr_shader.h reads e = 1 - alpha and lifts those pixels above paper white.

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

uniform sampler2D u_EmitTexture; // 6th game attachment: emission (R8, or R16F graded)
uniform float u_Gain;
uniform int u_Graded; // 1 = R16F unbounded emission, alpha = 1 / (1 + e)

in vec2 v_TexCoord;

void main()
{
	if (u_Graded == 1) {
		float g = max(texture(u_EmitTexture, v_TexCoord).r * u_Gain, 0.0);
		FragColor = vec4(0.0, 0.0, 0.0, 1.0 / (1.0 + g));
		return;
	}
	float e = clamp(texture(u_EmitTexture, v_TexCoord).r * u_Gain, 0.0, 1.0);
	FragColor = vec4(0.0, 0.0, 0.0, 1.0 - e);
}

#endif
