#include "CPU.h"

__FILESCOPE__{
	//Interrupt vectors and magic numbers
const int VECTOR_NMI = 0xFFFA;
const int VECTOR_RESET = 0xFFFC;
const int VECTOR_IRQ = 0xFFFE;

const int STACK_OFFSET = 0x100;
const int DMA_LIMIT = 256;
}

using namespace MAGSNES; //Windows.h includes a typedef in one of its headers for 'byte', so we need to scope it within this file

CPU::CPU(Bus *refBus)
	: refBus(*refBus),
		refMM(this->refBus.mainMemory),
		refReadBus(this->refBus.readBus),
		refWriteBus(this->refBus.writeBus),
		sysCore(Core::get_sys_core()) {

	total_reset();
}

void CPU::total_reset() {
	regA = 0;
	regX = 0;
	regY = 0;
	regP = 0x20; //Flag T must always be on, although regP isn't used in this implementation
	regSP = 0xFF;
	regPC = 0;
	regInterrupt = INTERRUPT_NONE;
	regExtraCycles = 0;
	regPageCross = 0;
	DMACounter = 0;
	DMAAddress = 0;
	refBus.reset();

	//Store the flags separately for efficiency
	flagN = false;
	flagV = false;
	flagTrash = true; //Bit 5 in 6502 P register. Not used, but should always be on.
	flagB = false;
	flagD = false;
	flagI = false;
	flagZ = false;
	flagC = false;
}

void CPU::post_interrupt(const MAGSNES::byte INTERRUPT_TYPE) {
	//Ignore IRQ if flag I is set
	if ((INTERRUPT_TYPE == INTERRUPT_IRQ) && flagI) {
		return;
	}

	regInterrupt = INTERRUPT_TYPE;
}

void CPU::initialize_PC() {
	regPC = refMM[VECTOR_RESET] | (refMM[VECTOR_RESET + 1] << 8);
}

//Interrupt and cycle handling takes place here
MAGSNES::byte CPU::execute_next() {
	MAGSNES::byte extraCycles = 0;
	if (regInterrupt) { //anything but zero means an interrupt
		extraCycles = handle_interrupt();
		//Block CPU from executing on DMA
		if (regInterrupt == INTERRUPT_DMA) {
			return extraCycles;
		}
	}
	//Execute the opcode at PC (note that PC will have likely been changed if an interrupt
	//took place)
	MAGSNES::byte opcode = refMM[regPC];
	return execute(opcode) + extraCycles;
}

//Decodes an opcode into an ALU operation, addressing mode, and cycle group, then executes it.
//Returns the # of cycles taken.
MAGSNES::byte CPU::execute(const MAGSNES::byte opcode) {
	OpInfo	opinfo = opcodeVector[opcode];
	byte		addrMode = opinfo.addrMode, extraCycles = 0;
	word		operand;

	//Note that an extra cycle is added for page crosses on certain opcodes in addressing
	//modes ABSOLUTE_X, ABSOLUTE_Y, and INDIRECT_Y
	switch (addrMode) {
	case ACCUMULATOR:
		operand = accumulatorOperand();
		break;

	case IMMEDIATE:
		operand = immediateOperand();
		break;

	case IMPLIED:
		operand = impliedOperand();
		break;

	case ZERO_PAGE:
		operand = zeroPageOperand();
		break;

	case ABSOLUTE:
		operand = absoluteOperand();
		operand = coerce_address(operand);
		break;

	case RELATIVE:
		operand = relativeOperand();
		break;

	case ZERO_PAGE_X:
		operand = zeroPageIndexedXOperand();
		break;

	case ABSOLUTE_X:
		operand = absoluteIndexedXOperand();
		/*if (regExtraCycles && pageCrossOpcodes[opcode]) {
		extraCycles = regExtraCycles;
		}*/
		operand = coerce_address(operand);
		break;

	case ABSOLUTE_Y:
		operand = absoluteIndexedYOperand();
		/*if (regExtraCycles && pageCrossOpcodes[opcode]) {
		extraCycles = regExtraCycles;
		}*/
		operand = coerce_address(operand);
		break;

	case INDIRECT_X:
		operand = indirectIndexedXOperand();
		operand = coerce_address(operand);
		break;

	case INDIRECT_Y:
		operand = indirectIndexedYOperand();
		/*if (regExtraCycles && pageCrossOpcodes[opcode]) {
		extraCycles = regExtraCycles;
		}*/
		operand = coerce_address(operand);
		break;

	case ZERO_PAGE_Y:
		operand = zeroPageIndexedYOperand();
		break;

	case ABSOLUTE_INDIRECT:
		operand = absoluteIndirectOperand();
		operand = coerce_address(operand);
		break;

	default:
		sysCore.alert_error("Invalid addressing mode detected! This means that either your ROM may be corrupted or the mapper for the ROM may not be implemented. Closing ROM...");
		break;
	}

	OpCallback tmpProc = opinfo.instr;
	MAGSNES::byte cyclesTaken = tmpProc(operand, addrMode, *this);
	return cyclesTaken + extraCycles;
}

