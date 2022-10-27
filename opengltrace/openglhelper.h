#pragma once

#include <windows.h>

class OpenGLHelperPrivate;
class OpenGLHelper
{
public:
	static OpenGLHelper* Instance(int mode = 0);
	~OpenGLHelper();
	void traceAPI(const char* api);
private:
	OpenGLHelper(DWORD threadID);
private:
	OpenGLHelperPrivate* _dptr;
	friend OpenGLHelperPrivate;
};

