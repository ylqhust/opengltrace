#include "openglhelper.h"
#include <mutex>
#include "third_party/detours/src/detours.h"
#include <gl/GL.h>
#include <thread>
#include <vector>
#include <string>
#include <sstream>

std::mutex gLock;

#define TRACE_CALL_THREAD 0
#define TRACE_ALL_THREAD 1

OpenGLHelper* OpenGLHelper::Instance(int mode)
{
	static OpenGLHelper* instance = nullptr;
	if (nullptr == instance)
	{
		std::lock_guard<std::mutex> lock(gLock);
		if (nullptr == instance)
		{
			instance = new OpenGLHelper(mode == TRACE_CALL_THREAD ? GetCurrentThreadId() : 0);
		}
	}
	return instance;
}

struct TraceDetailItem
{
	DWORD ts;
	const char* name;
	DWORD threadID;
};

class OpenGLHelperPrivate
{
	OpenGLHelperPrivate(DWORD threadID)
	{
		_threadID = threadID;
		_stop = false;
		_threadEvent = ::CreateEventA(NULL, FALSE, FALSE, NULL);
		_writeFileThread.reset(new std::thread([&]() {
			writeFileThread();
			}));
	}

	~OpenGLHelperPrivate()
	{
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_stop = true;
		}
		SetEvent(_threadEvent);
		if (_writeFileThread->joinable())
			_writeFileThread->join();
		_writeFileThread.reset();
		CloseHandle(_threadEvent);
		_threadEvent = NULL;
	}
	void hookOpenGLFunctions();
	void unHookOpenGLFunctions();
	void traceAPI(const char* api);
	void writeFileThread();
	friend OpenGLHelper;
	DWORD _threadID;
	std::shared_ptr<std::thread> _writeFileThread;
	std::vector<TraceDetailItem> _item1;
	std::vector<TraceDetailItem> _item2;
	std::mutex _mutex;
	bool _stop;
	HANDLE _threadEvent;
};

OpenGLHelper::OpenGLHelper(DWORD threadID):
	_dptr(new OpenGLHelperPrivate(threadID))
{
	_dptr->hookOpenGLFunctions();
}

OpenGLHelper::~OpenGLHelper()
{
	_dptr->unHookOpenGLFunctions();
	delete _dptr;
	_dptr = nullptr;
}

void OpenGLHelper::traceAPI(const char* api)
{
	_dptr->traceAPI(api);
}

#include "generate_def_code.h"

void OpenGLHelperPrivate::hookOpenGLFunctions()
{
	HMODULE openglHandle = GetModuleHandleA("opengl32.dll");
	if (openglHandle == NULL)
	{
		MessageBoxA(NULL, "GetModuleHandleA(\"opengl32.dll\") == NULL", "ERROR", 0);
		return;
	}
	DetourTransactionBegin();
	DetourUpdateThread(::GetCurrentThread());
#include "generate_hook_code.h"
	DetourTransactionCommit();
}

void OpenGLHelperPrivate::unHookOpenGLFunctions()
{
	DetourTransactionBegin();
	DetourUpdateThread(::GetCurrentThread());
#include "generate_unhook_code.h"
	DetourTransactionCommit();
}

void OpenGLHelperPrivate::traceAPI(const char* api)
{
	DWORD threadID = GetCurrentThreadId();
	if (_threadID == 0 || threadID == _threadID)
	{
		TraceDetailItem item;
		item.ts = GetTickCount();
		item.name = api;
		item.threadID = threadID;
		_item1.push_back(item);
		if (_item1.size() >= 1000)
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_item2.empty())
				_item1.swap(_item2);
			else
			{
				_item2.insert(_item2.end(), _item1.begin(), _item1.end());
				_item1.clear();
			}
			SetEvent(_threadEvent);
		}
	}
}

inline bool SameFrame(DWORD lastTs, const TraceDetailItem& item)
{
	return item.ts - lastTs < 15;
}

void OpenGLHelperPrivate::writeFileThread()
{
	std::string filename = "D://opengltrace-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(_threadID) + ".log";
	HANDLE logfile = CreateFileA(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (logfile == NULL)
	{
		std::string err = "create file=" + filename + " failed";
		MessageBoxA(NULL, err.c_str(), "ERROR", 0);
		return;
	}
	DWORD lastTs = 0;
	int callCounter = 0;
	while (true)
	{
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_stop)
				break;
		}
		DWORD ret = ::WaitForSingleObject(_threadEvent, INFINITE);
		if (ret != WAIT_OBJECT_0)
		{ 
			std::string err = "WaitForSingleObject return wrong code=" + std::to_string(ret) + ", lastError=" + std::to_string(GetLastError());
			MessageBoxA(NULL, err.c_str(), "ERROR", 0);
			break;
		}
		std::vector<TraceDetailItem> items;
		{
			std::lock_guard<std::mutex> lock(_mutex);
			items.swap(_item2);
		}
		std::ostringstream oss;
		for (auto item : items)
		{
			if (!SameFrame(lastTs, item))
			{
				oss << "\nnew frame\n";
				lastTs = item.ts;
				callCounter = 0;
			}
			oss << item.ts << " (" << ++callCounter << ") " << item.name << "\n";
		}
		std::string content = oss.str();
		DWORD alreadyWrited = 0;
		while (alreadyWrited != content.size())
		{
			DWORD writed = 0;
			BOOL ret = WriteFile(logfile, content.c_str(), content.size(), &writed, NULL);
			if (ret == FALSE)
			{
				MessageBoxA(NULL, "WriteFile Failed", "ERROR", 0);
				break;
			}
			alreadyWrited += writed;
		}
	}
	CloseHandle(logfile);
}

