#pragma once

#include "defs.h"

#include <Windows.h>
#include <thread>

namespace MAGSNES {

//This class controls all threads (except the main thread), including sending/receiving messages, and startup/shutdown
class ThreadManager {
public:
	ThreadManager(DWORD mainThreadID);

	//Destructor blocks until all threads are finished
	~ThreadManager();

	typedef int(*ThreadProc)(HANDLE);

	enum ThreadID {
		THREAD_ID_EXEC, //Not the main thread; refers to SNES CPU etc. execution
		THREAD_ID_VIDEO,
		THREAD_ID_AUDIO,
		THREAD_ID_TOTAL
	};

	//Return value represents whether or not the thread was able to be created
	const bool start_thread(const int id, ThreadProc threadProc, HANDLE arg);

	//Shut down the child threads in the opposite order from that which they were created.
	void kill_threads();

private:
	DWORD mainThreadID;
	std::thread *execThread, *videoThread, *audioThread;
};

} /* namespace MAGSNES */