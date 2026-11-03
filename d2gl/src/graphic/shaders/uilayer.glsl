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

uniform sampler2D u_FrameTexture;
uniform sampler2D u_WorldTexture;
uniform vec2 u_Scale;

in vec2 v_TexCoord;

void main()
{
	vec2 size = vec2(textureSize(u_FrameTexture, 0));
	vec2 texel = v_TexCoord * size;
	ivec2 nearest = ivec2(clamp(floor(texel), vec2(0.0), size - 1.0));

	vec3 diff = abs(texelFetch(u_FrameTexture, nearest, 0).rgb - texelFetch(u_WorldTexture, nearest, 0).rgb);
	if (max(diff.r, max(diff.g, diff.b)) < 0.5 / 255.0)
		discard;

	// sharp-bilinear: nearest inside a texel, linear only across the edge band
	vec2 region = 0.5 - 0.5 / max(u_Scale, vec2(1.0));
	vec2 center = floor(texel - 0.5) + 0.5;
	vec2 f = texel - 0.5 - floor(texel - 0.5);
	f = clamp((f - region) / (1.0 - 2.0 * region), 0.0, 1.0);

	FragColor = vec4(texture(u_FrameTexture, (center + f) / size).rgb, 1.0);
}

#endif
