#include "BIOS.h"

__FILESCOPE__{
	MAGSNES::GLManager *pSharedGLM = nullptr;
	MAGSNES::AudioManager *pSharedALM = nullptr;
}

using namespace MAGSNES;

BIOS::BIOS()
	: sysCore(Core::get_sys_core()), pSys(nullptr) {
	
}

BIOS::~BIOS() {}

void BIOS::open_ROM() {
	sysCore.shouldEmulate = true;
	sysCore.threadManager->start_thread(ThreadManager::ThreadID::THREAD_ID_AUDIO, BIOS::start_audio, this);
	sysCore.threadManager->start_thread(ThreadManager::ThreadID::THREAD_ID_VIDEO, BIOS::start_video, this);
	sysCore.threadManager->start_thread(ThreadManager::ThreadID::THREAD_ID_EXEC, BIOS::start_execution, this);
}

void BIOS::eject_ROM() {
	sysCore.shouldEmulate = false;
}

int BIOS::start_execution(HANDLE contextArg) {
	while (pSharedGLM == nullptr) {
		//Wait for video thread to start up the GLM if it's not already
	}

	MAGSNES::Core &sysCore = Core::get_sys_core();
	MAGSNES::GLManager &glm = *pSharedGLM;
	BIOS &context = *((BIOS *)contextArg);

	sysCore.isExecRunning = true;

	LARGE_INTEGER start, end, elapsed;
	QueryPerformanceCounter(&start);
	dword cyclesTaken;

	context.pSys = new System(&glm);
	context.pSys->loadROM(context.sysCore.fileSelection);

	MAIN_EXEC_LOOP:
	while (context.sysCore.shouldEmulate) {
		if (context.sysCore.shouldHalt) {
			continue;
		}

		QueryPerformanceCounter(&start);
		cyclesTaken = 0;

		while (cyclesTaken < 1789) { //At ~1.789 MHz, we have 1789 NES CPU cycles per millisecond
			cyclesTaken += context.pSys->step();
		}

		//Wait until 1 millisecond has passed.
		while (true) {

			QueryPerformanceCounter(&end);
			context.sysCore.get_elapsed_microseconds(start, end, elapsed);
			if (elapsed.QuadPart > 1000) { //CHANGING THIS NUMBER IS THE EASIEST WAY TO ALTER THE EMULATION SPEED
				break;
			}
			
		}
		
	}

	//The gotos are used to make sure we don't kill the thread until the user wants to quit the program (std::thread seems to have trouble exiting and restarting...)
	while (context.sysCore.shouldRun) {
		if (context.sysCore.shouldEmulate) {
			goto MAIN_EXEC_LOOP;
		}
	}

	delete context.pSys;
	context.pSys = nullptr;
	context.sysCore.logmsg("Video thread exited successfully");

	sysCore.isExecRunning = false;

	return 0;
}

int BIOS::start_video(HANDLE contextArg) {
	MAGSNES::Core &sysCore = Core::get_sys_core();
	pSharedGLM = new GLManager(sysCore);
	GLManager &glm = *pSharedGLM;
	glm.hook_up_gl();
	BIOS &context = *((BIOS *)contextArg);

	static const int FRAME_INTERVAL = 1000 / 60;
	static LARGE_INTEGER start, end, elapsed;

	ZeroMemory(&start, sizeof(LARGE_INTEGER));
	ZeroMemory(&end, sizeof(LARGE_INTEGER));
	ZeroMemory(&elapsed, sizeof(LARGE_INTEGER));

	QueryPerformanceCounter(&start);

	MAIN_VIDEO_LOOP:
	while (context.sysCore.shouldEmulate) {
		if (context.sysCore.shouldHalt) {
			continue;
		}

		if (context.sysCore.shouldDrawFrame) {
			glm.update_screen();
			context.sysCore.shouldDrawFrame = false;

			QueryPerformanceCounter(&end);
			context.sysCore.get_elapsed_microseconds(start, end, elapsed);
			//Let the the thread relax (at 60 fps, frames will never be less than 16 milliseconds apart.
			if (elapsed.QuadPart > (FRAME_INTERVAL * 1000)) { //Make sure we don't pass Sleep() a negative
				Sleep(0);
			} else {
				Sleep(FRAME_INTERVAL - (elapsed.QuadPart / 1000));
			}
			QueryPerformanceCounter(&start);
		}
	}

	//The gotos are used to make sure we don't kill the thread until the user wants to quit the program (std::thread seems to have trouble exiting and restarting...)
	while (context.sysCore.shouldRun) {
		if (context.sysCore.shouldEmulate) {
			goto MAIN_VIDEO_LOOP;
		}
	}

	while (context.sysCore.isExecRunning) {
		volatile int i = 0; //volatile == don't optimize this loop away
		i++;
		//Wait until the execution thread is finishing, so it does not try to dereference
		//resources which this thread owns and will destroy upon termination
	}

	delete pSharedGLM;
	pSharedGLM = nullptr;

	return 0;
}

int BIOS::start_audio(HANDLE contextArg) {
	MAGSNES::Core &sysCore = Core::get_sys_core();
	BIOS &context = *((BIOS *)contextArg);
	pSharedALM = new AudioManager();
	AudioManager &alm = *pSharedALM;

	alm.begin_audio();

	/* MAIN LOOP */
	alm.main_audio_loop(); //Unlike the other two child threads, this thread persists even after a ROM is closed, until the app exits.
	/* END MAIN LOOP */

	alm.end_audio();

	delete pSharedALM;
	pSharedALM = nullptr;

	return 0;
}