#pragma once
#include <sstream>
#include <functional>

#define APP_NAME L"icmd"

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define AUTO_DEFER_VAR CONCAT(__defer_var_, __LINE__)
#define DEFER _DeferHelper AUTO_DEFER_VAR

#ifdef _DEBUG
#define COMMAND_LINE APP_NAME L" :browse"
#else
#define COMMAND_LINE GetCommandLine()
#endif

struct _DeferHelper
{
	typedef std::function<void()> Func;
	Func func_;
	_DeferHelper(Func func) :func_(func) {}
	~_DeferHelper() { func_(); }
};

class error: public std::wostringstream
{
public:
	~error()
	{
		MessageBox(0, str().c_str(), APP_NAME, MB_ICONERROR);
	}
};
