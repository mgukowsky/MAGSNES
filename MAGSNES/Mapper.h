#pragma once

#include "ROM.h"
#include "CPU.h"
#include "PPU.h"

namespace MAGSNES {

	const int ADDR_PRG_LOWER_BANK = 0x8000,
						ADDR_PRG_UPPER_BANK = 0xC000,
						ADDR_CHR_LOWER_BANK = 0x0000,
						ADDR_CHR_UPPER_BANK = 0x1000,

						//Some mappers (i.e. MMC3) need the banks to be divided into smaller units
						ADDR_PRG_LOWER_BANK_UPPER_HALF = 0xA000,
						ADDR_CHR_LOWER_BANK_SECOND_QUARTER = 0x0400,
						ADDR_CHR_LOWER_BANK_UPPER_HALF = 0x0800,
						ADDR_CHR_LOWER_BANK_FOURTH_QUARTER = 0x0C00;

	//Base class for mappers
	class Mapper {
	public:
		Mapper(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus)
			: refROM(*pROM), refCPU(*pCPU), refPPU(*pPPU), refBus(*pBus) {}

		virtual ~Mapper() {};
		virtual void monitor() = 0;

	protected:
		ROM &refROM;
		CPU &refCPU;
		PPU &refPPU;
		Bus &refBus;

		enum {
			NUM_PRG_BANKS,
			NUM_CHR_BANKS
		};

		//Non-virtual protected methods which allow Mapper children to use Mapper's friends
		word get_option(const MAGSNES::byte optionID) {
			switch (optionID) {
			case NUM_PRG_BANKS:
				return refROM.options->numBanksPRG_ROM;
				break;

			case NUM_CHR_BANKS:
				return refROM.options->numBanksCHR_ROM;
				break;

			default:
				return 0;
				break;
			}
		}

		//Loads a 16KB PRG_ROM bank into CPU main memory
		void load_bank_mm(const word bankID, const word startAddr) {
			const BankPRG &tmp = *(refROM.get_prg_bank(bankID));

			for (int i = 0; i < PRG_BANK_SIZE; i++) {
				refCPU.refMM[startAddr + i] = tmp.data[i];
			}
		}

		//Loads an 8KB PRG_ROM bank into CPU main memory
		void load_bank_mm_8K(const word bankID, const word startAddr, const bool shouldUseUpperHalf) {
			const BankPRG &tmp = *(refROM.get_prg_bank(bankID));
			const word LIMIT = PRG_BANK_SIZE / 2;
			const word BANK_OFFSET = (shouldUseUpperHalf) ? LIMIT : 0;

			for (int i = 0; i < LIMIT; i++) {
				refCPU.refMM[startAddr + i] = tmp.data[i + BANK_OFFSET];
			}
		}

		//Loads a 4KB CHR_ROM bank into VRAM
		void load_bank_vm(const word bankID, const word startAddr) {
			const BankCHR &tmp = *(refROM.get_chr_bank(bankID));

			for (int i = 0; i < CHR_BANK_SIZE; i++) {
				refPPU.refVM[startAddr + i] = tmp.data[i];
			}
		}

		//Loads a 2KB CHR_ROM bank into VRAM
		void load_bank_vm_2k(const word bankID, const word startAddr, const bool shouldUseUpperHalf) {
			const BankCHR &tmp = *(refROM.get_chr_bank(bankID));
			const word LIMIT = CHR_BANK_SIZE / 2;
			const word BANK_OFFSET = (shouldUseUpperHalf) ? LIMIT : 0;

			for (int i = 0; i < LIMIT; i++) {
				refPPU.refVM[startAddr + i] = tmp.data[i + BANK_OFFSET];
			}
		}

		//Loads a 1KB CHR_ROM bank into VRAM; note that we need the enum to pick which quarter of the bank to select
		enum class CHR_QUARTER {
			CHR_FIRST_QUARTER,
			CHR_SECOND_QUARTER,
			CHR_THIRD_QUARTER,
			CHR_FOURTH_QUARTER
		};

		void load_bank_vm_1k(const word bankID, const word startAddr, const CHR_QUARTER quarter) {
			const BankCHR &tmp = *(refROM.get_chr_bank(bankID));
			const word LIMIT = CHR_BANK_SIZE / 4;
			word BANK_OFFSET;

			switch (quarter) {
			case CHR_QUARTER::CHR_FIRST_QUARTER:
				BANK_OFFSET = 0;
				break;
			case CHR_QUARTER::CHR_SECOND_QUARTER:
				BANK_OFFSET = LIMIT;
				break;
			case CHR_QUARTER::CHR_THIRD_QUARTER:
				BANK_OFFSET = LIMIT * 2;
				break;
			case CHR_QUARTER::CHR_FOURTH_QUARTER:
				BANK_OFFSET = LIMIT * 3;
				break;

			//Don't need default b/c we are using an enum class
			}

			for (int i = 0; i < LIMIT; i++) {
				refPPU.refVM[startAddr + i] = tmp.data[i + BANK_OFFSET];
			}
		}

	private:
		//This function loads ROM data into RAM and VRAM, and MUST set the CPU PC register to the appropriate value (usually the reset vector)
		virtual void initialize_memory() = 0;
	};

} /* namespace NESPP */