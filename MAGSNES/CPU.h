#pragma once

#include "Bus.h"
#include "Core.h"

namespace MAGSNES {

class CPU {

	friend class Mapper;
	friend class PPU;
	DECLARE_DEBUGGER_ACCESS

public:

	CPU(Bus *refBus);

	void total_reset();
	void post_interrupt(const byte INTERRUPT_TYPE);

	//Initializes CPU PC register to reset vector. Must be called after loading banks into main memory.
	void initialize_PC();

	byte execute_next();
	byte execute(const byte opcode);

	//Allow CPU to access to access parts of the PPU, since we can't pass a direct reference to it.
	void connect_to_ppu(byte *pdata, byte *ppalettes, word *paddr) {
		pPPUDATAbuff = pdata;
		pPPUPALETTES = ppalettes;
		pPPUADDR = paddr;
	}

	//Some symbols we need are defined in Windows.h

#ifdef ABSOLUTE
#undef ABSOLUTE
#endif
#ifdef RELATIVE
#undef RELATIVE
#endif

	enum {
		ADDR_MODE_NONE,
		ACCUMULATOR,
		IMMEDIATE,
		ZERO_PAGE,
		ZERO_PAGE_X,
		ZERO_PAGE_Y,
		ABSOLUTE,
		ABSOLUTE_X,
		ABSOLUTE_Y,
		IMPLIED,
		RELATIVE,
		INDIRECT_X,
		INDIRECT_Y,
		ABSOLUTE_INDIRECT,
		ADDR_MODE_TOTAL
	};

	enum {
		INTERRUPT_NONE,
		INTERRUPT_IRQ,
		INTERRUPT_NMI,
		INTERRUPT_RESET,
		INTERRUPT_DMA,
		INTERRUPT_TOTAL
	};

	//Callbacks used to execute the CPU instruction; returns num cycles taken
	typedef byte(*OpCallback)(word, byte, CPU&);

private:

	/*	Instance variables */
	byte regA, regX, regY, regP, regSP, regInterrupt, regExtraCycles, regPageCross;
	bool flagN, flagV, flagTrash, flagB, flagD, flagI, flagZ, flagC;
	word regPC, DMACounter, DMAAddress, &refReadBus, &refWriteBus;
	Bus &refBus;
	Core &sysCore;
	byte(&refMM)[MM_SIZE];

	//Allows the CPU to immediately access the value in the PPUDATA buffer while avoiding cross references
	byte *pPPUDATAbuff, *pPPUPALETTES; //pPPUPALETTES is a pointer to VRAM $3F00, which allows us to instantly retrieve a palette upon a PPUDATA read
	word *pPPUADDR;

	//Contains information about each opcode
	struct OpInfo {
		//The instruction callback
		CPU::OpCallback instr;
		//Addressing mode to get operand for the instruction
		byte addrMode;
	};

	static const OpInfo opcodeVector[0x100];

	//Implements mirroring in main memory
	FORCEINLINE const word coerce_address(const word address) {
		//Implements mirroring of $0000 to $07FF at $0800 to $0FFF, 
		//$1000 to $17FF, and $18FF to $1FFF.
		if ((address > 0x7FF) && (address < 0x2000)) {
			return address & 0x7FF; //Get last 11 bits (mod 0x800)

			//Implements mirroring of $2000 to $2007 every 8 bytes until $4000
		} else if ((address > 0x2007) && (address < 0x4000)) {
			word offset = address & 0x07; //Get last 3 bits (mod 8)
			return offset + 0x2000;

		} else {
			return address;
		}
	}

	//Note that stack operations do not always manipulate memory; they only move the
	//stack pointer except for the byte that is being pushed.
	void push_byte(const byte val);

	byte pop_byte();

	FORCEINLINE byte flagsToP() {
		return	(flagC) |
			(flagZ << 1) |
			(flagI << 2) |
			(flagD << 3) |
			(flagB << 4) |
			0x20 | //A;ways have flagTrash on
			(flagV << 6) |
			(flagN << 7);
	}

