#include "MMC1.h"

using namespace MAGSNES;

MMC1::MMC1(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus)
	: Mapper(pROM, pCPU, pPPU, pBus),
	regShift(0), numShifts(0), regCtrl(0), regChrZero(0),
	regChrOne(0), regPrg(0), shouldSwitchChr8kb(true), shouldSwapLoPrg(true),
	PrgSizeIs32KB(false), ChrLoSelect(0), ChrHiSelect(0), PrgSelect(0) {

	initialize_memory();
}

//No cleanup needed;
MMC1::~MMC1() {}

void MMC1::initialize_memory() {
	//On reset, load PRG bank 0 into $8000, and the last is loaded into $C000.
	load_bank_mm(0, ADDR_PRG_LOWER_BANK);
	load_bank_mm(refROM.get_num_prg_banks() - 1, ADDR_PRG_UPPER_BANK);

	//Docs suggest that CHR is left blank on reset/power on, but here we'll
	//opt to load in the first CHR bank into the pattern tables

	//TODO: figure out CHR-RAM stuff (i.e. this condition is false)
	if (refROM.get_num_chr_banks() > 0) {
		load_bank_vm(0, ADDR_CHR_LOWER_BANK);
		load_bank_vm(1, ADDR_CHR_UPPER_BANK);
	}
}

void MMC1::monitor() {
	//Don't care if address is <$8000 (bit 15 must be set)
	if (!(refBus.writeBus & 0x8000)) {
		return;
	}

	MAGSNES::byte lastByte = refBus.writeData, newMirroringType;
	word lastWrite = refBus.writeBus;

	//Reset the shift register and num shifts if bit 7 of the address is set
	if (lastByte & 128) {
		regShift = 0;
		numShifts = 0;
		//Some sources say to reset this register, others do not
		regCtrl &= 0b00011111;
		regCtrl |= 0b00001100;
		//The reset signal causes these flags to reset. It is UNCLEAR if this does anything to the mirroring selection (the lowest 2 bits of regctrl)
		shouldSwapLoPrg = true;
		PrgSizeIs32KB = false;
		return;
	}

	if (numShifts < 4) {
		//Read into shift reg 1 bit at a time
		MAGSNES::byte tmpVal = (lastByte & 1) ? 0x10 : 0;
		regShift >>= 1;
		regShift |= tmpVal;
		numShifts++;
		return;

		//On the 5th write...
	} else {
		//First we read the last bit into the shift reg
		MAGSNES::byte tmpVal = (lastByte & 1) ? 0x10 : 0;
		regShift >>= 1;
		regShift |= tmpVal;

		//isolate address bits 13 and 14 to determine register

		word regSelect = (lastWrite & (0x6000)) >> 0xD;

		switch (regSelect) {
		case 0: //$8000 - $9FFF
						//In reality there are more mirroring types than this controls
			newMirroringType = (regShift & 1) ? PPU::MIRROR_HORIZONTAL : PPU::MIRROR_VERTICAL;
			refPPU.loadMirroringType(newMirroringType);

			if (!(regShift & 0x8)) {
				PrgSizeIs32KB = true;
			} else {
				PrgSizeIs32KB = false;
				shouldSwapLoPrg = (regShift & 0x4) ? true : false;
			}

			shouldSwitchChr8kb = (regShift & 0x10) ? false : true;
			break;

		//The other 3 cases cause the actual bankswitching

		/*
		Note we don't multiply the selection here b/c MMC1 selects CHR in 4kb chunks, which is the format that this implementation stores
		CHR banks.
		*/

		case 1: //$A000 - $BFFF
			ChrLoSelect = regShift;
			
			if (get_option(NUM_CHR_BANKS) > 0) {
				if (shouldSwitchChr8kb) {
					MAGSNES::byte tmpChr = ChrLoSelect & 0xFE; //8kb CHR mode ignores lowest bit
					load_bank_vm(tmpChr, ADDR_CHR_LOWER_BANK);
					load_bank_vm(tmpChr + 1, ADDR_CHR_UPPER_BANK);

				} else {
					load_bank_vm(ChrLoSelect, ADDR_CHR_LOWER_BANK);
				}
			}
			break;

		case 2: //$C000 - $DFFF
			ChrHiSelect = regShift;

			if (get_option(NUM_CHR_BANKS) > 0) {
				if (!shouldSwitchChr8kb) { //8KB switching is done only through ChrLoSelect
					load_bank_vm(ChrHiSelect, ADDR_CHR_UPPER_BANK);
				}
			}

			break;

		case 3: //$E000 - $FFFF
			PrgSelect = regShift & 0xFF;

			if (PrgSizeIs32KB) {
				//ignore lo bit if switching 32KB, but multiply the selection by 2 to account for the 16KB size in which the banks are stored
				MAGSNES::byte tmpPrg = (PrgSelect * 2) & 0xFE;

				load_bank_mm(tmpPrg, ADDR_PRG_LOWER_BANK);
				load_bank_mm(tmpPrg + 1, ADDR_PRG_UPPER_BANK);
			} else {
				MAGSNES::byte tmpPrg = PrgSelect;

				if (shouldSwapLoPrg) {
					load_bank_mm(tmpPrg, ADDR_PRG_LOWER_BANK);
				} else {
					load_bank_mm(tmpPrg, ADDR_PRG_UPPER_BANK);
				}
			}

			break;

		default:
			break;
		}

		//Finally, we reset the shift register and the num writes
		regShift = 0;
		numShifts = 0;
	}
}