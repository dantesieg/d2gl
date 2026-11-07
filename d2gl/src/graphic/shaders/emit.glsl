/*
	D2GL: Diablo 2 LoD Glide/DDraw to OpenGL Wrapper.
	Copyright (C) 2023  Bayaraa

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
*/

// UI layer composite: the upscale shader ran on the world snapshot only; this pass
// draws every pixel the UI changed (frame != world snapshot) on top, upscaled with
// sharp-bilinear so panels and text stay crisp.

#ifdef VERTEX

layout(location = 0) in vec2 Position;

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

uniform sampler2D u_EmitTexture;
uniform float u_Gain;
uniform sampler2D u_Texture; // mode 1: game colour copy (prefx texture)
uniform int u_Mode;          // 0: emissive alpha pass, 1: float-pipeline diffuse shoulder
uniform float u_Knee;

in vec2 v_TexCoord;

void main()
{
	if (u_Mode == 1) {
		// colour = diffuse + additive; soft-clip diffuse to paper white (C1 shoulder from u_Knee,
		// asymptote 1.0, on the max channel so hue is kept), additive light stays above 1.0.
		vec3 add = max(texture(u_EmitTexture, v_TexCoord).rgb, 0.0);
		vec3 d = max(texture(u_Texture, v_TexCoord).rgb - add, 0.0);
		float m = max(d.r, max(d.g, d.b));
		if (m > u_Knee) {
			float w = 1.0 - u_Knee;
			d *= (u_Knee + w * (1.0 - exp(-(m - u_Knee) / w))) / m;
		}
		FragColor = vec4(d + add, 1.0);
		return;
	}
	float e = clamp(texture(u_EmitTexture, v_TexCoord).r * u_Gain, 0.0, 1.0);
	FragColor = vec4(0.0, 0.0, 0.0, 1.0 - e);
}

#endif
