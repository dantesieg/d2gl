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

uniform sampler2D u_EmitTexture; // 5th game attachment: emission (R8) / additive share (RGBA16F, float pipeline)
uniform float u_Gain;
uniform sampler2D u_Texture; // game colour copy (prefx texture)
uniform sampler2D u_Expo;    // 1x1 adapted exposure in stops (float pipeline auto exposure)
uniform int u_Mode;          // 0: emissive alpha pass, 1: exposure + diffuse shoulder, 3: exposure adapt (1x1)
uniform float u_Knee;        // shoulder knee, >= 1.0 = shoulder off
uniform int u_ExpoOn;
uniform float u_Key;         // target linear luminance of the diffuse log-average
uniform float u_Strength;    // 0..1 share of full normalisation
uniform vec2 u_ExpoRange;    // min, max stops
uniform vec2 u_Rate;         // per-frame blend weight: x darkening, y brightening
uniform int u_Reset;
uniform sampler2D u_Bloom;   // HDR bloom level (modes 5-7)
uniform vec2 u_SrcTexel;     // 1 / source size
uniform float u_Intensity;
uniform float u_Levels;

in vec2 v_TexCoord;

void main()
{
	if (u_Mode == 1) {
		// colour = diffuse + additive. Exposure scales both (signal domain: 2^(stops/2.2));
		// diffuse is soft-clipped to paper white (C1 shoulder from u_Knee, asymptote 1.0, on the
		// max channel so hue is kept); additive light stays above 1.0.
		vec3 add = max(texture(u_EmitTexture, v_TexCoord).rgb, 0.0);
		vec3 d = max(texture(u_Texture, v_TexCoord).rgb - add, 0.0);
		if (u_ExpoOn == 1) {
			float f = exp2(texture(u_Expo, vec2(0.5)).r / 2.2);
			d *= f;
			add *= f;
		}
		float m = max(d.r, max(d.g, d.b));
		if (m > u_Knee) {
			float w = 1.0 - u_Knee;
			d *= (u_Knee + w * (1.0 - exp(-(m - u_Knee) / w))) / m;
		}
		FragColor = vec4(d + add, 1.0);
		return;
	}
	if (u_Mode == 3) {
		// Log-average linear luminance of the diffuse world (additive light excluded so spell
		// flashes do not pump); near-black void is skipped. 24x16 samples, 1x1 target.
		float sum = 0.0;
		float n = 0.0;
		for (int y = 0; y < 16; y++) {
			for (int x = 0; x < 24; x++) {
				vec2 uv = (vec2(x, y) + 0.5) / vec2(24.0, 16.0);
				vec3 d = max(textureLod(u_Texture, uv, 0.0).rgb - max(textureLod(u_EmitTexture, uv, 0.0).rgb, 0.0), 0.0);
				float l = dot(d, vec3(0.2126, 0.7152, 0.0722));
				if (l > 0.02) {
					sum += log2(pow(l, 2.2));
					n += 1.0;
				}
			}
		}
		float target = 0.0;
		if (n > 0.0)
			target = clamp(u_Strength * (log2(u_Key) - sum / n), u_ExpoRange.x, u_ExpoRange.y);
		float prev = texture(u_Expo, vec2(0.5)).r;
		float r = target < prev ? u_Rate.x : u_Rate.y;
		FragColor = vec4(u_Reset == 1 ? target : mix(prev, target, r), 0.0, 0.0, 1.0);
		return;
	}
	// HDR bloom (float pipeline, pre-UI): only energy above paper white, dual-filter (Kawase) chain.
	if (u_Mode == 4) { // prefilter: 4x4 box from the full-res colour copy, keep what is above 1.0 (hue kept)
		vec2 h = u_SrcTexel;
		vec3 c = texture(u_Texture, v_TexCoord + vec2(-h.x, -h.y)).rgb + texture(u_Texture, v_TexCoord + vec2(h.x, -h.y)).rgb;
		c += texture(u_Texture, v_TexCoord + vec2(-h.x, h.y)).rgb + texture(u_Texture, v_TexCoord + vec2(h.x, h.y)).rgb;
		c *= 0.25;
		float m = max(c.r, max(c.g, c.b));
		c *= min(max(m - 1.0, 0.0), 16.0) / max(m, 0.0001);
		FragColor = vec4(c, 1.0);
		return;
	}
	if (u_Mode == 5) { // dual-filter down
		vec2 h = u_SrcTexel * 0.5;
		vec3 c = texture(u_Bloom, v_TexCoord).rgb * 4.0;
		c += texture(u_Bloom, v_TexCoord - h).rgb + texture(u_Bloom, v_TexCoord + h).rgb;
		c += texture(u_Bloom, v_TexCoord + vec2(h.x, -h.y)).rgb + texture(u_Bloom, v_TexCoord - vec2(h.x, -h.y)).rgb;
		FragColor = vec4(c * 0.125, 1.0);
		return;
	}
	if (u_Mode == 6) { // dual-filter up (tent), added onto the level below (One_One)
		vec2 h = u_SrcTexel * 0.5;
		vec3 c = texture(u_Bloom, v_TexCoord + vec2(-h.x * 2.0, 0.0)).rgb + texture(u_Bloom, v_TexCoord + vec2(h.x * 2.0, 0.0)).rgb;
		c += texture(u_Bloom, v_TexCoord + vec2(0.0, h.y * 2.0)).rgb + texture(u_Bloom, v_TexCoord + vec2(0.0, -h.y * 2.0)).rgb;
		c += (texture(u_Bloom, v_TexCoord + vec2(-h.x, h.y)).rgb + texture(u_Bloom, v_TexCoord + vec2(h.x, h.y)).rgb) * 2.0;
		c += (texture(u_Bloom, v_TexCoord + vec2(h.x, -h.y)).rgb + texture(u_Bloom, v_TexCoord + vec2(-h.x, -h.y)).rgb) * 2.0;
		FragColor = vec4(c / 12.0, 1.0);
		return;
	}
	if (u_Mode == 7) { // composite onto the game colour (One_One), levels averaged
		FragColor = vec4(texture(u_Bloom, v_TexCoord).rgb * (u_Intensity / u_Levels), 0.0);
		return;
	}
	float e = clamp(texture(u_EmitTexture, v_TexCoord).r * u_Gain, 0.0, 1.0);
	FragColor = vec4(0.0, 0.0, 0.0, 1.0 - e);
}

#endif
