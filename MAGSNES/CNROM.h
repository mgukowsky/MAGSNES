#pragma once

#include "Mapper.h"

namespace MAGSNES {

	//iNES Mapper ID 3
	class CNROM : public Mapper {
	public:
		CNROM(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus);
		~CNROM();


		/*
		CNROM is relatively straightforward. PRG ROM is loaded in a manner identical to NROM and is not switchable,
		and writing a value to $8000 - $FFFF selects an 8KB CHR bank to load into VRAM at $0000.
		*/
		void monitor();

	private:

		void initialize_memory();

	};

} /* namespace NESPP */