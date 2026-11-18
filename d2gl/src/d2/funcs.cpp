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
#include "funcs.h"
#include "common.h"
#include "helpers.h"
#include "modules/hd_text.h"
#include "modules/motion_prediction.h"
#include "modules/loot_sparkle.h"
#include "stubs.h"
#include "graphic/hires_pack.h"

#include <cmath>

namespace d2gl::d2 {

uint32_t mpqLoad(const char* mpqfile)
{
	if (isVer(V_114d))
		return ((mpqLoad_114_t)mpqLoad_Fn)(mpqfile, 0, 0, 0);
	else if (isVerMin(V_111))
		return mpqLoadStub("d2gl.dll", mpqfile, "d2gl", 0, 0);

	return ((mpqLoad_110_t)mpqLoad_Fn)("d2gl.dll", mpqfile, 0, 0, 0, 0, 0);
}

glm::ivec2 getCursorPos()
{
	if (App.game.screen == GameScreen::InGame)
		return { *mouse_x, *mouse_y };

	return { *mouse_x_menu, *mouse_y_menu };
}

bool isPerspective()
{
	const bool available = *(bool*)(perspective + 4);
	return *perspective && available;
}

bool isEscMenuOpen()
{
	const bool control = *(bool*)(esc_menu_open + 8);
	return *esc_menu_open || control;
}

bool isLangCJK(uint32_t lang_id)
{
	return lang_id == LANG_JPN || lang_id == LANG_KOR || lang_id == LANG_CHI;
}

UnitAny* getPlayerUnit()
{
	return (UnitAny*)*(uintptr_t*)player_unit;
}

UnitAny* findUnit(uint32_t type_id)
{
	const uint32_t unit_id = type_id & 0x00FFFFFF;
	const uint32_t unit_type = type_id >> 24;

	auto unit = findUnitClient(unit_id, unit_type);
	if (unit)
		return unit;

	return findUnitServer(unit_id, unit_type);
}

DWORD getUnitID(UnitAny* unit)
{
	return isVer(V_109d) ? unit->v109.dwUnitId : unit->v110.dwUnitId;
}

DWORD getUnitFlag(UnitAny* unit)
{
	return isVer(V_109d) ? unit->v109.dwFlags : unit->v110.dwFlags;
}

Path* getUnitPath(UnitAny* unit)
{
	return isVer(V_109d) ? unit->v109.pPath : unit->v110.pPath;
}

StaticPath* getUnitStaticPath(UnitAny* unit)
{
	return isVer(V_109d) ? unit->v109.pStaticPath : unit->v110.pStaticPath;
}

uint32_t getUnitStat(UnitAny* unit, uint32_t stat)
{
	return isVer(V_109d) ? ((getUnitStat109d_t)getUnitStat_Fn)(unit, stat) : getUnitStat_Fn(unit, stat, 0);
}

UnitAny* getSelectedItem()
{
	return (UnitAny*)*(uintptr_t*)selected_item;
}

bool isUnitDead(UnitAny* unit)
{
	return unit && (d2::getUnitFlag(unit) & 0x10000);
}

char* getPlayerName(UnitAny* unit)
{
	return (char*)(isVer(V_109d) ? unit->v109.pPlayerData->szName : unit->v110.pPlayerData->szName);
}

MonsterType getMonsterType(UnitAny* unit)
{
	if (isVer(V_109d)) {
		if (unit->v109.pMonsterData->fChamp)
			return MonsterType::Champion;
		else if (unit->v109.pMonsterData->fBoss)
			return MonsterType::Boss;
	} else {
		if (unit->v110.pMonsterData->fSuperUniq)
			return MonsterType::SuperUnique;
		else if (unit->v110.pMonsterData->fChamp || unit->v110.pMonsterData->fPossesed || unit->v110.pMonsterData->fGhostly)
			return MonsterType::Champion;
		else if (unit->v110.pMonsterData->fBoss)
			return MonsterType::Boss;
	}

	return MonsterType::Normal;
}

wchar_t* getMonsterName(UnitAny* unit)
{
	return isVer(V_109d) ? unit->v109.pMonsterData->wName : unit->v110.pMonsterData->wName;
}

bool isMercUnit(UnitAny* unit)
{
	if (unit->dwType != d2::UnitType::Monster || isVer(V_109d))
		return false;

	return unit->v110.dwClassId == MERC_A1 || unit->v110.dwClassId == MERC_A2 || unit->v110.dwClassId == MERC_A3 || unit->v110.dwClassId == MERC_A4 || unit->v110.dwClassId == MERC_A5;
}

ItemQuality getItemQuality(UnitAny* unit)
{
	return isVer(V_109d) ? unit->v109.pItemData->dwQuality : unit->v110.pItemData->dwQuality;
}

BYTE getItemLocation(UnitAny* unit)
{
	return isVer(V_109d) ? unit->v109.pItemData->ItemLocation : unit->v110.pItemData->ItemLocation;
}

CellFile* getCellFile(CellContext* cell)
{
	if (isVerMin(V_113c))
		return cell->v113.pCellFile;
	else if (isVer(V_112))
		return cell->v112.pCellFile;
	else if (isVerMin(V_111))
		return cell->v111.pCellFile;
	return cell->v109.pCellFile;
}

DWORD getCellNo(CellContext* cell)
{
	if (isVerMin(V_113c))
		return cell->v113.nCellNo;
	else if (isVer(V_112))
		return cell->v112.nCellNo;
	else if (isVerMin(V_111))
		return cell->v111.nCellNo;
	return cell->v109.nCellNo;
}

void gameDrawBegin()
{
	if (App.game.screen == GameScreen::Loading || App.game.screen == GameScreen::Menu)
		App.game.screen = GameScreen::InGame;
}

void automapDrawBegin()
{
	App.context->drawSilhouettes(); // [Graphic] silhouettes: end of the world draws (also at uiDrawBegin)
	App.game.draw_stage = DrawStage::Map;
	App.context->onStageChange();
}

void automapDrawEnd()
{
	App.game.draw_stage = DrawStage::HUD;
	App.context->onStageChange();
}

void uiDrawBegin()
{
	modules::LootSparkle::Instance().draw();
	App.context->drawSilhouettes();
	App.game.draw_stage = DrawStage::UI;
	App.context->onStageChange();
}

void uiDrawCursorItem()
{
	App.game.draw_stage = DrawStage::CursorItem;
	App.context->onStageChange();
}

void uiDrawEnd()
{
	App.game.draw_stage = DrawStage::Cursor;
	App.context->onStageChange();
}

// [Graphic] silhouettes: stencil class of a unit sprite (1 the player, 2 objects such as trees and doors)
static uint8_t silhouetteClass()
{
	const UnitAny* unit = currently_drawing_unit;
	if (!App.silhouettes || !unit)
		return 0;
	if (unit == getPlayerUnit())
		return 1;
	return unit->dwType == UnitType::Object ? 2 : 0;
}

static bool drawLiquidPortal(CellContext* cell, int x, int y, uint32_t gamma, int draw_mode, uint8_t* palette);

void __stdcall drawImageHooked(CellContext* cell, int x, int y, uint32_t gamma, int draw_mode, uint8_t* palette)
{
	App.context->setStencilClass(silhouetteClass());
	if (App.hd_cursor && App.game.draw_stage >= DrawStage::Cursor)
		return;

	if (modules::HDText::Instance().drawImage(cell, x, y, draw_mode)) {
		const auto pos = modules::MotionPrediction::Instance().drawImage(x, y, D2DrawFn::Image, gamma, draw_mode);
		// [Graphic] liquid_portals: town portal objects become a procedural vortex
		if (App.liquid_portals && App.game.draw_stage == DrawStage::World && drawLiquidPortal(cell, pos.x, pos.y, gamma, draw_mode, palette))
			return;
		// [Graphic] liquid_potions: potion items drawn by DrawItemGfx get animated liquid in the shader (pushVertex tag)
		drawing_potion = App.liquid_potions && drawing_ui_item && ISGLIDE3X() && isPotionItem(drawing_ui_item);
		drawImage(cell, pos.x, pos.y, gamma, draw_mode, palette);
		drawing_potion = false;
		modules::LootSparkle::Instance().record(cell, pos.x, pos.y);
	}

	modules::HDText::drawItemQuantity(true);
}

void __stdcall drawPerspectiveImageHooked(CellContext* cell, int x, int y, uint32_t gamma, int draw_mode, int screen_mode, uint8_t* palette)
{
	App.context->setStencilClass(0);
	const auto pos = modules::MotionPrediction::Instance().drawImage(x, y, D2DrawFn::PerspectiveImage);
	drawPerspectiveImage(cell, pos.x, pos.y, gamma, draw_mode, screen_mode, palette);
}

void __stdcall drawShiftedImageHooked(CellContext* cell, int x, int y, uint32_t gamma, int draw_mode, int global_palette_shift)
{
	App.context->setStencilClass(0);
	if (modules::HDText::Instance().drawShiftedImage(cell, x, y)) {
		auto pos = modules::MotionPrediction::Instance().drawImage(x, y, D2DrawFn::ShiftedImage);
		drawShiftedImage(cell, pos.x, pos.y, gamma, draw_mode, global_palette_shift);
	}
}

// [Graphic] orb_style 1: the liquid cell as the game just drew it (pCurGfxCell is set by the draw call).
// DC6 lines: 0x80 ends a line, 0x80 | n skips n transparent pixels, n < 0x80 is followed by n palette indices.
struct OrbCell {
	int xoffs, yoffs, height;
	int x0, y0, x1, y1; // opaque box in cell pixels, top-down, end exclusive
	uint32_t color;     // 0xFFRRGGBB average of the opaque pixels in the current palette
};

static bool decodeOrbCell(CellContext* cell, OrbCell* out)
{
	__try {
		const GfxCell* gfx = (isVerMin(V_113c)) ? cell->v113.pCurGfxCell : ((isVer(V_112)) ? cell->v112.pCurGfxCell : ((isVerMin(V_111)) ? cell->v111.pCurGfxCell : cell->v109.pCurGfxCell));
		if (!gfx || gfx->width == 0 || gfx->width > 256 || gfx->height == 0 || gfx->height > 256 || gfx->length == 0 || gfx->length > 0x10000)
			return false;
		const BYTE* data = &gfx->cols;
		const DWORD len = gfx->length;
		const int w = (int)gfx->width, h = (int)gfx->height;
		int x = 0, row = 0, x0 = w, x1 = 0, r0 = h, r1 = 0;
		uint32_t n = 0, sr = 0, sg = 0, sb = 0; // <= 65536 pixels * 255 fits
		for (DWORD i = 0; i < len && row < h; i++) {
			const BYTE b = data[i];
			if (b == 0x80) {
				row++;
				x = 0;
			} else if (b & 0x80)
				x += b & 0x7F;
			else if (b) {
				if (i + b >= len || x + b > w)
					return false;
				for (DWORD k = 1; k <= b; k++) {
					const uint32_t c = hires::g_pal[data[i + k]];
					sr += (c >> 16) & 0xFF;
					sg += (c >> 8) & 0xFF;
					sb += c & 0xFF;
				}
				n += b;
				x0 = std::min(x0, x);
				x1 = std::max(x1, x + b);
				r0 = std::min(r0, row);
				r1 = std::max(r1, row + 1);
				x += b;
				i += b;
			}
		}
		if (n < 16 || x1 - x0 < 8 || r1 - r0 < 8)
			return false;
		out->xoffs = (int)gfx->xoffs;
		out->yoffs = (int)gfx->yoffs;
		out->height = h;
		out->x0 = x0;
		out->x1 = x1;
		out->y0 = gfx->flags ? r0 : h - r1; // flip 0: lines are stored bottom-up
		out->y1 = gfx->flags ? r1 : h - r0;
		out->color = 0xFF000000 | ((sr / n) << 16) | ((sg / n) << 8) | (sb / n);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// Liquid box at the cell's draw position (vertical extent y + yoffs - height .. y + yoffs, as D2Gfx clips it),
// fill = the drawn bottom lines over the box, slosh = decaying kick from fill changes (per orb, ~0.6 s to 10 %).
static bool pushProceduralOrb(CellContext* cell, int x, int y, int skip_lines, int draw_lines)
{
	OrbCell oc;
	if (!decodeOrbCell(cell, &oc))
		return false;

	const float left = (float)(x + oc.xoffs), bottom = (float)(y + oc.yoffs), top = bottom - (float)oc.height;
	const glm::vec4 rect = { left + (float)oc.x0, top + (float)oc.y0, left + (float)oc.x1, top + (float)oc.y1 };
	const float surface = bottom - (float)(skip_lines + draw_lines);
	const float fill = std::clamp((rect.w - surface) / (rect.w - rect.y), 0.0f, 1.0f);

	static struct { float fill; float slosh; ULONGLONG tick; } orbs[2] = {};
	const int id = x < (int)*d2::screen_width / 2 ? 0 : 1;
	auto& s = orbs[id];
	const ULONGLONG now = GetTickCount64();
	if (s.tick && now - s.tick < 1000) {
		s.slosh *= std::exp(-(float)(now - s.tick) * 0.001f / 0.26f);
		s.slosh = std::min(1.0f, s.slosh + std::fabs(fill - s.fill) * 6.0f);
	} else
		s.slosh = 0.0f;
	s.fill = fill;
	s.tick = now;

	const uint32_t f8 = (uint32_t)(fill * 255.0f + 0.5f);
	const uint32_t s8 = (uint32_t)(s.slosh * 255.0f + 0.5f);
	const uint32_t m8 = (uint32_t)(std::clamp(App.orb_motion.value, 0.0f, 2.0f) * 127.5f + 0.5f);
	return App.context->pushOrbQuad(rect, oc.color, (f8 << 24) | (s8 << 16) | (m8 << 8) | (id ? 0xFFu : 0u));
}

// [Graphic] liquid_bars: one procedural quad over the bar's full track (flag y 4, x 1); fill from the left, pulse =
// decaying kick when the value rises (per bar slot: 0 experience, 1 stamina, 2.. plugin bars through d2gl_liquid_bar).
static bool pushLiquidBar(float x, float y, float w, float h, float fill, uint32_t rgb, uint32_t slot)
{
	if (!(fill >= 0.0f)) // NaN
		fill = 0.0f;
	fill = std::min(fill, 1.0f);
	static struct { float fill; float pulse; ULONGLONG tick; } bars[64] = {};
	auto& s = bars[slot & 63];
	const ULONGLONG now = GetTickCount64();
	if (s.tick && now - s.tick < 1000) {
		s.pulse *= std::exp(-(float)(now - s.tick) * 0.001f / 0.45f);
		if (fill > s.fill + 0.0005f)
			s.pulse = std::min(1.0f, s.pulse + 0.3f + (fill - s.fill) * 6.0f);
	} else
		s.pulse = 0.0f;
	s.fill = fill;
	s.tick = now;

	const uint32_t f8 = (uint32_t)(fill * 255.0f + 0.5f);
	const uint32_t p8 = (uint32_t)(s.pulse * 255.0f + 0.5f);
	const uint32_t m8 = (uint32_t)(std::clamp(App.orb_motion.value, 0.0f, 2.0f) * 127.5f + 0.5f);
	return App.context->pushOrbQuad({ x, y, x + w, y + h }, 0xFF000000 | (rgb & 0xFFFFFF), (f8 << 24) | (p8 << 16) | (m8 << 8) | ((slot * 37u) & 0xFFu), 1);
}

// d2gl_liquid_bar export (glide3x.cpp): plugin bars in game coordinates, during UI drawing on the game thread
int liquidBarExternal(int x, int y, int w, int h, float fill, uint32_t rgb, int id)
{
	if (!App.context || !ISGLIDE3X() || App.game.screen != GameScreen::InGame || w <= 0 || h <= 0 || w > 4096 || h > 4096)
		return 0;
	return pushLiquidBar((float)x, (float)y, (float)w, (float)h, fill, rgb, 2u + (uint32_t)id % 62u) ? 1 : 0;
}

// [Graphic] liquid_portals: town portal (objects.txt 59) and permanent red portal (60). The game's draw runs with its
// vertices dropped (it loads the cell), our vortex quad replaces it over the frame's opaque box in the sprite's average
// colour; further layers of the same unit in the same frame are dropped. The game's sprite is drawn if the cell cannot be read.
static bool drawLiquidPortal(CellContext* cell, int x, int y, uint32_t gamma, int draw_mode, uint8_t* palette)
{
	UnitAny* unit = currently_drawing_unit;
	if (!unit || unit->dwType != UnitType::Object || (isVer(V_109d)) || !ISGLIDE3X())
		return false;
	const DWORD cls = unit->v110.dwClassId;
	if (cls != 59 && cls != 60)
		return false;

	static UnitAny* last_unit = nullptr;
	static uint32_t last_frame = 0;
	const uint32_t frame = App.context->getFrameCount();
	const bool again = last_unit == unit && last_frame == frame;
	drawing_orb_hd = true;
	drawImage(cell, x, y, gamma, draw_mode, palette);
	drawing_orb_hd = false;
	if (again)
		return true;

	OrbCell oc;
	if (!decodeOrbCell(cell, &oc))
		return false;
	const float left = (float)(x + oc.xoffs), top = (float)(y + oc.yoffs) - (float)oc.height;
	const glm::vec4 rect = { left + (float)oc.x0, top + (float)oc.y0, left + (float)oc.x1, top + (float)oc.y1 };
	const uint32_t m8 = (uint32_t)(std::clamp(App.orb_motion.value, 0.0f, 2.0f) * 127.5f + 0.5f);
	const uint32_t seed = (getUnitID(unit) * 73u) & 0xFFu;
	if (!App.context->pushOrbQuad(rect, oc.color, (seed << 24) | (m8 << 8) | (cls == 60 ? 1u : 0u), 2))
		return false;
	last_unit = unit;
	last_frame = frame;
	return true;
}

void __stdcall drawVerticalCropImageHooked(CellContext* cell, int x, int y, int skip_lines, int draw_lines, int draw_mode)
{
	App.context->setStencilClass(0);
	if (modules::HDText::Instance().isActive() && App.game.draw_stage >= DrawStage::UI) {
		if (y < 150 || (*d2::screen_shift >= SCREENPANEL_LEFT && y < (int)*d2::screen_height - 100 && x < (int)*d2::screen_width / 2))
			return;
	}

	const auto pos = modules::MotionPrediction::Instance().drawImage(x, y, D2DrawFn::VerticalCropImage);
	// [HDR] orb_glow: the orb liquid is a solid vertical crop at (0x1C/0x1D | width - 0x6F, height - 13)
	// (D2Client PANEL_DrawHealthOrb 0x496F80 / PANEL_DrawManaOrb 0x497110, per Redivivus)
	const bool orb_liquid = draw_mode == 5 && y == (int)*d2::screen_height - 13 &&
		(x == 0x1C || x == 0x1D || x == (int)*d2::screen_width - 0x6F);
	const bool orb = orb_liquid && App.hdr.orb_glow.value > 0.0f;
	d2::drawing_orb = orb;
	d2::drawing_orb_liquid = orb_liquid;
	// [Graphic] orb_style 1: the game's draw runs with its vertices dropped (it loads the cell), then our quad takes
	// its place in the command stream; the classic liquid is drawn if the cell cannot be read
	const bool hd = orb_liquid && App.orb_style.selected >= 1 && ISGLIDE3X();
	d2::drawing_orb_hd = hd;
	drawVerticalCropImage(cell, pos.x, pos.y, skip_lines, draw_lines, draw_mode);
	d2::drawing_orb_hd = false;
	if (hd && !pushProceduralOrb(cell, pos.x, pos.y, skip_lines, draw_lines))
		drawVerticalCropImage(cell, pos.x, pos.y, skip_lines, draw_lines, draw_mode);
	d2::drawing_orb = false;
	d2::drawing_orb_liquid = false;
}

void __stdcall drawClippedImageHooked(CellContext* cell, int x, int y, void* crop_rect, int draw_mode)
{
	App.context->setStencilClass(0);
	const auto pos = modules::MotionPrediction::Instance().drawImage(x, y, D2DrawFn::ClippedImage);
	drawClippedImage(cell, pos.x, pos.y, crop_rect, draw_mode);
}

void __stdcall drawImageFastHooked(CellContext* cell, int x, int y, uint8_t palette_index)
{
	App.context->setStencilClass(0);
	const auto pos = modules::MotionPrediction::Instance().drawImage(x, y, D2DrawFn::ImageFast);
	drawImageFast(cell, pos.x, pos.y, palette_index);
}

void __stdcall drawShadowHooked(CellContext* cell, int x, int y)
{
	App.context->setStencilClass(0);
	const auto pos = modules::MotionPrediction::Instance().drawImage(x, y, D2DrawFn::Shadow);
	if (!App.context->isShadowFxActive()) {
		drawShadow(cell, pos.x, pos.y);
		return;
	}

	App.context->setShadowDraw(1);
	drawShadow(cell, pos.x, pos.y);
	App.context->setShadowDraw(0);
}

void __stdcall drawSolidRectExHooked(int left, int top, int right, int bottom, uint32_t color, int draw_mode)
{
	App.context->setStencilClass(0);
	// [Graphic] liquid_bars: stamina bar, D2Client PANEL_DrawStaminaBar (1.14d 0x4975B0, per Redivivus):
	// DrawSolidRectExBySize(width / 2 - 0x7F, height - 0x1B, stamina * 0x66 / max, 0x12, palette index, mode 2)
	if (App.liquid_bars && (App.game.draw_stage == DrawStage::UI || App.game.draw_stage == DrawStage::HUD) && draw_mode == 2 &&
		left == (int)*d2::screen_width / 2 - 0x7F && top == (int)*d2::screen_height - 0x1B && bottom == top + 0x12 && right >= left && right - left <= 0x66 &&
		pushLiquidBar((float)left, (float)top, (float)0x66, (float)0x12, (float)(right - left) / (float)0x66, hires::g_pal[color & 0xFF], 1))
		return;
	auto offset = modules::MotionPrediction::Instance().drawSolidRect();
	if (!modules::HDText::Instance().drawSolidRect(left - offset.x, top - offset.y, right - offset.x, bottom - offset.y, color, draw_mode))
		drawSolidRectEx(left - offset.x, top - offset.y, right - offset.x, bottom - offset.y, color, draw_mode);

	modules::HDText::drawItemQuantity(false, left, bottom);
}

void __stdcall drawLineHooked(int x_start, int y_start, int x_end, int y_end, uint8_t color, uint8_t alpha)
{
	App.context->setStencilClass(0);
	// [Graphic] liquid_bars: experience bar, D2Client PANEL_DrawGoldAmount (1.14d 0x498EA0, per Redivivus): two lines
	// from width / 2 - 0x90 at height - 0x26 and - 0x25, exp into level * 0x77 / level span long, colour 0xFF, alpha 0xFF
	if (App.liquid_bars && (App.game.draw_stage == DrawStage::UI || App.game.draw_stage == DrawStage::HUD) && color == 0xFF && alpha == 0xFF &&
		y_end == y_start && x_start == (int)*d2::screen_width / 2 - 0x90 && x_end > x_start && x_end - x_start <= 0x77) {
		static bool exp_liquid = false;
		const int ys = (int)*d2::screen_height - 0x26;
		if (y_start == ys) {
			uint32_t c = hires::g_pal[0xFF] & 0xFFFFFF;
			const int cr = (c >> 16) & 0xFF, cg = (c >> 8) & 0xFF, cb = c & 0xFF;
			if (std::max(cr, std::max(cg, cb)) - std::min(cr, std::min(cg, cb)) < 48)
				c = 0xD8A838; // the game's line is white: amber liquid
			exp_liquid = pushLiquidBar((float)x_start, (float)ys, (float)0x77, 2.0f, (float)(x_end - x_start) / (float)0x77, c, 0);
			if (exp_liquid)
				return;
		} else if (y_start == ys + 1 && exp_liquid) {
			exp_liquid = false;
			return;
		}
	}
	const auto offset = modules::MotionPrediction::Instance().drawLine(x_start, y_start);
	drawLine(x_start - offset.x, y_start - offset.y, x_end - offset.x, y_end - offset.y, color, alpha);
}

bool __stdcall drawGroundTileHooked(TileContext* tile, GFXLight* light, int x, int y, int world_x, int world_y, uint8_t alpha, int screen_panels, bool tile_data)
{
	App.context->setStencilClass(0);
	// Drawing invisible tile crashes on glide mode.
	if (ISGLIDE3X() && tile) {
		const auto len = strlen(tile->szTileName);
		if (len > 8) {
			const auto name = tile->szTileName + len - 8;
			if (_stricmp(name, "Warp.dt1") == 0)
				return true;
		}
	}

	const auto offset = modules::MotionPrediction::Instance().getGlobalOffset();
	App.context->setShadowDraw(2);
	const bool ret = drawGroundTile(tile, light, x - offset.x, y - offset.y, world_x, world_y, alpha, screen_panels, tile_data);
	App.context->setShadowDraw(0);
	return ret;
}

bool __stdcall drawWallTileHooked(TileContext* tile, int x, int y, GFXLight* light, int screen_panels)
{
	App.context->setStencilClass(2);
	const auto offset = modules::MotionPrediction::Instance().getGlobalOffset(true);
	return drawWallTile(tile, x - offset.x, y - offset.y, light, screen_panels);
}

bool __stdcall drawTransWallTileHooked(TileContext* tile, int x, int y, GFXLight* light, int screen_panels, uint8_t alpha)
{
	App.context->setStencilClass(0);
	const auto offset = modules::MotionPrediction::Instance().getGlobalOffset(true);
	return drawTransWallTile(tile, x - offset.x, y - offset.y, light, screen_panels, alpha);
}

bool __stdcall drawShadowTileHooked(TileContext* tile, int x, int y, int draw_mode, int screen_panels)
{
	App.context->setStencilClass(0);
	const auto offset = modules::MotionPrediction::Instance().getGlobalOffset(true);
	App.context->setShadowDraw(2);
	const bool ret = drawShadowTile(tile, x - offset.x, y - offset.y, draw_mode, screen_panels);
	App.context->setShadowDraw(0);
	return ret;
}

void __fastcall takeScreenShotHooked()
{
	App.context->getCommandBuffer()->pushCommand(CommandType::TakeScreenShot);
}

void __fastcall drawNormalTextHooked(const wchar_t* str, int x, int y, uint32_t color, uint32_t centered)
{
	// Glide mode light gray text appears black. So direct to dark gray.
	if (ISGLIDE3X() && !App.hd_text.active && color == 15)
		color = 5;

	const auto pos = modules::MotionPrediction::Instance().drawText(str, x, y, D2DrawFn::NormalText);
	if (!modules::HDText::Instance().drawText(str, pos.x, pos.y, color, centered))
		drawNormalText(str, pos.x, pos.y, color, centered);
}

void __fastcall drawNormalTextExHooked(const wchar_t* str, int x, int y, uint32_t color, uint32_t centered, uint32_t trans_lvl)
{
	const auto pos = modules::MotionPrediction::Instance().drawText(str, x, y, D2DrawFn::NormalTextEx);
	if (!modules::HDText::Instance().drawText(str, pos.x, pos.y, color, centered, trans_lvl))
		drawNormalTextEx(str, pos.x, pos.y, color, centered, trans_lvl);
}

void __fastcall drawFramedTextHooked(const wchar_t* str, int x, int y, uint32_t color, uint32_t centered)
{
	const auto pos = modules::MotionPrediction::Instance().drawText(str, x, y, D2DrawFn::FramedText);
	if (!modules::HDText::Instance().drawFramedText(str, pos.x, pos.y, color, centered))
		drawFramedText(str, pos.x, pos.y, color, centered);
}

void __fastcall drawRectangledTextHooked(const wchar_t* str, int x, int y, uint32_t rect_color, uint32_t rect_transparency, uint32_t color)
{
	const auto pos = modules::MotionPrediction::Instance().drawText(str, x, y, D2DrawFn::RectangledText);
	if (!modules::HDText::Instance().drawRectangledText(str, pos.x, pos.y, rect_transparency, color))
		drawRectangledText(str, pos.x, pos.y, rect_color, rect_transparency, color);
}

uint32_t __fastcall getNormalTextWidthHooked(const wchar_t* str)
{
	if (modules::HDText::Instance().isActive())
		return modules::HDText::Instance().getNormalTextWidth(str, 0);
	return getNormalTextWidth(str);
}

uint32_t __fastcall getNormalTextNWidthHooked(const wchar_t* str, const int n_chars)
{
	if (modules::HDText::Instance().isActive())
		return modules::HDText::Instance().getNormalTextWidth(str, n_chars);
	return getNormalTextNWidth(str, n_chars);
}

uint32_t __fastcall getFramedTextSizeHooked(const wchar_t* str, uint32_t* width, uint32_t* height)
{
	if (modules::HDText::Instance().isActive())
		return modules::HDText::Instance().getFramedTextSize(str, width, height);
	return getFramedTextSize(str, width, height);
}

uint16_t __fastcall getFontHeightHooked()
{
	if (modules::HDText::Instance().isActive())
		return modules::HDText::Instance().getFontHeight();
	return getFontHeight();
}

uint32_t __fastcall setTextSizeHooked(uint32_t size)
{
	modules::HDText::Instance().setTextSize(size);
	size = (size > 13) ? 11 : size;
	return setTextSize(size);
}

void rectangledTextBegin()
{
	modules::MotionPrediction::Instance().textMotion(D2DrawFn::NormalText);
	modules::HDText::Instance().borderedRect(true);
}

void rectangledTextEnd()
{
	modules::MotionPrediction::Instance().textMotion(D2DrawFn::None);
	modules::HDText::Instance().borderedRect(false);
}

void unitHoverText()
{
	modules::MotionPrediction::Instance().unitHoverTextMotion();
}

void altItemsText()
{
	modules::MotionPrediction::Instance().altItemsTextMotion();
}

void drawRectFrame()
{
	modules::HDText::Instance().drawRectFrame();
}

void drawUnitHealthBar()
{
	modules::HDText::Instance().drawUnitHealthBar();
}

void loadUIImage()
{
	modules::HDText::Instance().loadUIImage();
}

void drawSubTextA()
{
	modules::HDText::Instance().drawSubText(1);
}

void drawSubTextB()
{
	modules::HDText::Instance().drawSubText(2);
}

void drawSubTextC()
{
	modules::HDText::Instance().drawSubText(3);
}

void levelEntryText()
{
	modules::HDText::Instance().startEntryText();
}

}