	FORCEINLINE void pToFlags(byte val) {
		flagC = (val & 0x1) ? true : false;
		flagZ = (val & 0x2) ? true : false;
		flagI = (val & 0x4) ? true : false;
		flagD = (val & 0x8) ? true : false;
		flagB = false; //Bit 4 is IGNORED when restoring flags from the stack
		flagTrash = true; //Bit 5 is always on
		flagV = (val & 0x40) ? true : false;
		flagN = (val & 0x80) ? true : false;
	}

	//Functions to retrieve the desired operand from memory. RESPONSIBLE FOR
	//INCREMENTING PC. Returns the formatted operand.

	//Operand is the accumulator (A) register.
	//This usually means the operation will change the value of
	//regA.
	FORCEINLINE word accumulatorOperand() {
		regPC += 1;
		return regA;
	}

	//Operand is the byte after the instruction
	FORCEINLINE word immediateOperand() {
		word addr = regPC + 1;
		regPC += 2; //Reads operation, then operand
		return addr;
	}

	//Operand is the byte after the instruction, coerces to the 
	//range -128 to +127
	FORCEINLINE word relativeOperand() {
		word addr = regPC + 1;
		regPC += 2; //Reads operation, then operand
		return addr;
	}

	//Basically a placeholder, as implied addressing means no
	//operands are needed
	FORCEINLINE word impliedOperand() {
		regPC += 1;
		return 0;
	}

	//Get a byte in range $0000 to $00FF
	FORCEINLINE word zeroPageOperand() {
		word addr = refMM[regPC + 1];
		regPC += 2; //Reads operation, then operand address
		return addr;
	}

	//Add X register to immediate operand to get a zero page address ($0000 - $00FF).
	//This means that the final address MUST BE WRAPPED past 0xFF before it is read from!
	FORCEINLINE word zeroPageIndexedXOperand() {
		word memAddr = (refMM[regPC + 1]) + regX;
		memAddr = memAddr & 0xFF;
		regPC += 2;
		return memAddr;
	}

	//Same as zero page indexed X, but w/ regY
	FORCEINLINE word zeroPageIndexedYOperand() {
		word memAddr = (refMM[regPC + 1]) + regY;
		memAddr = memAddr & 0xFF;
		regPC += 2;
		return memAddr;
	}

	//The next two bytes in memory form a ('lil endian) word, which is the address of
	//a byte in main memory ($0000 to $FFFF);

	//TODO: should the absolute bytes that form memaddr wrap around the page?
	FORCEINLINE word absoluteOperand() {
		word pc = regPC;
		word memAddr = refMM[pc + 1] | (refMM[pc + 2] << 8);
		regPC += 3;
		return memAddr;
	}

	//These two take the next two bytes in memory to form a word, then add the value of
	//the X or Y register to form the desired memory address. These two functions usually
	//require an extra machine cycle if adding the register to the initial memory address
	//crosses over to a different page.
	FORCEINLINE word absoluteIndexedXOperand() {
		word pc = regPC;
		word baseAddr = refMM[pc + 1] | (refMM[pc + 2] << 8);
		word memAddr = baseAddr + regX;

		regPageCross = ((baseAddr & 0xFF00) != (memAddr & 0xFF00)) ? 1 : 0;

		regPC += 3;
		return memAddr;
	}

	//Same as absoluteIndexedXOperand(), but with Y register
	FORCEINLINE word absoluteIndexedYOperand() {
		word pc = regPC;
		word baseAddr = refMM[pc + 1] | (refMM[pc + 2] << 8);
		word memAddr = baseAddr + regY;

		regPageCross = ((baseAddr & 0xFF00) != (memAddr & 0xFF00)) ? 1 : 0;

		regPC += 3;
		return memAddr;
	}

