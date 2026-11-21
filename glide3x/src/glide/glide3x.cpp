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

#include "pch.h"
#include "wrapper.h"
#include "capture.h"
#include "d2/common.h"

using namespace d2gl;

#ifdef __cplusplus
extern "C" {
#endif

FX_ENTRY void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU32 depth)
{
	if (App.glide_capture)
		capture::rec(capture::OP_BUFFER_CLEAR, { color, alpha, depth });
	GlideWrapper->onBufferClear();
}

FX_ENTRY void FX_CALL grBufferSwap(FxU32 swap_interval)
{
	if (App.glide_capture)
		capture::rec(capture::OP_BUFFER_SWAP, { swap_interval });
	GlideWrapper->onBufferSwap();
	if (App.glide_capture)
		capture::onFrame();
}

FX_ENTRY void FX_CALL grDrawPoint(const void* pt)
{
	if (capture::g_on)
		capture::rec(capture::OP_DRAW_POINT, {}, pt, sizeof(GlideVertex));
	GlideWrapper->grDrawPoint(pt);
}

FX_ENTRY void FX_CALL grDrawLine(const void* v1, const void* v2)
{
	if (capture::g_on) {
		GlideVertex v[2] = { *(const GlideVertex*)v1, *(const GlideVertex*)v2 };
		capture::rec(capture::OP_DRAW_LINE, {}, v, sizeof(v));
	}
	GlideWrapper->grDrawLine(v1, v2);
}

FX_ENTRY void FX_CALL grDrawVertexArray(FxU32 mode, FxU32 Count, void* pointers)
{
	if (capture::g_on)
		capture::recDrawArray(mode, Count, (void**)pointers);
	GlideWrapper->grDrawVertexArray(mode, Count, (void**)pointers);
}

FX_ENTRY void FX_CALL grDrawVertexArrayContiguous(FxU32 mode, FxU32 Count, void* pointers, FxU32 stride)
{
	if (capture::g_on) // the wrapper reads a packed GlideVertex array whatever the stride
		capture::rec(capture::OP_DRAW_ARRAY_CONTIG, { mode, Count, stride }, pointers, Count * (uint32_t)sizeof(GlideVertex));
	GlideWrapper->grDrawVertexArrayContiguous(mode, Count, pointers);
}

FX_ENTRY void FX_CALL grAlphaBlendFunction(GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df, GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df)
{
	if (App.glide_capture)
		capture::rec(capture::OP_ALPHA_BLEND, { (uint32_t)rgb_sf, (uint32_t)rgb_df, (uint32_t)alpha_sf, (uint32_t)alpha_df });
	GlideWrapper->grAlphaBlendFunction(rgb_df);
}

FX_ENTRY void FX_CALL grAlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)
{
	if (App.glide_capture)
		capture::rec(capture::OP_ALPHA_COMBINE, { (uint32_t)function, (uint32_t)factor, (uint32_t)local, (uint32_t)other, (uint32_t)invert });
	GlideWrapper->grAlphaCombine(function, local);
}

FX_ENTRY void FX_CALL grChromakeyMode(GrChromakeyMode_t mode)
{
	if (App.glide_capture)
		capture::rec(capture::OP_CHROMAKEY_MODE, { (uint32_t)mode });
	GlideWrapper->grChromakeyMode(mode);
}

FX_ENTRY void FX_CALL grColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)
{
	if (App.glide_capture)
		capture::rec(capture::OP_COLOR_COMBINE, { (uint32_t)function, (uint32_t)factor, (uint32_t)local, (uint32_t)other, (uint32_t)invert });
	GlideWrapper->grColorCombine(function, local);
}

FX_ENTRY void FX_CALL grConstantColorValue(GrColor_t value)
{
	if (App.glide_capture)
		capture::rec(capture::OP_CONSTANT_COLOR, { (uint32_t)value });
	GlideWrapper->grConstantColorValue(value);
}

FX_ENTRY void FX_CALL grLoadGammaTable(FxU32 nentries, FxU32* red, FxU32* green, FxU32* blue)
{
	if (App.glide_capture) {
		const uint32_t n = nentries > 256 ? 256 : nentries;
		uint32_t t[3 * 256];
		memcpy(t, red, n * 4), memcpy(t + n, green, n * 4), memcpy(t + 2 * n, blue, n * 4);
		capture::rec(capture::OP_GAMMA_TABLE, { n }, t, n * 12);
	}
	GlideWrapper->grLoadGammaTable(nentries, red, green, blue);
}

FX_ENTRY void FX_CALL guGammaCorrectionRGB(FxFloat red, FxFloat green, FxFloat blue)
{
	if (App.glide_capture)
		capture::rec(capture::OP_GAMMA_RGB, { capture::f2u(red), capture::f2u(green), capture::f2u(blue) });
	GlideWrapper->guGammaCorrectionRGB(red, green, blue);
}

FX_ENTRY void FX_CALL grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo* info)
{
	if (App.glide_capture)
		capture::rec(capture::OP_TEX_SOURCE, { (uint32_t)tmu, startAddress, evenOdd, (uint32_t)info->smallLodLog2, (uint32_t)info->largeLodLog2, (uint32_t)info->aspectRatioLog2, (uint32_t)info->format });
	GlideWrapper->grTexSource(tmu, startAddress, info);
}

FX_ENTRY void FX_CALL grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo* info)
{
	if (capture::g_on) {
		uint32_t w, h;
		Wrapper::getTexSize(info, w, h);
		capture::rec(capture::OP_TEX_DOWNLOAD, { (uint32_t)tmu, startAddress, evenOdd, (uint32_t)info->smallLodLog2, (uint32_t)info->largeLodLog2, (uint32_t)info->aspectRatioLog2, (uint32_t)info->format }, info->data, w * h);
	}
	GlideWrapper->grTexDownloadMipMap(tmu, startAddress, info);
}

