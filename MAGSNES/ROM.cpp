#include "ROM.h"

#define byte	MAGSNES::byte

using namespace MAGSNES;

ROM::ROM(const char * const path)
	: ROM_NAME(path), sysCore(Core::get_sys_core()) {

	//Are we opening a *.nes file?
	const int STRLIMIT = ROM_NAME.length();
	if (ROM_NAME[STRLIMIT - 3] != 'n' ||
		ROM_NAME[STRLIMIT - 2] != 'e' ||
		ROM_NAME[STRLIMIT - 1] != 's') {

		//*****TODO: stop execution of the ROM here. Maybe move this code outside of the constructor? But would break RAII...
		sysCore.alert_error("Not an iNES file");
	}

	std::basic_ifstream<byte> raw;
	raw.open(path, std::basic_ifstream<byte>::in | std::basic_ifstream<byte>::binary);

	if (!is_iNES(raw)) {
		sysCore.alert_error("It appears that the ROM you opened is not a valid iNES file, or it may be corrupted");
	}

	byte headerChars[12];
	raw.get(headerChars, 12);
	raw.get(); //advance one extra char

	word	prgRamBanks = (headerChars[4] == 0) ? 1 : headerChars[4],
		mapper = ((headerChars[2]) >> 4) | (headerChars[3] & 0xF0);

	//Set options based on header values
	options = new ROMINFO{
		headerChars[0], //numBanksPRG_ROM
		headerChars[1],	//numBanksCHR_ROM
		prgRamBanks, //numBanksPRG_RAM
		mapper, //mapperID
		!!(headerChars[2] & 0x01), //hasVerticalMirroring
		!!(headerChars[2] & 0x02), //hasBatteryRAM
		!!(headerChars[2] & 0x04), //hasTrainer
		!!(headerChars[2] & 0x08)  //hasFourScreenVRAM
	};

	//Load PRG banks
	for (int i = 0; i < options->numBanksPRG_ROM; i++) {
		BankPRG *tmp = new BankPRG;
		tmp->id = i;

		for (int j = 0; j < PRG_BANK_SIZE; j++) {
			tmp->data[j] = raw.get();
		}

		PRGBanks.push_back(tmp);
	}

	//Load CHR banks
	for (int i = 0; i < (options->numBanksCHR_ROM) * 2; i++) { //We multiply by 2 b/c iNES measures number of CHR banks in 8KB chunks
		BankCHR *tmp = new BankCHR;
		tmp->id = i;

		for (int j = 0; j < CHR_BANK_SIZE; j++) {
			tmp->data[j] = raw.get();
		}

		CHRBanks.push_back(tmp);
	}

}

ROM::~ROM() {
	for (std::vector<const BankPRG*>::const_iterator i = PRGBanks.begin(); i < PRGBanks.end(); i++) {
		delete *i;
	}

	for (std::vector<const BankCHR*>::const_iterator i = CHRBanks.begin(); i < CHRBanks.end(); i++) {
		delete *i;
	}

	delete options;

}

const BankPRG * const ROM::get_prg_bank(const int id) {
	return PRGBanks[id];
}

const BankCHR * const ROM::get_chr_bank(const int id) {
	return CHRBanks[id];
}

const word ROM::get_mapper_id() {
	return options->mapperID;
}

const byte ROM::get_mirroring_type() {
	return (options->hasVerticalMirroring) ? 1 : 0;
}

const byte ROM::get_num_prg_banks() {
	return PRGBanks.size();
}

const byte ROM::get_num_chr_banks() {
	return CHRBanks.size();
}

bool ROM::is_iNES(std::basic_ifstream<byte> &src) {
	byte	a = src.get(),
		b = src.get(),
		c = src.get(),
		d = src.get();

	return (a == 0x4E) && (b == 0x45) && (c == 0x53) && (d = 0x1A); //'N', 'E', 'S', <magic number>
}