	//This mode simply takes the next word in memory as the address of the 
	//operand, which is in this case a 16 bit address for JMP
	FORCEINLINE word absoluteIndirectOperand() {
		word pc = regPC;
		word baseAddr = refMM[pc + 1] | (refMM[pc + 2] << 8);

		//Hi byte of fetched operand needs to wrap around the page
		word baseAddrHi = baseAddr & 0xFF00;
		byte baseAddrLo = baseAddr;
		baseAddrLo++;
		word memAddr = refMM[baseAddr] | (refMM[(baseAddrHi | baseAddrLo)] << 8);
		regPC += 3;

		return memAddr;
	}

	//This mode, also known as pre-indexed indirect addressing, first takes a zero page 
	//address as the immediate operand, adds the X register to it (with wraparound),
	//and uses that calculated address as the address of a word to read from memory, 
	//which will be the absolute address of the final operand. Always takes 6 cycles.
	FORCEINLINE word indirectIndexedXOperand() {
		word indirectAddr = refMM[regPC + 1] + regX;
		indirectAddr = indirectAddr & 0xFF;
		word indirectAddrHi = (indirectAddr + 1) & 0xFF;
		word memAddr = refMM[indirectAddr] | (refMM[indirectAddrHi] << 8);
		regPC += 2;
		return memAddr;
	}

	//This mode, also known as post-indexed indirect addressing, first takes a zero page
	//address as an immediate operand, and reads a word from that zero page address.
	//That word plus the value of the Y register gives the absolute address of
	//the final operand. Usually requires an extra cycle if a page cross occurs when
	//adding the Y register.
	FORCEINLINE word indirectIndexedYOperand() {
		word indirectAddr = refMM[regPC + 1];
		word indirectAddrHi = (indirectAddr + 1) & 0xFF;

		word baseAddrLo = refMM[indirectAddr];
		word baseAddrHi = refMM[indirectAddrHi];
		word baseAddr = baseAddrLo + (baseAddrHi << 8);

		word memAddr = baseAddr + regY;

		regPageCross = ((baseAddr & 0xFF00) != (memAddr & 0xFF00)) ? 1 : 0;

		regPC += 2;
		return memAddr;
	}

	FORCEINLINE byte read_byte(const word addr) {
		refBus.readBus = addr;

		//Note that PPU handles the increment of ppuAddr, and putting the VRAM data into the data buffer
		if (addr == 0x2007) {	//Must handle PPUDATA read as a special case, b/c the CPU expects to immediately receive the value of the PPU data buffer

			// Return a palette immediately
			if (*pPPUADDR >= 0x3F00) {
				word tmpAddr = coercePPUAddress(*pPPUADDR); //We need to coerce the address to account for PPU mirroring
				byte offset = *pPPUADDR & 0x1F;	//Get offset into the palette RAM
				return *(pPPUPALETTES + offset);

			} else {
				return *pPPUDATAbuff;
			}

		} else {
			return refMM[addr]; //addr will have been coerced by the time this function is called
		}
	}

	//Helper function, copy of PPU::coercePPUAddress(word)
	FORCEINLINE word coercePPUAddress(word addr) {
		//Mirror $0 to $3FFF
		addr &= 0x3FFF; //Mask to 14 bits; same as addr %= 4000

										//Mirror $2000 to $2EFF
		if (addr > 0x2FFF && addr < 0x3F00) {
			return addr - 0x1000;

		} else if (addr > 0x3EFF && addr < 0x4000) {
			addr = (addr & 0x1F) + 0x3F00; //Don't return yet; this area is mirrored even further...

																			//These 4 addresses are mirrors of the address 0x10 bytes lower
			switch (addr) {
			case 0x3F10:
				return 0x3F00;
				break;

			case 0x3F14:
				return 0x3F04;
				break;

			case 0x3F18:
				return 0x3F08;
				break;

			case 0x3F1C:
				return 0x3F0C;
				break;

			default:
				return addr;
				break;
			}
		} else {
			return addr;
		}
	}