const MAGSNES::byte CPU::handle_interrupt() {
	if (regInterrupt == INTERRUPT_NMI) {

		push_byte(regPC >> 8);
		push_byte(regPC); //regPC will be coerced to a byte here
		push_byte(flagsToP());

		flagI = true;
		regPC = refMM[VECTOR_NMI] | (refMM[VECTOR_NMI + 1] << 8);

	} else if (regInterrupt == INTERRUPT_DMA) {
		//Exit prematurely if in DMA
		return execute_DMA_step();

	} else if (regInterrupt == INTERRUPT_IRQ) {

		push_byte(regPC >> 8);
		push_byte(regPC); //regPC will be coerced to a MAGSNES::byte here
		push_byte(flagsToP() | 0x10); //Set flagB in the version of the flags we push (as per CPU manual)

		flagI = true;
		regPC = refMM[VECTOR_IRQ] | (refMM[VECTOR_IRQ + 1] << 8);

	} else { //reset
		regPC = refMM[VECTOR_RESET] | (refMM[VECTOR_RESET + 1] << 8);
	}

	regInterrupt = INTERRUPT_NONE;
	return 7; //Attending to an interrupt takes an extra 7 cycles
}

const MAGSNES::byte CPU::execute_DMA_step() {
	if (DMACounter >= 256) {
		post_interrupt(INTERRUPT_NONE);
		//Only scenario where 1 cycle is returned by a call to executeNext
		return 1;
	}

	MAGSNES::byte dataToSend = refMM[DMAAddress];
	refBus.OAM[DMACounter] = dataToSend;

	DMAAddress++;
	DMACounter++;

	return 2;
}

void CPU::start_DMA(const word startAddr) {
	DMAAddress = startAddr;
	//Exact number of cycles varies by source, but most agree that this many cycles 
	//are needed. Will take 512 + 1 cycles (the check for >256 in executeDMAStep)
	DMACounter = 0;
	post_interrupt(INTERRUPT_DMA);
}


FORCEINLINE void CPU::push_byte(const MAGSNES::byte val) {
	refMM[regSP + STACK_OFFSET] = val;
	regSP--;
}

FORCEINLINE MAGSNES::byte CPU::pop_byte() {
	regSP++;
	return refMM[regSP + STACK_OFFSET];
}

