#include <windows.h>
#include <gdiplus.h>

#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>

#include <sstream>

#pragma comment(lib, "gdiplus")
#pragma comment(lib, "shlwapi")

#define APP_NAME L"icmp"

class error: public std::wostringstream
{
public:
	~error()
	{
		MessageBox(0, str().c_str(), APP_NAME, MB_ICONERROR);
	}
};

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
	Bitmap* bitmap = new Bitmap(file_path);
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
	SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
}

void show_usage()
{
	PCWSTR usage =
		L"usage:\n"
		L" * icmd :red\n"
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

bool parse_arg(PCWSTR *icon_file_, PCWSTR *command_)
{
	int argc = 0;
	PCWSTR cmd_line = GetCommandLine();
	PWSTR *argv = CommandLineToArgvW(cmd_line, &argc);

	if (argc == 1) {
		show_usage();
		return false;
	}

	PCWSTR icon_file = argv[1];
	if (!PathFileExists(icon_file)) {
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

HWND get_cmd_window()
{
	HWND hwnd = GetConsoleWindow();
	if (hwnd)
		FreeConsole();

	if (!AllocConsole())
		return NULL;

	return GetConsoleWindow();
}

int entry()
{
	PCWSTR icon_file = NULL;
	PCWSTR command = NULL;
	if (!parse_arg(&icon_file, &command))
		return 1;

	init();
	HICON icon = load_icon_from_file(icon_file);
	if (!icon) {
		error() << L"failed to load: " << icon_file;
		return 2;
	}

	HWND hwnd = get_cmd_window();
	if (!hwnd)
		return 3;

	peel_off_taskbar(hwnd);
	set_window_icon(hwnd, icon);
	_wsystem(command);
	return 0;
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
	return entry();
}
