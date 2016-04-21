#include "Controller.h"

//Key codes of default buttons
#define DEFAULT_A_BUTTON						'X'
#define DEFAULT_B_BUTTON						'Z'
#define DEFAULT_SELECT_BUTTON				'A'
#define DEFAULT_START_BUTTON				'S'
#define DEFAULT_UP_BUTTON						VK_UP
#define DEFAULT_DOWN_BUTTON					VK_DOWN			//down arrow
#define DEFAULT_LEFT_BUTTON					VK_LEFT			//left arrow
#define DEFAULT_RIGHT_BUTTON				VK_RIGHT		//right arrow

#define NULL_SIGNAL									0xAB

#define CONTROLLER_REG_PLAYER_ONE		0x4016
#define CONTROLLER_REG_PLAYER_TWO		0x4017			//unimplemented; always 0

using namespace MAGSNES;

Controller::Controller(Bus *pBus)
	: refBus(*pBus), sysCore(Core::get_sys_core()),
	previousWrite(NULL_SIGNAL),
	strobeCounter(0),
	shouldStrobe(false) {

	for (int i = 0; i < 8; i++) {
		activeStates[i] = false;
	}
}

//No cleanup needed
Controller::~Controller() {}

void Controller::tick() {

	if (refBus.writeBus == CONTROLLER_REG_PLAYER_ONE) {
		receive_signal(refBus.mainMemory[CONTROLLER_REG_PLAYER_ONE]);
	}

	//***POSSIBLE BUG***: The actual behavior of the controller seems to be to continuously
	//send the value of button A until the strobe signal is received; meaning as long as
	//$4016 is equal to 1 and we are not in strobe state, button A's status should be sent to 
	//$4016. So if a game seems not to responding to button A, this may be why, although
	//omitting this behavior should be inconsequential

	//Listen for a read of the I/O register
	if (refBus.readBus == CONTROLLER_REG_PLAYER_ONE) {
		shouldStrobe = true;
	}

	if (shouldStrobe) {
		MAGSNES::byte idxToRead = statemap[strobeCounter];
		bool tmpStatus;

		if (idxToRead == BUTTON_NONE) {
			tmpStatus = false;
		} else {
			//tmpStatus = activeStates[idxToRead];
			tmpStatus = sysCore.activeKeys[idxToRead];
		}

		//I'm -pretty- sure that it doesn't matter that we're overwriting the other bits at
		//$4016, since those are apparently only used by the Zapper
		if (tmpStatus) {
			refBus.mainMemory[CONTROLLER_REG_PLAYER_ONE] = 1;
		} else {
			refBus.mainMemory[CONTROLLER_REG_PLAYER_ONE] = 0;
		}

		//ALWAYS keep the controller 2 register clear, otherwise some ROMs may interpret garbage at this address (or data written to it which is never
		//erased) as data from the second controller (i.e. pausing in the Legend of Zelda goes to the game over screen, which is triggered by certain button
		//presses on controller 2, causing a bug where $4017 is initialized/written to with garbage but is never cleared, so reads of $4017.0 always return 1, 
		//which is the equivalent of all buttons being pressed.
		refBus.mainMemory[CONTROLLER_REG_PLAYER_TWO] = 0;

		strobeCounter++;

		if (strobeCounter > 23) { //Catch bugs that may arise if a ROM keeps reading from the controller register
			strobeCounter = 0;
		}
		shouldStrobe = false;
	}

}

void Controller::receive_signal(const MAGSNES::byte value) {
	//Anticipate the next write to have bit 0 off
	if ((previousWrite == NULL_SIGNAL) && (value & 0x01)) {
		previousWrite = 1;

		//If last write had bit 0 on and current write has bit 0 off, then begin strobing
	} else if ((previousWrite == 1) && (!(value & 0x01))) {
		strobeCounter = 0;
		previousWrite = NULL_SIGNAL;

		shouldStrobe = true;
	} else {
		previousWrite = NULL_SIGNAL;
	}
}

//void Controller::handle_keydown(const int keycode) {
//	switch (keycode) {
//	case DEFAULT_A_BUTTON:
//		activeStates[BUTTON_A] = true;
//		break;
//	case DEFAULT_B_BUTTON:
//		activeStates[BUTTON_B] = true;
//		break;
//	case DEFAULT_SELECT_BUTTON:
//		activeStates[BUTTON_SELECT] = true;
//		break;
//	case DEFAULT_START_BUTTON:
//		activeStates[BUTTON_START] = true;
//		break;
//	case DEFAULT_UP_BUTTON:
//		activeStates[BUTTON_UP] = true;
//		break;
//	case DEFAULT_DOWN_BUTTON:
//		activeStates[BUTTON_DOWN] = true;
//		break;
//	case DEFAULT_LEFT_BUTTON:
//		activeStates[BUTTON_LEFT] = true;
//		break;
//	case DEFAULT_RIGHT_BUTTON:
//		activeStates[BUTTON_RIGHT] = true;
//		break;
//	default:
//		break;
//	}
//}
//
//void Controller::handle_keyup(const int keycode) {
//	switch (keycode) {
//	case DEFAULT_A_BUTTON:
//		activeStates[BUTTON_A] = false;
//		break;
//	case DEFAULT_B_BUTTON:
//		activeStates[BUTTON_B] = false;
//		break;
//	case DEFAULT_SELECT_BUTTON:
//		activeStates[BUTTON_SELECT] = false;
//		break;
//	case DEFAULT_START_BUTTON:
//		activeStates[BUTTON_START] = false;
//		break;
//	case DEFAULT_UP_BUTTON:
//		activeStates[BUTTON_UP] = false;
//		break;
//	case DEFAULT_DOWN_BUTTON:
//		activeStates[BUTTON_DOWN] = false;
//		break;
//	case DEFAULT_LEFT_BUTTON:
//		activeStates[BUTTON_LEFT] = false;
//		break;
//	case DEFAULT_RIGHT_BUTTON:
//		activeStates[BUTTON_RIGHT] = false;
//		break;
//	default:
//		break;
//	}
//}

const MAGSNES::byte Controller::statemap[24] = {
	DEFAULT_A_BUTTON, DEFAULT_B_BUTTON, DEFAULT_SELECT_BUTTON, DEFAULT_START_BUTTON,
	DEFAULT_UP_BUTTON, DEFAULT_DOWN_BUTTON, DEFAULT_LEFT_BUTTON, DEFAULT_RIGHT_BUTTON,
	BUTTON_NONE, BUTTON_NONE, BUTTON_NONE, BUTTON_NONE,
	BUTTON_NONE, BUTTON_NONE, BUTTON_NONE, BUTTON_NONE,
	BUTTON_NONE, BUTTON_NONE, BUTTON_NONE, BUTTON_NONE,
	BUTTON_NONE, BUTTON_NONE, BUTTON_NONE, BUTTON_NONE
};