//Each opcode serves as an index into this array. Illegal opcodes
//point to ERR as their callback, which will raise an exception.
const CPU::OpInfo CPU::opcodeVector[0x100] = {

	/* 0x00 */{ BRK, IMPLIED },					{ ORA, INDIRECT_X },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x04 */{ ERR, 0 },								{ ORA, ZERO_PAGE },						{ ASL, ZERO_PAGE },					{ ERR, 0 },
	/* 0x08 */{ PHP, IMPLIED },					{ ORA, IMMEDIATE },						{ ASL, ACCUMULATOR },				{ ERR, 0 },
	/* 0x0C */{ ERR, 0 },								{ ORA, ABSOLUTE },						{ ASL, ABSOLUTE },					{ ERR, 0 },

	/* 0x10 */{ BPL, RELATIVE },				{ ORA, INDIRECT_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x14 */{ ERR, 0 },								{ ORA, ZERO_PAGE_X },					{ ASL, ZERO_PAGE_X },				{ ERR, 0 },
	/* 0x18 */{ CLC, IMPLIED },					{ ORA, ABSOLUTE_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x1C */{ ERR, 0 },								{ ORA, ABSOLUTE_X },					{ ASL, ABSOLUTE_X },				{ ERR, 0 },

	/* 0x20 */{ JSR, ABSOLUTE },				{ AND, INDIRECT_X },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x24 */{ BIT, ZERO_PAGE },				{ AND, ZERO_PAGE },						{ ROL, ZERO_PAGE },					{ ERR, 0 },
	/* 0x28 */{ PLP, IMPLIED },					{ AND, IMMEDIATE },						{ ROL, ACCUMULATOR },				{ ERR, 0 },
	/* 0x2C */{ BIT, ABSOLUTE },				{ AND, ABSOLUTE },						{ ROL, ABSOLUTE },					{ ERR, 0 },

	/* 0x30 */{ BMI, RELATIVE },				{ AND, INDIRECT_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x34 */{ ERR, 0 },								{ AND, ZERO_PAGE_X },					{ ROL, ZERO_PAGE_X },				{ ERR, 0 },
	/* 0x38 */{ SEC, IMPLIED },         { AND, ABSOLUTE_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x3C */{ ERR, 0 },								{ AND, ABSOLUTE_X },					{ ROL, ABSOLUTE_X },				{ ERR, 0 },

	/* 0x40 */{ RTI, IMPLIED },					{ EOR, INDIRECT_X },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x44 */{ ERR, 0 },								{ EOR, ZERO_PAGE },						{ LSR, ZERO_PAGE },					{ ERR, 0 },
	/* 0x48 */{ PHA, IMPLIED },					{ EOR, IMMEDIATE },						{ LSR, ACCUMULATOR },				{ ERR, 0 },
	/* 0x4C */{ JMP, ABSOLUTE },				{ EOR, ABSOLUTE },						{ LSR, ABSOLUTE },					{ ERR, 0 },

	/* 0x50 */{ BVC, RELATIVE },				{ EOR, INDIRECT_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x54 */{ ERR, 0 },								{ EOR, ZERO_PAGE_X },					{ LSR, ZERO_PAGE_X },				{ ERR, 0 },
	/* 0x58 */{ CLI, IMPLIED },					{ EOR, ABSOLUTE_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x5C */{ ERR, 0 },								{ EOR, ABSOLUTE_X },					{ LSR, ABSOLUTE_X },				{ ERR, 0 },

	/* 0x60 */{ RTS, IMPLIED },					{ ADC, INDIRECT_X },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x64 */{ ERR, 0 },								{ ADC, ZERO_PAGE },						{ ROR, ZERO_PAGE },					{ ERR, 0 },
	/* 0x68 */{ PLA, IMPLIED },					{ ADC, IMMEDIATE },						{ ROR, ACCUMULATOR },				{ ERR, 0 },
	/* 0x6C */{ JMP, ABSOLUTE_INDIRECT },{ ADC, ABSOLUTE },						{ ROR, ABSOLUTE },					{ ERR, 0 },

	/* 0x70 */{ BVS, RELATIVE },				{ ADC, INDIRECT_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x74 */{ ERR, 0 },								{ ADC, ZERO_PAGE_X },					{ ROR, ZERO_PAGE_X },				{ ERR, 0 },
	/* 0x78 */{ SEI, IMPLIED },					{ ADC, ABSOLUTE_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x7C */{ ERR, 0 },								{ ADC, ABSOLUTE_X },					{ ROR, ABSOLUTE_X },				{ ERR, 0 },

	/* 0x80 */{ ERR, 0 },								{ STA, INDIRECT_X },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x84 */{ STY, ZERO_PAGE },				{ STA, ZERO_PAGE },						{ STX, ZERO_PAGE },					{ ERR, 0 },
	/* 0x88 */{ DEY, IMPLIED },					{ ERR, 0 },										{ TXA, IMPLIED },						{ ERR, 0 },
	/* 0x8C */{ STY, ABSOLUTE },				{ STA, ABSOLUTE },						{ STX, ABSOLUTE },					{ ERR, 0 },

	/* 0x90 */{ BCC, RELATIVE },				{ STA, INDIRECT_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0x94 */{ STY, ZERO_PAGE_X },			{ STA, ZERO_PAGE_X },					{ STX, ZERO_PAGE_Y },				{ ERR, 0 },
	/* 0x98 */{ TYA, IMPLIED },					{ STA, ABSOLUTE_Y },					{ TXS, IMPLIED },						{ ERR, 0 },
	/* 0x9C */{ ERR, 0 },								{ STA, ABSOLUTE_X },					{ ERR, 0 },									{ ERR, 0 },

	/* 0xA0 */{ LDY, IMMEDIATE },				{ LDA, INDIRECT_X },					{ LDX, IMMEDIATE },					{ ERR, 0 },
	/* 0xA4 */{ LDY, ZERO_PAGE },				{ LDA, ZERO_PAGE },						{ LDX, ZERO_PAGE },					{ ERR, 0 },
	/* 0xA8 */{ TAY, IMPLIED },					{ LDA, IMMEDIATE },						{ TAX, IMPLIED },						{ ERR, 0 },
	/* 0xAC */{ LDY, ABSOLUTE },				{ LDA, ABSOLUTE },						{ LDX, ABSOLUTE },					{ ERR, 0 },

	/* 0xB0 */{ BCS, RELATIVE },				{ LDA, INDIRECT_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0xB4 */{ LDY, ZERO_PAGE_X },			{ LDA, ZERO_PAGE_X },					{ LDX, ZERO_PAGE_Y },				{ ERR, 0 },
	/* 0xB8 */{ CLV, IMPLIED },					{ LDA, ABSOLUTE_Y },					{ TSX, IMPLIED },						{ ERR, 0 },
	/* 0xBC */{ LDY, ABSOLUTE_X },			{ LDA, ABSOLUTE_X },					{ LDX, ABSOLUTE_Y },				{ ERR, 0 },

	/* 0xC0 */{ CPY, IMMEDIATE },				{ CMP, INDIRECT_X },					{ ERR, 0 },									{ ERR, 0 },
	/* 0xC4 */{ CPY, ZERO_PAGE },				{ CMP, ZERO_PAGE },						{ DEC, ZERO_PAGE },					{ ERR, 0 },
	/* 0xC8 */{ INY, IMPLIED },					{ CMP, IMMEDIATE },						{ DEX, IMPLIED },						{ ERR, 0 },
	/* 0xCC */{ CPY, ABSOLUTE },				{ CMP, ABSOLUTE },						{ DEC, ABSOLUTE },					{ ERR, 0 },

	/* 0xD0 */{ BNE, RELATIVE },				{ CMP, INDIRECT_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0xD4 */{ ERR, 0 },								{ CMP, ZERO_PAGE_X },					{ DEC, ZERO_PAGE_X },				{ ERR, 0 },
	/* 0xD8 */{ CLD, IMPLIED },					{ CMP, ABSOLUTE_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0xDC */{ ERR, 0 },								{ CMP, ABSOLUTE_X },					{ DEC, ABSOLUTE_X },				{ ERR, 0 },

	/* 0xE0 */{ CPX, IMMEDIATE },				{ SBC, INDIRECT_X },					{ ERR, 0 },									{ ERR, 0 },
	/* 0xE4 */{ CPX, ZERO_PAGE },				{ SBC, ZERO_PAGE },						{ INC, ZERO_PAGE },					{ ERR, 0 },
	/* 0xE8 */{ INX, IMPLIED },					{ SBC, IMMEDIATE },						{ NOP, IMPLIED },						{ ERR, 0 },
	/* 0xEC */{ CPX, ABSOLUTE },				{ SBC, ABSOLUTE },						{ INC, ABSOLUTE },					{ ERR, 0 },

	/* 0xF0 */{ BEQ, RELATIVE },				{ SBC, INDIRECT_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0xF4 */{ ERR, 0 },								{ SBC, ZERO_PAGE_X },					{ INC, ZERO_PAGE_X },				{ ERR, 0 },
	/* 0xF8 */{ SED, IMPLIED },					{ SBC, ABSOLUTE_Y },					{ ERR, 0 },									{ ERR, 0 },
	/* 0xFC */{ ERR, 0 },								{ SBC, ABSOLUTE_X },					{ INC, ABSOLUTE_X },				{ ERR, 0 },

};

/*
These are the functions which encapsulate instruction logic. Each instruction returns the
number of cycles taken, based on the given addressing mode. Each of these functions
can be pointed to by an OpCallback pointer.

Note that these functions set refBus's readBus and writeBus appropriately.
*/

//Add memory and regA with carry
MAGSNES::byte CPU::ADC(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	word operand = context.read_byte(address);

	word result = operand + context.regA + context.flagC;

	context.flagC = (result > 0xFF) ? true : false;

	result &= 0xFF;


	//Set the overflow flag when we add two numbers (each < 128), but the result > 127;
	//checks if pos + pos = neg OR neg + neg = pos
	//For example, we would expect two positives to always sum to a positive, but the signed byte
	//may say otherwise (i.e. 64 + 65 = 129, but signed it is -127)
	context.flagV = (!((context.regA ^ operand) & 0x80) && ((context.regA ^ result) & 0x80)) ? true : false;
	context.flagZ = (result == 0) ? true : false;
	context.flagN = (result & 0x80) ? true : false;

	context.regA = result;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_X:
		return 4;
	case ABSOLUTE_X:
		return 4;
	case ABSOLUTE_Y:
		return 4;
	case INDIRECT_X:
		return 6;
	case INDIRECT_Y:
		return 5;
	default:
		return 3;
	}
}


//AND regA with memory
MAGSNES::byte CPU::AND(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	word operand = context.read_byte(address);

	context.regA &= operand;
	word result = context.regA;
	context.flagN = (result & 0x80) ? true : false;
	context.flagZ = (result == 0) ? true : false;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_X:
		return 4;
	case ZERO_PAGE_Y:
		return 4;
	case ABSOLUTE_X:
		return 4;
	case ABSOLUTE_Y:
		return 4;
	case INDIRECT_X:
		return 6;
	case INDIRECT_Y:
		return 5;
	default:
		return 3;
	}
}

//Shift memory or accumulator left by one bit.
//Can operate directly on memory.
//flagC = memory OR regA & 0x80; memory/regA <<= 1
MAGSNES::byte CPU::ASL(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	word operand;

	//Operate on regA (addr is the val of regA)
	if (ADDR_MODE == ACCUMULATOR) {
		//address is the value of regA
		operand = address << 1;
		context.regA = operand & 0xFF;
	} else {
		operand = context.read_byte(address);

		operand <<= 1;

		context.write_byte(address, operand);
	}

	context.flagN = (operand & 0x80) ? true : false;
	context.flagZ = ((operand & 0xFF) == 0) ? true : false;
	context.flagC = (operand > 0xFF) ? true : false;

	switch (ADDR_MODE) {
	case ACCUMULATOR:
		return 2;
	case ZERO_PAGE:
		return 5;
	case ABSOLUTE:
		return  6;
	case ZERO_PAGE_X:
		return 6;
	case ABSOLUTE_X:
		return 7;
	default:
		return 3;
	}
}

//Branch on flagC === false (Carry Clear)
MAGSNES::byte CPU::BCC(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte extraCycles = 0;

	word operand = context.refMM[address];
	//Convert to signed byte
	operand = (operand < 128) ? operand : operand - 256;

	if (!context.flagC) {
		word tmp = context.regPC + operand;
		if ((context.regPC & 0xFF00) != (tmp & 0xFF00)) {
			extraCycles = 2;
		} else {
			extraCycles = 1;
		}
		context.regPC = tmp;
	}

	return extraCycles + 2;
}

//Branch on flagC === true (Carry Set)
MAGSNES::byte CPU::BCS(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte extraCycles = 0;

	word operand = context.refMM[address];
	//Convert to signed byte
	operand = (operand < 128) ? operand : operand - 256;

	if (context.flagC) {
		word tmp = context.regPC + operand;
		if ((context.regPC & 0xFF00) != (tmp & 0xFF00)) {
			extraCycles = 2;
		} else {
			extraCycles = 1;
		}
		context.regPC = tmp;
	}

	return extraCycles + 2;
}

//Branch on flagZ === true (Equals Zero)
MAGSNES::byte CPU::BEQ(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte extraCycles = 0;

	word operand = context.refMM[address];
	//Convert to signed byte
	operand = (operand < 128) ? operand : operand - 256;

	if (context.flagZ) {
		word tmp = context.regPC + operand;
		if ((context.regPC & 0xFF00) != (tmp & 0xFF00)) {
			extraCycles = 2;
		} else {
			extraCycles = 1;
		}
		context.regPC = tmp;
	}

	return extraCycles + 2;
}

//Test bits in memory with regA
//set flagN if bit 7 is set in operand
//set flagV if bit 6 is set in operand
//set flagZ if regA & operand === 0
MAGSNES::byte CPU::BIT(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);

	MAGSNES::byte tmp = context.regA & operand;
	context.flagN = (operand & 0x80) ? true : false;
	context.flagV = (operand & 0x40) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	switch (ADDR_MODE) {
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	default:
		return 3;
	}
}

//Branch on flagN === true (result MInus)
MAGSNES::byte CPU::BMI(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte extraCycles = 0;

	word operand = context.refMM[address];
	//Convert to signed byte
	operand = (operand < 128) ? operand : operand - 256;

	if (context.flagN) {
		word tmp = context.regPC + operand;
		if ((context.regPC & 0xFF00) != (tmp & 0xFF00)) {
			extraCycles = 2;
		} else {
			extraCycles = 1;
		}
		context.regPC = tmp;
	}

	return extraCycles + 2;
}

//Branch on flagZ === false (Not Zero)
MAGSNES::byte CPU::BNE(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte extraCycles = 0;

	word operand = context.refMM[address];
	//Convert to signed byte
	operand = (operand < 128) ? operand : operand - 256;

	if (!context.flagZ) {
		word tmp = context.regPC + operand;
		if ((context.regPC & 0xFF00) != (tmp & 0xFF00)) {
			extraCycles = 2;
		} else {
			extraCycles = 1;
		}
		context.regPC = tmp;
	}

	return extraCycles + 2;
}

//Branch on flagN === false (result PLus)
MAGSNES::byte CPU::BPL(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte extraCycles = 0;

	word operand = context.refMM[address];
	//Convert to signed byte
	operand = (operand < 128) ? operand : operand - 256;

	if (!context.flagN) {
		word tmp = context.regPC + operand;
		if ((context.regPC & 0xFF00) != (tmp & 0xFF00)) {
			extraCycles = 2;
		} else {
			extraCycles = 1;
		}
		context.regPC = tmp;
	} else {
		int i = 0;
		i++;
	}

	return extraCycles + 2;
}

//Force an IRQ.
//Increments PC by 2 before it is pushed on the stack, 
//then pushes the flags onto the stack.
//Attends to the IRQ by putting the word at $FFFE into regPC.
//Sets flagI to show that we are attending to an IRQ.
MAGSNES::byte CPU::BRK(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	//Increment the PC we push to point past the current instruction, 
	//otherwise we would return to the same instruction. Also, 6502 has a 'bug'
	//where the return address skips over the MAGSNES::byte after the BRK instruction, 
	//which is why we increment PC by 1 when we push it. 
	word tmpPC = context.regPC + 1;
	context.refMM[context.regSP + 0x100] = (tmpPC & 0xFF00) >> 8;
	context.regSP = (context.regSP - 1) & 0xFF;
	context.refMM[context.regSP + 0x100] = tmpPC & 0xFF;
	context.regSP = (context.regSP - 1) & 0xFF;

	MAGSNES::byte tmp = context.flagsToP();
	tmp |= 0x10; //Set flagB in the version of the flags we push (as per CPU manual)
	context.refMM[context.regSP + 0x100] = tmp;
	context.regSP = (context.regSP - 1) & 0xFF;

	context.flagI = true;
	context.regPC = context.refMM[VECTOR_IRQ] | (context.refMM[VECTOR_IRQ + 1] << 8);

	return 7;
}

//Branch on flagV === false (oVerflow Clear)
MAGSNES::byte CPU::BVC(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte extraCycles = 0;

	word operand = context.refMM[address];
	//Convert to signed byte
	operand = (operand < 128) ? operand : operand - 256;

	if (!context.flagV) {
		word tmp = context.regPC + operand;
		if ((context.regPC & 0xFF00) != (tmp & 0xFF00)) {
			extraCycles = 2;
		} else {
			extraCycles = 1;
		}
		context.regPC = tmp;
	}

	return extraCycles + 2;
}

//Branch on flagV === true (oVerflow Set)
MAGSNES::byte CPU::BVS(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte extraCycles = 0;

	word operand = context.refMM[address];
	//Convert to signed byte
	operand = (operand < 128) ? operand : operand - 256;

	if (context.flagV) {
		word tmp = context.regPC + operand;
		if ((context.regPC & 0xFF00) != (tmp & 0xFF00)) {
			extraCycles = 2;
		} else {
			extraCycles = 1;
		}
		context.regPC = tmp;
	}

	return extraCycles + 2;
}

//CLear flagC
MAGSNES::byte CPU::CLC(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.flagC = false;
	return 2;
}

//CLear flagD
MAGSNES::byte CPU::CLD(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.flagD = false;
	return 2;
}

//CLear flagI
MAGSNES::byte CPU::CLI(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.flagI = false;
	return 2;
}

//CLear flagV
MAGSNES::byte CPU::CLV(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.flagV = false;
	return 2;
}

//Compares memory and regA
MAGSNES::byte CPU::CMP(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);

	MAGSNES::byte ra = context.regA;
	MAGSNES::byte tmp = (ra - operand);

	context.flagZ = (tmp == 0) ? true : false;
	context.flagN = (tmp & 0x80) ? true : false;
	context.flagC = (ra >= operand) ? true : false;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_X:
		return 4;
	case ABSOLUTE_X:
		return 4;
	case ABSOLUTE_Y:
		return 4;
	case INDIRECT_X:
		return 6;
	case INDIRECT_Y:
		return 5;
	default:
		return 3;
	}
}

//Compares memory and regX
MAGSNES::byte CPU::CPX(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);

	MAGSNES::byte rx = context.regX;
	MAGSNES::byte tmp = (rx - operand);

	context.flagZ = (tmp == 0) ? true : false;
	context.flagN = (tmp & 0x80) ? true : false;
	context.flagC = (rx >= operand) ? true : false;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	default:
		return 3;
	}
}

//Compares memory and regY
MAGSNES::byte CPU::CPY(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);

	MAGSNES::byte ry = context.regY;
	MAGSNES::byte tmp = (ry - operand) & 0xFF;

	context.flagZ = (tmp == 0) ? true : false;
	context.flagN = (tmp & 0x80) ? true : false;
	context.flagC = (ry >= operand) ? true : false;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	default:
		return 3;
	}
}

//Decrement a memory address by one
MAGSNES::byte CPU::DEC(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);
	operand -= 1;
	context.write_byte(address, operand);

	MAGSNES::byte tmp = operand;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	switch (ADDR_MODE) {
	case ZERO_PAGE:
		return 5;
	case ABSOLUTE:
		return 6;
	case ZERO_PAGE_X:
		return 6;
	case ABSOLUTE_X:
		return 7;
	default:
		return 3;
	}
}

//Decrement regX by one
MAGSNES::byte CPU::DEX(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.regX--;
	MAGSNES::byte tmp = context.regX;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	return 2;
}

//Decrement regY by one
MAGSNES::byte CPU::DEY(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.regY--;
	MAGSNES::byte tmp = context.regY;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	return 2;
}

//Exclusive OR (aka XOR) memory with regA, 
//store result in regA
//regA ^ operand -> regA
MAGSNES::byte CPU::EOR(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);

	MAGSNES::byte tmp = context.regA ^ operand;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	context.regA = tmp;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_X:
		return 4;
	case ABSOLUTE_X:
		return 4;
	case ABSOLUTE_Y:
		return 4;
	case INDIRECT_X:
		return 6;
	case INDIRECT_Y:
		return 5;
	default:
		return 3;
	}
}

//All invalid opcodes point to this callback. 
//Note that this is not an actual 6502 instruction.
MAGSNES::byte CPU::ERR(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.sysCore.alert_error("Invalid opcode detected! This most likely means that your ROM is invalid or corrupted. Closing ROM...");
	return 0;
}

//INCremement a memory address by 1
MAGSNES::byte CPU::INC(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);
	operand += 1;
	context.write_byte(address, operand);

	MAGSNES::byte tmp = operand;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	switch (ADDR_MODE) {
	case ZERO_PAGE:
		return 5;
	case ABSOLUTE:
		return 6;
	case ZERO_PAGE_X:
		return 6;
	case ABSOLUTE_X:
		return 7;
	default:
		return 3;
	}
}

