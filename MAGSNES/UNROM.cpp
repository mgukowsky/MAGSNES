#include "UNROM.h"

using namespace MAGSNES;

UNROM::UNROM(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus)
	: Mapper(pROM, pCPU, pPPU, pBus) {

	initialize_memory();
}

//No cleanup needed
UNROM::~UNROM() {}

void UNROM::monitor() {
	//Don't care if address is <$8000 (bit 15 must be set)
	if (!(refBus.writeBus & 0x8000)) {
		return;
	}

	MAGSNES::byte lastByte = refBus.writeData;

	load_bank_mm(lastByte, ADDR_PRG_LOWER_BANK);
}



void UNROM::initialize_memory() {
	//On reset, load PRG bank 0 into $8000, and the last is loaded into $C000.
	load_bank_mm(0, ADDR_PRG_LOWER_BANK);
	load_bank_mm(refROM.get_num_prg_banks() - 1, ADDR_PRG_UPPER_BANK);
}