#pragma once

#include "Mapper.h"

namespace MAGSNES {

//iNES Mapper ID 4
class MMC3 : public Mapper {
public:
	MMC3(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus);
	~MMC3();

	void monitor();

private:
	void initialize_memory();

	MAGSNES::byte IRQCounter, IRQReloadValue;
	bool isIRQEnabled, shouldInvertCHRBanks;

	//MMC3 has a number of banks which can be selected.
	//Each of these constants is in the form [chr/prg rom][size][base address to load]
	enum {
		CHR2K0x0000,
		CHR2K0x0800,
		CHR1K0x1000,
		CHR1K0x1400,
		CHR1K0x1800,
		CHR1K0x1C00,
		PRG8K0x8000,
		PRG8K0xA000
	};

};

} /* namespace MAGSNES */