FX_ENTRY void FX_CALL grTexDownloadTable(GrTexTable_t type, void* data)
{
	if (App.glide_capture)
		capture::rec(capture::OP_TEX_TABLE, { (uint32_t)type }, data, 256 * 4);
	GlideWrapper->grTexDownloadTable(data);
}

FX_ENTRY FxBool FX_CALL grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t* info)
{
	const FxBool ok = GlideWrapper->grLfbLock(writeMode, origin, info);
	if (App.glide_capture)
		capture::lfbLocked(ok ? info : nullptr);
	return ok;
}

FX_ENTRY FxBool FX_CALL grLfbUnlock(GrLock_t type, GrBuffer_t buffer)
{
	if (App.glide_capture)
		capture::lfbUnlocked();
	const FxBool ok = GlideWrapper->grLfbUnlock(); // presents the movie frame
	if (App.glide_capture)
		capture::onFrame();
	return ok;
}

FX_ENTRY GrContext_t FX_CALL grSstWinOpen(FxU32 hWnd, GrScreenResolution_t screen_resolution, GrScreenRefresh_t refresh_rate, GrColorFormat_t color_format, GrOriginLocation_t origin_location, int nColBuffers, int nAuxBuffers)
{
	if (App.glide_capture)
		capture::rec(capture::OP_SST_WIN_OPEN, { (uint32_t)screen_resolution, d2::is_in_game ? *d2::is_in_game : 0 });
	return Wrapper::grSstWinOpen(hWnd, screen_resolution);
}

FX_ENTRY FxU32 FX_CALL grGet(FxU32 pname, FxU32 plength, FxI32* params)
{
	return Wrapper::grGet(pname, *params);
}

FX_ENTRY FxU32 FX_CALL grTexMaxAddress(GrChipID_t tmu)
{
	return GLIDE_TEX_MEMORY - 256 * 256;
}

FX_ENTRY FxU32 FX_CALL grTexMinAddress(GrChipID_t tmu)
{
	return 0;
}

FX_ENTRY const char* FX_CALL grGetString(FxU32 pname)
{
	return Wrapper::grGetString(pname);
}

// Called but unnecessary functions

FX_ENTRY FxBool FX_CALL grSstWinClose(GrContext_t context)
{
	return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grSelectContext(GrContext_t context)
{
	return FXTRUE;
}

FX_ENTRY void FX_CALL grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode)
{
}

FX_ENTRY void FX_CALL grRenderBuffer(GrBuffer_t buffer)
{
}

FX_ENTRY void FX_CALL grErrorSetCallback(GrErrorCallbackFnc_t fnc)
{
}

FX_ENTRY void FX_CALL grFinish()
{
}

FX_ENTRY void FX_CALL grFlush()
{
}

FX_ENTRY void FX_CALL grSstSelect(int which_sst)
{
}

FX_ENTRY void FX_CALL grChromakeyValue(GrColor_t value)
{
}

FX_ENTRY void FX_CALL grColorMask(FxBool rgb, FxBool a)
{
}

FX_ENTRY void FX_CALL grDepthMask(FxBool mask)
{
}

FX_ENTRY void FX_CALL grDitherMode(GrDitherMode_t mode)
{
}

FX_ENTRY void FX_CALL grCoordinateSpace(GrCoordinateSpaceMode_t mode)
{
}

FX_ENTRY void FX_CALL grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height)
{
}

FX_ENTRY void FX_CALL grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode)
{
}

FX_ENTRY void FX_CALL grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgb_function, GrCombineFactor_t rgb_factor, GrCombineFunction_t alpha_function, GrCombineFactor_t alpha_factor, FxBool rgb_invert, FxBool alpha_invert)
{
}

FX_ENTRY void FX_CALL grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode, GrTextureFilterMode_t magfilter_mode)
{
}

FX_ENTRY void FX_CALL grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend)
{
}

FX_ENTRY void FX_CALL grGlideInit(void)
{
}

FX_ENTRY void FX_CALL grGlideShutdown(void)
{
}

// Unused functions

FX_ENTRY void FX_CALL grEnable(GrEnableMode_t mode)
{
	assert(false);
}

FX_ENTRY void FX_CALL grDisable(GrEnableMode_t mode)
{
	assert(false);
}

FX_ENTRY void FX_CALL grDrawTriangle(const void* a, const void* b, const void* c)
{
	assert(false);
}

FX_ENTRY void FX_CALL grAADrawTriangle(const void* a, const void* b, const void* c, FxBool ab_antialias, FxBool bc_antialias, FxBool ca_antialias)
{
	assert(false);
}

FX_ENTRY void FX_CALL grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy)
{
	assert(false);
}

FX_ENTRY void FX_CALL grDepthBufferFunction(GrCmpFnc_t function)
{
	assert(false);
}

FX_ENTRY void FX_CALL grDepthBufferMode(GrDepthBufferMode_t mode)
{
	assert(false);
}

FX_ENTRY GrProc FX_CALL grGetProcAddress(char* procName)
{
	assert(false);
	return NULL;
}

FX_ENTRY FxU32 FX_CALL grTexTextureMemRequired(FxU32 evenOdd, GrTexInfo* info)
{
	assert(false);
	return 0;
}

#ifdef __cplusplus
}
#endif
// D2GL build stamp for MedianRevived ("<hash>[-dirty] built <date>", hash "unknown" for local builds); exported undecorated
#include "build_stamp.h"
extern "C" const char* __stdcall d2glVersion(void)
{
	return D2GL_VERSION_STRING;
}
#pragma comment(linker, "/EXPORT:d2glVersion=_d2glVersion@0")