//INcrement regX by 1
MAGSNES::byte CPU::INX(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.regX++;
	MAGSNES::byte tmp = context.regX;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	return 2;
}

//INcrement regY by 1
MAGSNES::byte CPU::INY(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.regY++;
	MAGSNES::byte tmp = context.regY;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	return 2;
}

//Unconditional jump to anywhere in memory
//Move the address into PC
MAGSNES::byte CPU::JMP(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.regPC = address;

	switch (ADDR_MODE) {
	case ABSOLUTE:
		return 3;
	case ABSOLUTE_INDIRECT:
		return 5;
	default:
		return 3;
	}
}

//Unconditional Jump and Save Return address (a.k.a. Jump to SubRoutine)
MAGSNES::byte CPU::JSR(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	word tmpPC = context.regPC - 1;
	context.refMM[context.regSP + 0x100] = (tmpPC & 0xFF00) >> 8;
	context.regSP = (context.regSP - 1) & 0xFF;
	context.refMM[context.regSP + 0x100] = tmpPC & 0xFF;
	context.regSP = (context.regSP - 1) & 0xFF;

	context.regPC = address;

	return 6;
}

//LoaD memory into regA, then set
//flagN and flagZ accordingly
MAGSNES::byte CPU::LDA(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);
	context.regA = operand;

	context.flagN = (operand & 0x80) ? true : false;
	context.flagZ = (operand == 0) ? true : false;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_X:
		return 4;
	case ABSOLUTE_X:
		return 4;
	case ABSOLUTE_Y:
		return 4;
	case INDIRECT_X:
		return 6;
	case INDIRECT_Y:
		return 5;
	default:
		return 3;
	}
}

