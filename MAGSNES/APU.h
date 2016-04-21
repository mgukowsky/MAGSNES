#pragma once

#include "CPU.h"
#include "Core.h"

namespace MAGSNES {

//Emulates the 2A03 pseudo-Audio Processing Unit
class APU {
public:
	APU(CPU *pCPU, Bus *pBus);
	~APU();

	void tick(const MAGSNES::byte CPU_CYCLES);

private:

	//Registers shared by all channels
	struct APUREGISTERS {
		bool interruptInhibitFlag, useFiveStepFrameSequencerMode;
		dword masterCounter;
		MAGSNES::byte frameCounter;
	} *regs;

	//Registers which each channel has its own instance of.
	//A given channel does not use all of its registers.
	struct CHANNELREGISTERS {
		bool disableEnvelopeDecay, shouldLoopEnvelope, sweepEnabled, shouldSweepDownward; //'shouldLoopEnvelope' also serves as the length counter halt for the squares
		MAGSNES::byte volumeFactor, envelopeCounter, envelopePeriod, fadeoutCounter, //'envelopePeriod' is used as the triangle's linear counter period.
									sweepRefreshRate, sweepShiftAmt, sweepCounter, lengthCounter, linearCounter;
		word programmableTimer;
	} *square0Regs, *square1Regs, *triangleRegs;

	Bus &refBus;
	Core &sysCore;
	CPU &refCPU;
	MAGSNES::byte(&refMM)[MM_SIZE];

	void clock_frame_counter();
	void clock_envelope_and_triangle_counter();
	void clock_length_and_sweep_counter();

	void check_apu_ctrl(const MAGSNES::byte val);
	void check_apu_frame_counter(const MAGSNES::byte val);
	void check_channel_square(const MAGSNES::byte val, const word REG_ADDRESS, const MAGSNES::byte CHANNEL_ID);
	void check_channel_triangle(const MAGSNES::byte val, const word REG_ADDRESS);

};

} /* namespace NESPP */