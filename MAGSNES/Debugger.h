#pragma once

#ifdef TEST_BUILD

#include <iostream>
#include <cassert>

#include "Core.h"

namespace MAGSNES {

class Debugger {
public:
	Debugger();
	~Debugger();

	void run_all_tests();
	void run_cpu_tests();

private:
	//Text colors for windows console (always on black background). 
	//Bits 0-3 are text color, bits 4-7 are background color.
	enum class ScreenColor {
		BLACK,
		DARK_BLUE,
		DARK_GREEN,
		DULL_BLUE,
		DARK_RED,
		DARK_PURPLE,
		DARK_ORANGE,
		LIGHT_GRAY,
		DARK_GRAY,
		BRIGHT_BLUE,
		LIGHT_GREEN,
		LIGHT_BLUE,
		LIGHT_RED,
		LIGHT_PURPLE,
		LIGHT_YELLOW,
		WHITE,
		COLOR_TOTAL
	};

	Core &refCore;
	HANDLE hstdout;

	void set_output_color(const ScreenColor color) {
		SetConsoleTextAttribute(hstdout, (word)color);
	}

};

} /* namespace MAGSNES */

#endif /* ifdef TEST_BUILD */