	FORCEINLINE void write_byte(const word addr, const byte val) {
		refBus.writeBus = addr;

		//We want PRG_ROM to be immutable, so we put the data that would have been written to the address on the bus
		if (addr & 0x8000) {
			refBus.writeData = val;
		} else {
			if (addr != 0x2002) { //PPUSTATUS is read-only
				refMM[addr] = val;
			}
		}
	}

	const byte handle_interrupt();
	const byte execute_DMA_step();

	void start_DMA(const word startAddr);

	/*
	The functions which encapsulate instruction logic. Each instruction returns the
	number of cycles taken, based on the given addressing mode. Each of these functions
	can be pointed to by an OpCallback pointer.

	Make these static to enforce statelessness. This will also make it easy to
	point to the function by enforcing only one instance of each function; moreover,
	function pointers can ONLY point to static members! We pass in the context so that the
	static function can access members (public AND private, b/c the functions are still private
	members themselves) of the specific CPU instance. The static state
	also allows the functions to be called dynamically by a CPU instance.
	*/
	static byte ADC(word, byte, CPU&);
	static byte AND(word, byte, CPU&);
	static byte ASL(word, byte, CPU&);
	static byte BCC(word, byte, CPU&);
	static byte BCS(word, byte, CPU&);
	static byte BEQ(word, byte, CPU&);
	static byte BIT(word, byte, CPU&);
	static byte BMI(word, byte, CPU&);
	static byte BNE(word, byte, CPU&);
	static byte BPL(word, byte, CPU&);
	static byte BRK(word, byte, CPU&);
	static byte BVC(word, byte, CPU&);
	static byte BVS(word, byte, CPU&);
	static byte CLC(word, byte, CPU&);
	static byte CLD(word, byte, CPU&);
	static byte CLI(word, byte, CPU&);
	static byte CLV(word, byte, CPU&);
	static byte CMP(word, byte, CPU&);
	static byte CPX(word, byte, CPU&);
	static byte CPY(word, byte, CPU&);
	static byte DEC(word, byte, CPU&);
	static byte DEX(word, byte, CPU&);
	static byte DEY(word, byte, CPU&);
	static byte EOR(word, byte, CPU&);
	static byte ERR(word, byte, CPU&); //NOT a real opcode; meant to throw an error when given an illegal opcode.
	static byte INC(word, byte, CPU&);
	static byte INX(word, byte, CPU&);
	static byte INY(word, byte, CPU&);
	static byte JMP(word, byte, CPU&);
	static byte JSR(word, byte, CPU&);
	static byte LDA(word, byte, CPU&);
	static byte LDX(word, byte, CPU&);
	static byte LDY(word, byte, CPU&);
	static byte LSR(word, byte, CPU&);
	static byte NOP(word, byte, CPU&);
	static byte ORA(word, byte, CPU&);
	static byte PHA(word, byte, CPU&);
	static byte PHP(word, byte, CPU&);
	static byte PLA(word, byte, CPU&);
	static byte PLP(word, byte, CPU&);
	static byte ROL(word, byte, CPU&);
	static byte ROR(word, byte, CPU&);
	static byte RTI(word, byte, CPU&);
	static byte RTS(word, byte, CPU&);
	static byte SBC(word, byte, CPU&);
	static byte SEC(word, byte, CPU&);
	static byte SED(word, byte, CPU&);
	static byte SEI(word, byte, CPU&);
	static byte STA(word, byte, CPU&);
	static byte STX(word, byte, CPU&);
	static byte STY(word, byte, CPU&);
	static byte TAX(word, byte, CPU&);
	static byte TAY(word, byte, CPU&);
	static byte TSX(word, byte, CPU&);
	static byte TXA(word, byte, CPU&);
	static byte TXS(word, byte, CPU&);
	static byte TYA(word, byte, CPU&);
};

} /* namespace NESPP */