//LoaD memory into regX
MAGSNES::byte CPU::LDX(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);
	context.regX = operand;

	context.flagN = (operand & 0x80) ? true : false;
	context.flagZ = (operand == 0) ? true : false;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_Y:
		return 4;
	case ABSOLUTE_Y:
		return 4;
	default:
		return 3;
	}
}

//LoaD memory into regY
MAGSNES::byte CPU::LDY(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);
	context.regY = operand;

	context.flagN = (operand & 0x80) ? true : false;
	context.flagZ = (operand == 0) ? true : false;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_X:
		return 4;
	case ABSOLUTE_X:
		return 4;
	default:
		return 3;
	}
}

//Shift right regA or value at address by 1.
//bit that is shifted off the end is placed in flagC.
//Since a 0 will always be shifted into bit 7, flagN is
//always set to false. Set flagZ if result === 0.
MAGSNES::byte CPU::LSR(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte bitShiftedOff;
	word operand;

	if (ADDR_MODE == ACCUMULATOR) {
		//address will === regA
		bitShiftedOff = address & 0x01;
		operand = address >> 1;
		context.regA = operand;
	} else {
		operand = context.read_byte(address);
		bitShiftedOff = operand & 0x01;
		operand >>= 1;

		context.write_byte(address, operand);
	}

	context.flagN = false;
	context.flagZ = (operand == 0) ? true : false;
	context.flagC = (bitShiftedOff == 1) ? true : false;

	switch (ADDR_MODE) {
	case ACCUMULATOR:
		return 2;
	case ZERO_PAGE:
		return 5;
	case ABSOLUTE:
		return  6;
	case ZERO_PAGE_X:
		return 6;
	case ABSOLUTE_X:
		return 7;
	default:
		return 3;
	}
}

