#pragma once

#include <string>

#include "core.h"
#include "ROM.h"
#include "CPU.h"
#include "PPU.h"
#include "APU.h"
#include "Controller.h"
#include "Mapper.h"
#include "MAPPER_INCLUDE.h"

namespace MAGSNES {

	//Responsible for timing and coordinating component behaviors
	class System {

		DECLARE_DEBUGGER_ACCESS

	public:
		System(GLManager *pGLM);
		~System();
		void loadROM(const char * const path);
		void ejectROM();
		const MAGSNES::byte step();

		//Used by the program to figure out when to close the program
		bool done = false;

		int EXIT_CODE;

		//Display a message to the screen for 5 seconds (public wrapper for Core)
		//void post_message(char *msg) { pCore->post_message(msg); }

		void save_state();
		void load_state();

		enum {
			EXIT_CODE_QUIT,
			EXIT_CODE_RESET,
			EXIT_CODE_ERROR
		};

	private:
		ROM *currentROM;
		Core &sysCore;
		Bus *pBus;
		CPU *pCPU;
		PPU *pPPU;
		APU *pAPU;
		Controller *pController;
		Mapper *currentMapper;

		bool isRunning;

	};

} /* namespace NESPP */