#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>

#include <atlbase.h>
#include <atlconv.h>
#include <assert.h>

#include "colored_icon.hpp"

#pragma comment(lib, "gdiplus")
#pragma comment(lib, "shlwapi")

ColoredIcon g_colored_icon;

void init_gdi_plus()
{
	using namespace Gdiplus;
	GdiplusStartupInput input;
	ULONG_PTR token;
	GdiplusStartup(&token, &input, NULL);
}

void init()
{
	init_gdi_plus();
}

HICON load_icon_from_file(PCWSTR file_path)
{
	using namespace Gdiplus;
	Bitmap *bitmap = new Bitmap(file_path);
	if (bitmap->GetLastStatus() != Status::Ok)
		return NULL;

	HICON icon = NULL;
	bitmap->GetHICON(&icon);
	return icon;
}

void set_app_id(HWND hwnd, PCWSTR app_id)
{
	IPropertyStore *pps;
	HRESULT hr = SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&pps));
	if (!SUCCEEDED(hr))
		return;

	PROPVARIANT pv;
	hr = InitPropVariantFromString(app_id, &pv);
	if (SUCCEEDED(hr)) {
		hr = pps->SetValue(PKEY_AppUserModel_ID, pv);
		PropVariantClear(&pv);
	}

	pps->Release();
}

void peel_off_taskbar(HWND hwnd)
{
	wchar_t uuid[1024] = { 0 };

#pragma warning(push)
#pragma warning(disable: 28159)
	wsprintfW(uuid, L"%d-%d", GetTickCount(), (int)hwnd);
#pragma warning(pop)

	set_app_id(hwnd, uuid);
}

void set_window_icon(HWND hwnd, HICON icon)
{
	if (!icon)
		return;

	SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
}

void show_usage()
{
	PCWSTR usage =
		L"usage:\n"
		L" * icmd :red\n"
		L" * icmd :browse\n"
		L" * icmd image.png\n"
		L" * icmd image.png echo \"quoted\" ^& pause"
	;
	MessageBox(NULL, usage, APP_NAME, MB_ICONINFORMATION);
}

bool parse_arg_command(
	PCWSTR cmd_line,
	PWSTR *argv,
	PCWSTR icon_file,
	PCWSTR *command_)
{
	PCWSTR app_path = argv[0];
	PCWSTR pos = wcsstr(cmd_line, app_path);
	if (!pos)
		return false;

	pos = wcsstr(pos, icon_file);
	if (!pos)
		return false;

	PCWSTR command = pos + wcslen(icon_file);
	if (command[0] == L'"')
		++command;

	*command_ = L"";
	size_t command_len = wcslen(command);

	for (size_t i = 0; i < command_len; ++i) {
		if (command[i] != L' ') {
			*command_ = command;
			break;
		}
	}

	return true;
}

bool is_app_switch(PCWSTR filename)
{
	return filename[0] == L':';
}

bool parse_arg(PCWSTR *icon_file_, PCWSTR *command_)
{
	int argc = 0;
	PCWSTR cmd_line = COMMAND_LINE;
	PWSTR *argv = CommandLineToArgvW(cmd_line, &argc);

	if (argc == 1) {
		show_usage();
		return false;
	}

	PCWSTR icon_file = argv[1];
	if (!is_app_switch(icon_file) && !PathFileExists(icon_file)) {
		error() << L"file not exists: " << icon_file;
		return false;
	}

	*icon_file_ = icon_file;

	bool is_ok = parse_arg_command(
		cmd_line, argv, icon_file, command_);
	if (!is_ok || !wcslen(*command_)) {
		*command_ = L"cmd";
	}

	return true;
}

void redirect_stdio()
{
	FILE* dummy;
	freopen_s(&dummy, "CONIN$", "r", stdin);
	freopen_s(&dummy, "CONOUT$", "w", stderr);
	freopen_s(&dummy, "CONOUT$", "w", stdout);
}

HWND get_cmd_window()
{
	HWND hwnd = GetConsoleWindow();
	if (hwnd)
		FreeConsole();

	if (!AllocConsole())
		return NULL;

	redirect_stdio();
	return GetConsoleWindow();
}

bool parse_app_switch(PCWSTR switch_, HICON *icon)
{
	if (wcscmp(switch_, L"browse") == 0) {
		*icon = NULL;
		return true;
	}

	const size_t total = g_colored_icon.buildin.size();
	for (size_t i = 0; i < total; ++i) {
		auto &item = g_colored_icon.buildin[i];
		if (strcmp(ATL::CW2A(switch_), item.name) == 0) {
			*icon = g_colored_icon.get_icon(item.ref);
			return true;
		}
	}

	return false;
}

bool load_app_icon(PCWSTR icon_file, HICON *icon)
{
	if (is_app_switch(icon_file)) {
		if (parse_app_switch(&icon_file[1], icon))
			return true;

		show_usage();
		return false;
	}

	*icon = load_icon_from_file(icon_file);
	bool is_ok = *icon;
	if (!is_ok)
		error() << L"failed to load: " << icon_file;
	
	return is_ok;
}

void browse_show_preview(HWND hwnd, size_t selected)
{
	system("cls");
	printf("Use Up/Down key to select color, press Ctrl+C to exit.\n\n");

	auto color = g_colored_icon.buildin[selected];
	g_colored_icon.colored_print(color, [&] {
		printf("  -> [%02d]  %s\n\n", selected + 1, color.name);
	});

	printf("You can use command: \"%ls :%s\"\n\n", APP_NAME, color.name);
	HICON icon = g_colored_icon.get_icon(color.ref);
	set_window_icon(hwnd, icon);
}

void browse(HWND hwnd)
{
	const int KEY_UP = 72;
	const int KEY_DOWN = 80;
	const int END_OF_TEXT = 3;
	const int ARROW_KEY = 224;

	const size_t total = g_colored_icon.buildin.size();
	size_t selected = 0;
	bool arrow_key = false;

	browse_show_preview(hwnd, selected);
	while (true) {
		int ch = _getch();
		switch (ch) {
		case ARROW_KEY:
			arrow_key = true;
			continue;
		case END_OF_TEXT:
			return;
		}

		if (!arrow_key)
			continue;

		switch (ch) {
		case KEY_UP:
			selected += (total - 1);
			break;
		case KEY_DOWN:
			selected += 1;
			break;
		default:
			continue;
		}

		selected %= total;
		browse_show_preview(hwnd, selected);
	}
}

bool entry()
{
	PCWSTR icon_file = NULL;
	PCWSTR command = NULL;
	if (!parse_arg(&icon_file, &command))
		return false;

	init();
	if (!g_colored_icon.init())
		return false;

	HICON icon = NULL;
	if (!load_app_icon(icon_file, &icon))
		return false;

	HWND hwnd = get_cmd_window();
	if (!hwnd)
		return false;

	peel_off_taskbar(hwnd);
	if (icon) {
		set_window_icon(hwnd, icon);
		_wsystem(command);
	} else {
		browse(hwnd);
	}

	return true;
}

#pragma warning(push)
#pragma warning(disable: 28251)
int WINAPI WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nShowCmd)
#pragma warning(pop)
{
	entry();
	return 0;
}
