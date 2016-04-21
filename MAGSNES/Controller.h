#pragma once

#include "Bus.h"
#include "Core.h"

namespace MAGSNES {

//Emulates the behavior of a standard NES controller
class Controller {

	friend class System;
	DECLARE_DEBUGGER_ACCESS

public:

	Controller(Bus *pBus);
	~Controller();

	//Called after every CPU instruction
	void tick();

	//Handle key events sent by the emulated system's interpretation of SDL_Events
	void handle_keyup(const int keycode);
	void handle_keydown(const int keycode);

	//TODO: implement this
	//mapKeys()

private:

	//Sent by the NES emulated system after a write to a controller register ($4016 for player 1 controller)
	void receive_signal(const MAGSNES::byte value);

	Core &sysCore;
	Bus &refBus;

	//Which keys are currently being held down
	bool activeStates[8];

	//Variables which determine controller behavior cycle to cycle
	MAGSNES::byte previousWrite, strobeCounter;
	bool shouldStrobe;

	//Used by strobing procedure
	static const MAGSNES::byte statemap[24];

	enum {
		BUTTON_A,
		BUTTON_B,
		BUTTON_SELECT,
		BUTTON_START,
		BUTTON_UP,
		BUTTON_DOWN,
		BUTTON_LEFT,
		BUTTON_RIGHT,
		BUTTON_NONE = 0xAB,
		BUTTON_TOTAL
	};
};

} /* namespace NESPP */