//No OPeration
MAGSNES::byte CPU::NOP(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	//Does nothing
	return 0;
}

//OR memory with regA, store result in regA.
//Adjust flagN and flagZ according to result.
MAGSNES::byte CPU::ORA(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = (context.read_byte(address)) | context.regA;

	context.regA = operand;

	context.flagN = (operand & 0x80) ? true : false;
	context.flagZ = (operand == 0) ? true : false;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_X:
		return 4;
	case ABSOLUTE_X:
		return 4;
	case ABSOLUTE_Y:
		return 4;
	case INDIRECT_X:
		return 6;
	case INDIRECT_Y:
		return 5;
	default:
		return 3;
	}
}

//PusH regA
MAGSNES::byte CPU::PHA(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.refMM[context.regSP + 0x100] = context.regA;
	context.regSP--;

	return 3;
}

//PusH regP (flags)
MAGSNES::byte CPU::PHP(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	//The documentation for this is obscure, but the 6502 DOES set the 
	//B flag (bit 4 of P register) BEFORE pushing the flags. It is also expected
	//that bit 5 (an unused flag) will be unaffected (always on).
	context.refMM[context.regSP + 0x100] = context.flagsToP() | 0x30;
	context.regSP--;

	return 3;
}

//Pop (aka PulL) from stack, place into regA
//set flagN and flagZ accordingly
MAGSNES::byte CPU::PLA(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.regSP++;
	MAGSNES::byte tmp = context.refMM[context.regSP + 0x100];
	context.regA = tmp;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	return 4;
}

