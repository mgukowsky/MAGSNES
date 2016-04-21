#include "CNROM.h"

using namespace MAGSNES;

CNROM::CNROM(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus)
	: Mapper(pROM, pCPU, pPPU, pBus) {

	initialize_memory();
}

//No cleanup needed
CNROM::~CNROM() {}

void CNROM::monitor() {
	//Don't care if address is <$8000 (bit 15 must be set)
	if (!(refBus.writeBus & 0x8000)) {
		return;
	}

	MAGSNES::byte lastByte = refBus.writeData & 0x03; //CNROM only uses lo 2 bits to determine which bank to use

	load_bank_vm(lastByte * 2, ADDR_CHR_LOWER_BANK);
	load_bank_vm((lastByte * 2) + 1, ADDR_CHR_UPPER_BANK);
}



void CNROM::initialize_memory() {
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
		load_bank_vm(0, ADDR_CHR_LOWER_BANK);
		load_bank_vm(1, ADDR_CHR_UPPER_BANK);
	}
}