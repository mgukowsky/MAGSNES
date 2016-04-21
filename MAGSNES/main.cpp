#include "main.h"

#ifdef TEST_BUILD

#include "Debugger.h"

int test_thread_proc(HANDLE arg) {
	bool *pBool = (bool *)arg;

	while (*pBool == true) {
		//infinite loop until flag is turned off
	}

	std::cout << "THREAD HAS EXITED";

	std::cout << "hi";
	std::cout << "hi";
	std::cout << "hi";
	return 0;
}

int main(int argc, char **argv) {
	bool b = true;

	//Check that threading is working properly
	MAGSNES::ThreadManager t(GetCurrentThreadId());
	t.start_thread(0, test_thread_proc, &b);

	MAGSNES::Debugger gdbg;
	gdbg.run_all_tests();

	b = false;

	return 0;
}

#else /* ifdef TEST_BUILD */

__FILESCOPE__{
	MAGSNES::BIOS *sysBIOS = nullptr;
}

void MAGSNES::boot() {
	MAGSNES::Core &sysCore = MAGSNES::Core::get_sys_core();

	if (sysBIOS == nullptr) {
		sysCore.alert_error("Attempted to boot NES emulator without initializing BIOS");
		return;
	}

	sysBIOS->open_ROM();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	MAGSNES::Core &sysCore = MAGSNES::Core::get_sys_core();
	sysBIOS = new MAGSNES::BIOS();
	const bool initSuccess = sysCore.init_window(hInstance, nCmdShow, MAGSNES::boot);

	if (!initSuccess) {
		return 1;
	}

	//sysCore.threadManager->start_thread(MAGSNES::ThreadManager::ThreadID::THREAD_ID_VIDEO, vidProc, &sysCore);

	MSG Msg;

	//Standard Windows message pump. Use peek message so the app continues to run even when there are no Windows messages
	while (sysCore.shouldRun) {
		/*if (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&Msg);
			DispatchMessageW(&Msg);
		}*/

		//GetMessage will block the main thread when there are no events, but it is MUCH less intensive than a pure PeekMessage loop
		/*if (GetMessageW(&Msg, NULL, 0, 0) > 0) {
			TranslateMessage(&Msg);
			DispatchMessageW(&Msg);
		}*/

		while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&Msg);
			DispatchMessageW(&Msg);
		}

		//This seems to be an acceptable amount of latency for user input, and lets this thread relax
		Sleep(50);

	}

	sysCore.threadManager->kill_threads();

	//Shutdown after exiting main loop
	delete sysBIOS;
	sysBIOS = nullptr;

	return Msg.wParam;
}

#endif /* ifdef TEST_BUILD */