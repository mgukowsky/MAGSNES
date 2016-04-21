#pragma once

#include "Mapper.h"

namespace MAGSNES {

	//iNES Mapper ID 1
	class MMC1 : public Mapper {
	public:
		MMC1(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus);
		~MMC1();

		//MMC1 is weird :p It uses a 5 bit internal shift register which is written
		//to one bit at a time by writing to $8000 - $FFFF and, depending on the
		//address of the last write, sends that 5 bit value to one of four 
		//internal control registers which determine the banks loaded into
		//main memory and VRAM.
		void monitor();

	private:
		void initialize_memory();

		//Internal registers specific to this mapper
		word	regShift,
			numShifts,
			regCtrl, //$8000 - $9FFF
			regChrZero, //$A000 - $BFFF
			regChrOne, //$C000 - $DFFF
			regPrg; //$E000 - $FFFF

		bool 	shouldSwitchChr8kb,
			shouldSwapLoPrg,
			PrgSizeIs32KB;

		MAGSNES::byte	ChrLoSelect,
			ChrHiSelect,
			PrgSelect;
	};

} /* namespace NESPP */