#include "MMC3.h"

#define byte				MAGSNES::byte

using namespace			MAGSNES;

MMC3::MMC3(ROM *pROM, CPU *pCPU, PPU *pPPU, Bus *pBus)
	: Mapper(pROM, pCPU, pPPU, pBus),
		IRQCounter(0), IRQReloadValue(0), 
		isIRQEnabled(false), shouldInvertCHRBanks(false) {


}

MMC3::~MMC3()	{}

void MMC3::monitor() {

}

void MMC3::initialize_memory() {
	//Can't find any info about power on/reset, so simply using the first banks from the ROM.



}