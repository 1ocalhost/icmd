#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <conio.h>

#include <map>
#include <queue>

#include "../app.rc"
#include "base.hpp"

#define ICON_MASK_COLOR RGB(255, 0, 255)
#define ICON_MAX_LIGHTNESS 0.5

inline Gdiplus::Bitmap *load_image_from_resource(WORD id, PCWSTR type)
{
	HMODULE module = GetModuleHandle(NULL);
	HRSRC res = FindResource(module, MAKEINTRESOURCE(id), type);
	if (!res)
		return NULL;

	DWORD res_size = SizeofResource(module, res);
	if (!res_size)
		return NULL;

	HGLOBAL global_res = LoadResource(module, res);
	if (!global_res)
		return NULL;

	PVOID data_res = LockResource(global_res);
	if (!data_res)
		return NULL;

	HGLOBAL global_mem = GlobalAlloc(GHND, res_size);
	if (!global_mem)
		return NULL;

	DEFER([=] { GlobalFree(global_mem); });
	PVOID data = GlobalLock(global_mem);
	if (!data)
		return NULL;

	memcpy(data, data_res, res_size);

	IStream *stream = NULL;
	HRESULT result = CreateStreamOnHGlobal(
		global_mem, TRUE, &stream);
	if (FAILED(result))
		return NULL;

	DEFER([=] { stream->Release(); });
	return new Gdiplus::Bitmap(stream);
}

inline void get_image_gray_range(
	Gdiplus::Bitmap *src,
	Gdiplus::Bitmap *mask,
	BYTE *gray_min_,
	BYTE *gray_max_)
{
	BYTE gray_min = 255;
	BYTE gray_max = 0;
	UINT width = src->GetWidth();
	UINT heigt = src->GetHeight();
	Gdiplus::Color pixel;

	for (UINT x = 0; x < width; ++x) {
		for (UINT y = 0; y < heigt; ++y) {
			mask->GetPixel(x, y, &pixel);
			if (pixel.ToCOLORREF() != ICON_MASK_COLOR)
				continue;

			src->GetPixel(x, y, &pixel);
			BYTE red = pixel.GetR();
			if (red > gray_max)
				gray_max = red;
			if (red < gray_min)
				gray_min = red;
		}
	}

	*gray_min_ = gray_min;
	*gray_max_ = gray_max;
}

inline void render_colored_image(
	Gdiplus::Bitmap *src,
	Gdiplus::Bitmap *mask,
	Gdiplus::Bitmap *out,
	BYTE gray_min,
	BYTE gray_max,
	COLORREF color)
{
	float color_r = GetRValue(color);
	float color_g = GetGValue(color);
	float color_b = GetBValue(color);
	float complement_r = (float)255 - color_r;
	float complement_g = (float)255 - color_g;
	float complement_b = (float)255 - color_b;

	UINT width = src->GetWidth();
	UINT heigt = src->GetHeight();
	Gdiplus::Color pixel;

	for (UINT x = 0; x < width; ++x) {
		for (UINT y = 0; y < heigt; ++y) {
			mask->GetPixel(x, y, &pixel);
			if (pixel.ToCOLORREF() != ICON_MASK_COLOR) {
				out->SetPixel(x, y, pixel);
				continue;
			}

			src->GetPixel(x, y, &pixel);
			BYTE red = pixel.GetR();

			float shade = (red - gray_min) / (float)gray_max;
			shade *= ICON_MAX_LIGHTNESS;
			out->SetPixel(x, y, Gdiplus::Color(
				255,
				BYTE(color_r + (complement_r * shade)),
				BYTE(color_g + (complement_g * shade)),
				BYTE(color_b + (complement_b * shade))
			));
		}
	}
}

class ColoredIcon {
public:
	static const size_t MAX_CACHE_NUM = 50;

	typedef struct {
		PCSTR name;
		COLORREF ref;
		WORD attr;
	} BuildinItem;
	
	std::vector<BuildinItem> buildin;
	WORD foreground_reset;

	ColoredIcon()
	{
		const WORD R = FOREGROUND_RED;
		const WORD G = FOREGROUND_GREEN;
		const WORD B = FOREGROUND_BLUE;
		const WORD I = FOREGROUND_INTENSITY;
		const WORD RST = I + R + G + B;
		const BYTE V = 100;

		foreground_reset = RST;
		buildin = {
			{ "black",	RGB(0, 0, 0),	RST },
			{ "pink",	RGB(150, 0, 60), I + R },
			{ "red",	RGB(V, 0, 0),	R },
			{ "green",	RGB(0, V, 0),	G },
			{ "blue",	RGB(0, 0, V),	B },
			{ "yellow",	RGB(V, V, 0),	R + G },
			{ "purple",	RGB(V, 0, V),	R + B },
			{ "cyan",	RGB(0, V, V),	G + B },
		};
	}

	~ColoredIcon()
	{
		if (origin_) {
			delete origin_;
			origin_ = nullptr;
		}

		if (mask_) {
			delete mask_;
			mask_ = nullptr;
		}

		while (icons_queue_.size())
			release_cache_front();
	}

	bool init()
	{
		using Gdiplus::Status;

		origin_ = load_image_from_resource(
			ID_PNG_CMD, L"PNG");
		if (!origin_)
			return false;

		assert(origin_->GetLastStatus() == Status::Ok);
		mask_ = load_image_from_resource(
			ID_PNG_CMD_MASK, L"PNG");
		if (!mask_)
			return false;

		assert(mask_->GetLastStatus() == Status::Ok);
		get_image_gray_range(
			origin_, mask_, &gray_min_, &gray_max_);
		return true;
	}

	void colored_print(BuildinItem color, std::function<void()> func)
	{
		if (color.attr == foreground_reset) {
			func();
			return;
		}

		HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(console, color.attr);
		func();
		SetConsoleTextAttribute(console, foreground_reset);
	}

	HICON get_icon(COLORREF color)
	{
		auto it = icons_.find(color);
		if (it != icons_.end())
			return it->second;

		HICON icon = make_icon(color);
		if (icon) {
			if (icons_queue_.size() > MAX_CACHE_NUM)
				release_cache_front();

			icons_queue_.push(color);
			icons_[color] = icon;
		}

		return icon;
	}

private:
	Gdiplus::Bitmap *new_image()
	{
		return new Gdiplus::Bitmap(
			origin_->GetWidth(),
			origin_->GetHeight()
		);
	}

	HICON make_icon(COLORREF color)
	{
		Gdiplus::Bitmap *new_img = new_image();
		if (!new_img)
			return NULL;

		render_colored_image(
			origin_, mask_, new_img, gray_min_, gray_max_, color);

		HICON icon = NULL;
		new_img->GetHICON(&icon);
		delete new_img;
		return icon;
	}

	void release_cache_front()
	{
		auto it = icons_.find(icons_queue_.front());
		if (it != icons_.end()) {
			DestroyIcon(it->second);
			icons_.erase(it);
		}

		icons_queue_.pop();
	}

	Gdiplus::Bitmap *origin_;
	Gdiplus::Bitmap *mask_;
	BYTE gray_min_ = 0;
	BYTE gray_max_ = 0;

	std::map<COLORREF, HICON> icons_;
	std::queue<COLORREF> icons_queue_;
};
