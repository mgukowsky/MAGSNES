#pragma once

#include "Mapper.h"

namespace MAGSNES {

	//iNES Mapper ID 2
	class UNROM : public Mapper {
	public:
		UNROM(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus);
		~UNROM();


		/*
		UNROM is one of the more straightforward mappers. Upon writing to $8000 - $FFFF,
		the value written is used as the ID of the 16KB PRG bank to load into $8000 ($C000 never changes).
		That's it :D
		*/
		void monitor();

	private:

		void initialize_memory();

	};

} /* namespace NESPP */