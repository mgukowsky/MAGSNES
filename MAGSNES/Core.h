#pragma once

#include "ThreadManager.h" /* need to include Windows.h before GL headers */

#include <gl/glew.h>

#include "resource.h"

namespace MAGSNES {
	
class Core {
	DECLARE_DEBUGGER_ACCESS

public:
	Core();
	~Core();	

	typedef void(*Callback)(void);

	//This is the callback to invoke when a file ROM needs to be opened
	Callback bootProc;

	struct AudioRegs {
		dword SAMPLE_FREQUENCY;
		float baseAmp, triangleAmp;
		float square0Period, square0positiveAmp, square0negativeAmp,
					square1Period, square1positiveAmp, square1negativeAmp,
					trianglePeriod;
		DutyCycle square0DutyCycle, square1DutyCycle;
		bool square0ImplicitOff, square1ImplicitOff, triangleImplicitOff;
	} audioRegs;

	//*****TODO: put these flags into a struct*****
	//This flag controls when the main message pump should continue running
	bool shouldRun;

	//This flag controls whether or not the child threads should continue running after completing a loop iteration
	bool shouldEmulate;

	//This flag controls whether the child threads should execute or remain idle
	bool shouldHalt;

	//Controls when the video thread should draw the contents of one of its buffers
	bool shouldDrawFrame;

	//Signals to the video thread that the execution thread is still running, so it must not exit and delete the dependencies it owns which the
	//execution thread is still using
	bool isExecRunning;

	//Dimensions of the drawable portion (TODO: clarify) of the window
	int contextWidth, contextHeight;

	ThreadManager *threadManager;

	char fileSelection[MAX_PATH];
	bool activeKeys[256];

	//Singleton access
	__CLASSMETHOD__ Core & get_sys_core();

	//Registers window
	const bool init_window(HINSTANCE hInstance, int nCmdShow, Callback pBootProc);

	//Needs to be static in order to be pointed to by the window class. Public so it can be called by Windows OS outside the class.
	__CLASSMETHOD__ LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	//Outputs a message to a stream
	void logmsg(const char * const msg) {
		fprintf_s(stdout, "MESSAGE: %s\n", msg);
	}

	void logmsg(const wchar_t * const msg) {
		fwprintf_s(stdout, L"MESSAGE: %s\n", msg);
	}

	void logerr(const char * const msg) {
		fprintf_s(stderr, "!!!ERROR!!!: %s\n", msg);
	}

	void logerr(const wchar_t * const msg) {
		fwprintf_s(stderr, L"!!!ERROR!!!: %s\n", msg);
	}

	//Displays a message to the user and outputs the message to a stream
	void alert_message(const char * const msg);
	void alert_message(const wchar_t * const msg);
	void alert_error(const char * const msg);
	void alert_error(const wchar_t * const msg);

	HWND get_main_window() { return hwnd; }
	LARGE_INTEGER &get_cpu_freq() { return CPU_FREQ; }

	//Get the difference btwn start and end, place result into dst
	FORCEINLINE void get_elapsed_microseconds(LARGE_INTEGER &start, LARGE_INTEGER &end, LARGE_INTEGER &dst) {
		dst.QuadPart = end.QuadPart - start.QuadPart;
		dst.QuadPart *= 1000000;
		dst.QuadPart /= CPU_FREQ.QuadPart;
	}

	enum class AudioChannelID {
		SQUARE_0,
		SQUARE_1,
		TRIANGLE,
		NOISE
	};

	void set_channel_period(const float rawFrequency, const Core::AudioChannelID id) {
		switch (id) {
		case Core::AudioChannelID::SQUARE_0:
			audioRegs.square0Period = audioRegs.SAMPLE_FREQUENCY / rawFrequency;
			break;
		case Core::AudioChannelID::SQUARE_1:
			audioRegs.square1Period = audioRegs.SAMPLE_FREQUENCY / rawFrequency;
			break;
		case Core::AudioChannelID::TRIANGLE:
			audioRegs.trianglePeriod = audioRegs.SAMPLE_FREQUENCY / rawFrequency;
			break;
		}
	}

	void set_channel_amplitude(const float amp, const Core::AudioChannelID id) {
		switch (id) {
		case Core::AudioChannelID::SQUARE_0:
			audioRegs.square0negativeAmp = amp * -1;
			audioRegs.square0positiveAmp = amp;
			break;
		case Core::AudioChannelID::SQUARE_1:
			audioRegs.square1negativeAmp = amp * -1;
			audioRegs.square1positiveAmp = amp;
			break;
		}
	}

private:
	//Windows API-specifc IVs
	HWND hwnd;
	RECT glRect;
	LARGE_INTEGER CPU_FREQ, PROGRAM_START;

	qword framesDrawn;

	const bool select_file();
	void execute_keyboard_shortcut(const WPARAM wParam); //Post messages when keys are pressed when CTRL is held
};

} /* namespace MAGSNES */

//Handles UI and Platform-specific stuff