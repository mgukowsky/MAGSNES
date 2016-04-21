#include "System.h"

#define HALF_SECOND				500
#define QUARTER_SECOND		250
#define EIGHTH_SECOND			125

#define SAVE_STATE_DIRECTORY		"./states/"

using namespace MAGSNES;

//Various throttles for System clock rate
//static const dword	HALF_CPU_CLOCK_SPEED = CPU_CLOCK_SPEED / 2,
//QUARTER_CPU_CLOCK_SPEED = CPU_CLOCK_SPEED / 4,
//EIGHTH_CPU_CLOCK_SPEED = CPU_CLOCK_SPEED / 8;

System::System(GLManager *pGLM)
	: sysCore(Core::get_sys_core()),
	pBus(new Bus),
	pCPU(new CPU(this->pBus)),
	pPPU(new PPU(this->pBus, this->pCPU, this->sysCore, pGLM)),
	pAPU(new APU(this->pCPU, this->pBus)),
	pController(new Controller(this->pBus)),
	currentROM(nullptr),
	currentMapper(nullptr),
	isRunning(false) {

	pCPU->connect_to_ppu(pPPU->expose_ppudatabuffer(), pPPU->expose_ppupalettebaseaddr(), pPPU->expose_ppuaddr());
}

System::~System() {
	if (currentMapper != nullptr) { delete currentMapper; }
	if (currentROM != nullptr) { delete currentROM; }

	delete pController;
	delete pAPU;
	delete pPPU;
	delete pCPU;
	delete pBus;
}

void System::loadROM(const char * const path) {
	currentROM = new ROM(path);

	MAGSNES::byte mapperID = currentROM->get_mapper_id();

	//Selects a mapper based on iNES mapper ID. By initializing the mapper, RAM will be 
	//initialized, and the emulator will ready to run.
	switch (mapperID) {
	case 0:
		currentMapper = new NROM(currentROM, pCPU, pPPU, pBus);
		break;

	case 1:
		currentMapper = new MMC1(currentROM, pCPU, pPPU, pBus);
		break;

	case 2:
		currentMapper = new UNROM(currentROM, pCPU, pPPU, pBus);
		break;

	case 3:
		currentMapper = new CNROM(currentROM, pCPU, pPPU, pBus);
		break;

	case 4:
		currentMapper = new MMC3(currentROM, pCPU, pPPU, pBus);
		break;

	default:
		sysCore.alert_error("Unimplemented or invalid mapper requested");
	}

	//Load the mirroring type into the PPU
	pPPU->loadMirroringType(currentROM->get_mirroring_type());

	/*std::string loadMsg("Opened ROM: ");
	loadMsg.append(path);
	pCore->post_message(const_cast<char *>(loadMsg.c_str()));*/

	pCPU->initialize_PC();

}

//Do one CPU instruction, and 3 PPU cycles for each cycle the CPU takes. Returns the number of CPU cycles.
const MAGSNES::byte System::step() {
	//Execute next instruction, then do 3 PPU cycles for every cycle the CPU took
	MAGSNES::byte ppuCycles = pCPU->execute_next();

	//APU knows to do one cycle per CPU cycle (real NES does one APU every 2 CPU cycles, but APU#tick accounts for that)
	pAPU->tick(ppuCycles);

	//Check for R/W to registers before invoking the PPU
	pController->tick();
	currentMapper->monitor();
	//Only have the PPU check on the first tick of the group
	pPPU->tick(true);
	const MAGSNES::byte PPU_LIMIT = ppuCycles * 3;
	for (MAGSNES::byte ppuCounter = 1; ppuCounter < PPU_LIMIT; ppuCounter++) {
		pPPU->tick(false);
	}

	return ppuCycles;
}

void System::save_state() {

	//To start, we create a basic hash of the last 20 (or fewer) characters

	/*std::string namehash = "";
	const std::string &romname = currentROM->ROM_NAME;
	char buf[2];


	for (std::string::const_iterator i = romname.end(), int j = 0; (i > romname.begin() || j < 20); i--, j++) {
	_itoa_s(*i)
	itoa(tmp, buf, 16);

	namehash += (itoa(tmp, buf, 16));
	}
	char *pcstr = currentROM->ROM_NAME.c_str;
	const dword LIMIT = sizeof(currentROM->ROM_NAME.length());
	std::string statepath = SAVE_STATE_DIRECTORY;
	statepath.append(currentROM->ROM_NAME);

	std::basic_ifstream<byte> raw;
	raw.open(currentROM->ROM_NAME, std::basic_ifstream<byte>::out | std::basic_ifstream<byte>::binary);*/

}

void System::load_state() {

}