//Pop (aka PulL) from stack, place into flags
MAGSNES::byte CPU::PLP(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.regSP++;;
	MAGSNES::byte tmp = context.refMM[context.regSP + 0x100];
	context.pToFlags(tmp & 0xEF);

	return 4;
}

//ROtate regA or memory Left
//flagC is shifted IN to bit 0
//Store shifted off bit in flagC
//Adjust flagN and flagZ accordingly
MAGSNES::byte CPU::ROL(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte bitShiftedOff, tmp, operand;

	if (ADDR_MODE == ACCUMULATOR) {
		bitShiftedOff = address & 0x80;
		operand = address << 1;
		operand |= context.flagC;
		context.regA = operand;
	} else {
		operand = context.read_byte(address);

		bitShiftedOff = operand & 0x80;
		operand <<= 1;
		operand |= context.flagC;

		context.write_byte(address, operand);
	}

	context.flagC = (bitShiftedOff) ? true : false;

	context.flagN = (operand & 0x80) ? true : false;
	context.flagZ = (operand == 0) ? true : false;

	switch (ADDR_MODE) {
	case ACCUMULATOR:
		return 2;
	case ZERO_PAGE:
		return 5;
	case ABSOLUTE:
		return  6;
	case ZERO_PAGE_X:
		return 6;
	case ABSOLUTE_X:
		return 7;
	default:
		return 3;
	}
}

//ROtate regA or memory Right
//same logic as ROL
MAGSNES::byte CPU::ROR(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte bitShiftedOff, operand;

	if (ADDR_MODE == ACCUMULATOR) {
		bitShiftedOff = address & 0x01;
		operand = address >> 1;
		operand = (context.flagC) ? (operand | 0x80) : operand;
		context.regA = operand;
	} else {
		operand = context.read_byte(address);

		bitShiftedOff = operand & 0x01;
		operand >>= 1;
		operand = (context.flagC) ? (operand | 0x80) : operand;

		context.write_byte(address, operand);
	}

	context.flagC = (bitShiftedOff) ? true : false;

	context.flagN = (operand & 0x80) ? true : false;
	context.flagZ = (operand == 0) ? true : false;

	switch (ADDR_MODE) {
	case ACCUMULATOR:
		return 2;
	case ZERO_PAGE:
		return 5;
	case ABSOLUTE:
		return  6;
	case ZERO_PAGE_X:
		return 6;
	case ABSOLUTE_X:
		return 7;
	default:
		return 3;
	}
}

