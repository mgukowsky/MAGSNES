#include "Debugger.h"

#ifdef TEST_BUILD

__FILE_SCOPE__{
	
}

using namespace MAGSNES;

Debugger::Debugger()
	: refCore(Core::get_sys_core()), hstdout(GetStdHandle(STD_OUTPUT_HANDLE)) {

	set_output_color(ScreenColor::LIGHT_PURPLE);
	std::cout << "Welcome to MAGSNES test suite!\n\n";
}

Debugger::~Debugger() {}

void Debugger::run_all_tests() {
	set_output_color(ScreenColor::LIGHT_BLUE);
	std::cout << "Running all tests...\n\n";
	run_cpu_tests();
}

void Debugger::run_cpu_tests() {
	set_output_color(ScreenColor::LIGHT_BLUE);
	std::cout << "Running CPU tests...\n";
}

#endif /* ifdef TEST_BUILD */