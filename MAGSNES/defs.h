#pragma once

#include <cstdio>
#include <cstdint>

//Syntactic sugar
#define __FILESCOPE__				namespace  //For anonymous namespaces
#define __CLASSVARIABLE__		static
#define __CLASSMETHOD__			static

#define FORCEINLINE					__forceinline

#ifdef TEST_BUILD
#define DECLARE_DEBUGGER_ACCESS		friend class Debugger;
#else
#define DECLARE_DEBUGGER_ACCESS
#endif

namespace MAGSNES {

	//typedef std::runtime_error AppError;

	typedef uint8_t		byte;
	typedef uint16_t	word;
	typedef uint32_t	dword;
	typedef uint64_t	qword;

	const int NES_SCREEN_WIDTH = 256;
	const int NES_SCREEN_HEIGHT = 240;

	const int DEFAULT_SCREEN_SIZE_FACTOR = 4;

	const int DEFAULT_WINDOW_WIDTH = NES_SCREEN_WIDTH * DEFAULT_SCREEN_SIZE_FACTOR;
	const int DEFAULT_WINDOW_HEIGHT = NES_SCREEN_HEIGHT * DEFAULT_SCREEN_SIZE_FACTOR;

	const dword MICROSECONDS_PER_SECOND = 1000000;
	const dword MICROSECONDS_PER_30FPS_FRAME = MICROSECONDS_PER_SECOND / 30;
	const dword MICROSECONDS_PER_60FPS_FRAME = MICROSECONDS_PER_SECOND / 60;

	//TODO: put this in a better place
	enum class DutyCycle {
		DUTY_CYCLE_HALF,
		DUTY_CYCLE_QUARTER,
		DUTY_CYCLE_EIGHTH
	};

} /* namespace MAGSNES */