//ReTurn from Interrupt
//First, pop MAGSNES::byte representing flags off of stack, 
//and restore flags. Then, pop word off of stack, 
//which will be put in PC.
MAGSNES::byte CPU::RTI(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.regSP++;
	MAGSNES::byte tmp = context.refMM[context.regSP + 0x100];

	context.pToFlags(tmp);

	context.regSP++;
	tmp = context.refMM[context.regSP + 0x100];

	context.regSP++;
	MAGSNES::byte tmphi = context.refMM[context.regSP + 0x100];

	context.regPC = tmp | (tmphi << 8);

	return 6;
}

//ReTurn from Subroutine
//Pops word off the stack, then put it into regPC.
//Flags are NOT affected!
MAGSNES::byte CPU::RTS(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.regSP++;
	MAGSNES::byte tmp = context.refMM[context.regSP + 0x100];

	context.regSP++;
	MAGSNES::byte tmphi = context.refMM[context.regSP + 0x100];

	//TODO: should the +1 cross a page boundary or wrap?
	context.regPC = (tmp | (tmphi << 8)) + 1;

	return 6;
}

//SuBtract with Carry
//BINARY subtraction
//Subtract operand from regA, then subtract (NOT)flagC, and store 
//result in regA. We negate flagC to make the calculation in line 
//with the 6502 two's complement arithmetic.
//Set flagC when result >= 0.
//Set flagV when bit 7 of of the result and regA before the operation 
//differ, meaning the signed result was less than -128 or greater than
//+127.
//Set flagN and flagZ accordingly.
MAGSNES::byte CPU::SBC(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte operand = context.read_byte(address);

	signed int result = context.regA - operand - (!context.flagC);

	context.flagC = (result >= 0) ? true : false;

	result &= 0xFF;

	//See ADC for overflow explanation
	//Set overflow if pos - neg = neg OR neg - pos = pos
	context.flagV = (((context.regA ^ operand) & 0x80) && ((context.regA ^ result) & 0x80)) ? true : false;

	context.regA = result;

	context.flagN = (result & 0x80) ? true : false;
	context.flagZ = (result == 0) ? true : false;

	switch (ADDR_MODE) {
	case IMMEDIATE:
		return 2;
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_X:
		return 4;
	case ABSOLUTE_X:
		return 4;
	case ABSOLUTE_Y:
		return 4;
	case INDIRECT_X:
		return 6;
	case INDIRECT_Y:
		return 5;
	default:
		return 3;
	}
}

//SEt flagC
MAGSNES::byte CPU::SEC(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.flagC = true;

	return 2;
}

//SEt flagD
MAGSNES::byte CPU::SED(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.flagD = true;

	return 2;
}

//SEt flagI
MAGSNES::byte CPU::SEI(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.flagI = true;

	return 2;
}

//STore regA in memory
MAGSNES::byte CPU::STA(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.write_byte(address, context.regA);

	switch (ADDR_MODE) {
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_X:
		return 4;
	case ABSOLUTE_X:
		return 4;
	case ABSOLUTE_Y:
		return 4;
	case INDIRECT_X:
		return 6;
	case INDIRECT_Y:
		return 5;
	default:
		return 3;
	}
}

//STore regX in memory
MAGSNES::byte CPU::STX(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.write_byte(address, context.regX);

	switch (ADDR_MODE) {
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_Y:
		return 4;
	default:
		return 3;
	}
}

//STore regY in memory
MAGSNES::byte CPU::STY(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.write_byte(address, context.regY);

	switch (ADDR_MODE) {
	case ZERO_PAGE:
		return 3;
	case ABSOLUTE:
		return 4;
	case ZERO_PAGE_X:
		return 4;
	default:
		return 3;
	}
}

//Transfer regA to regX
//Value of regA does not change, adjust flagN and flagZ according to
//the value transferred
MAGSNES::byte CPU::TAX(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte tmp = context.regX = context.regA;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	return 2;
}

//Transfer regA to regY
MAGSNES::byte CPU::TAY(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte tmp = context.regY = context.regA;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	return 2;
}

//Transfer regSp to regX
MAGSNES::byte CPU::TSX(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte tmp = context.regX = context.regSP;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	return 2;
}

//Transfer regX to regA
MAGSNES::byte CPU::TXA(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte tmp = context.regA = context.regX;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	return 2;
}

//Transfer regX to regSp
//DOES NOT AFFECT FLAGS!!!
MAGSNES::byte CPU::TXS(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	context.regSP = context.regX;

	return 2;
}

//Transfer regY to regA
MAGSNES::byte CPU::TYA(word address, MAGSNES::byte ADDR_MODE, CPU &context) {
	MAGSNES::byte tmp = context.regA = context.regY;

	context.flagN = (tmp & 0x80) ? true : false;
	context.flagZ = (tmp == 0) ? true : false;

	return 2;
}
