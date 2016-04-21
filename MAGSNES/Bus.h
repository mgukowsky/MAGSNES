#pragma once

#include "defs.h"

namespace MAGSNES {

const dword	MM_SIZE = 0x10000,
						VM_SIZE = 0x4000,
						OAM_SIZE = 0x100;

//This class should be treated as a struct (only a class b/c
//of reset() member); no getters/setters
class Bus {
public:
	byte mainMemory[MM_SIZE], VM[VM_SIZE], OAM[OAM_SIZE];
	word readBus, writeBus, writeData;

	void reset() {
		readBus = 0;
		writeBus = 0;

		for (int i = 0; i < MM_SIZE; i++) {
			mainMemory[i] = 0;
		}
	}
};

}/* namespace NESPP */