#include "NROM.h"

using namespace MAGSNES;

NROM::NROM(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus)
	: Mapper(pROM, pCPU, pPPU, pBus) {
	initialize_memory();
}

//This mapper has no internal registers, so no cleanup is needed.
NROM::~NROM() {}

//This mapper does no monitoring for address R/W
void NROM::monitor() {}

void NROM::initialize_memory() {
	word numBanksPRG_ROM = get_option(NUM_PRG_BANKS);

	// Mirror at $C000 if 1 bank(16kb) only PRG - ROM, otherwise
	//load both banks sequentially.
	//Load 1 bank CHR-ROM into VRAM pattern tables.

	if (numBanksPRG_ROM > 1) {
		load_bank_mm(0, ADDR_PRG_LOWER_BANK);
		load_bank_mm(1, ADDR_PRG_UPPER_BANK);
		//Load single CHR bank (remember that this implementation splits the 8KB CHR banks into 2 4KB banks, loaded into VRAM $0000 and $1000
		load_bank_vm(0, ADDR_CHR_LOWER_BANK);
		load_bank_vm(1, ADDR_CHR_UPPER_BANK);
	} else {
		//Otherwise, mirror single PRG bank at lo and hi banks
		load_bank_mm(0, ADDR_PRG_LOWER_BANK);
		load_bank_mm(0, ADDR_PRG_UPPER_BANK);

		//Some NROM may have no CHR data at all...
		if (get_option(NUM_CHR_BANKS) > 0) {
			load_bank_vm(0, ADDR_CHR_LOWER_BANK);
			load_bank_vm(1, ADDR_CHR_UPPER_BANK);
		}
	}

	refCPU.initialize_PC();
}