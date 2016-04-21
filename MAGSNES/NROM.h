#pragma once

#include "Mapper.h"

namespace MAGSNES {

	//iNES Mapper ID 0; default MMC
	class NROM : public Mapper {
	public:
		NROM(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus);
		~NROM();
		void monitor();

	private:
		void initialize_memory();
	};

}; /* namespace NESPP */
