#pragma once

#include "System.h"
#include "GLManager.h"
#include "AudioManager.h"

namespace MAGSNES {

class BIOS {
public:
	BIOS();
	~BIOS();

	void open_ROM();
	void eject_ROM();

private:
	Core &sysCore;
	System *pSys;

	//Procedures for the child threads. Must be static so they can be pointed to, and must be in the format: (int)(*)(void *)
	static int start_execution(HANDLE contextArg);
	static int start_video(HANDLE contextArg);
	static int start_audio(HANDLE contextArg);

};

}