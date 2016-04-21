#pragma once

#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include "Core.h"

namespace MAGSNES {

	const word	PRG_BANK_SIZE = 0x4000,
		CHR_BANK_SIZE = 0x1000;

	//Structs to hold a single PRG or CHR bank
	struct BankPRG { //16KB
		int id;
		MAGSNES::byte data[PRG_BANK_SIZE];
	};

	struct BankCHR { //4KB
		int id;
		MAGSNES::byte data[CHR_BANK_SIZE];
	};

	class ROM {
		friend class Mapper;

	public:
		//Checks if it is a *.nes file
		ROM(const char * const path);
		~ROM();

		const BankPRG * const get_prg_bank(const int id);
		const BankCHR * const get_chr_bank(const int id);

		//Needed by the emulator to select the correct mapper
		const word get_mapper_id();
		const MAGSNES::byte get_mirroring_type();

		const MAGSNES::byte get_num_prg_banks();
		const MAGSNES::byte get_num_chr_banks();

		const std::string ROM_NAME;

		Core &sysCore;

	private:
		struct ROMINFO {
			const word numBanksPRG_ROM, numBanksCHR_ROM, numBanksPRG_RAM, mapperID;
			const bool hasVerticalMirroring, hasBatteryRAM, hasTrainer, hasFourScreenVRAM;
		} *options;

		std::vector<const BankPRG*> PRGBanks;
		std::vector<const BankCHR*> CHRBanks;

		//Check for magic bytes in file header
		bool is_iNES(std::basic_ifstream<MAGSNES::byte> &src);
	};

}/* namespace NESPP */
