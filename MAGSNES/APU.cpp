#include "APU.h"

#define byte	MAGSNES::byte

//Bits 0 and 1 of an address, where (address >= 0x4000 && address < 0x4010), identify the register affected
#define	REG_0_WRITE				0x00
#define	REG_1_WRITE				0x01
#define REG_2_WRITE				0x02
#define REG_3_WRITE				0x03

//Bits 2 and 3 of an address, where (address >= 0x4000 && address < 0x4010), identify the channel
#define SQUARE_0_WRITE		0x00
#define SQUARE_1_WRITE		0x04
#define TRIANGLE_WRITE		0x08
#define NOISE_WRITE				0x0C

//APU register addresses in main memory
#define SQUARE_0_REG_0		0x4000
#define SQUARE_0_REG_1		0x4001
#define SQUARE_0_REG_2		0x4002
#define SQUARE_0_REG_3		0x4003

#define SQUARE_1_REG_0		0x4004
#define SQUARE_1_REG_1		0x4005
#define SQUARE_1_REG_2		0x4006
#define SQUARE_1_REG_3		0x4007

#define TRIANGLE_REG_0		0x4008
#define TRIANGLE_REG_1		0x400A
#define TRIANGLE_REG_2		0x400B

#define NOISE_REG_0				0x400C
#define NOISE_REG_1				0x400E
#define NOISE_REG_2				0x400F

#define DMC_REG_0					0x4010
#define DMC_REG_1					0x4011
#define DMC_REG_2					0x4012
#define DMC_REG_3					0x4013

//Same register, different bits
#define APUCTRL						0x4015
#define APUSTATUS					0x4015

#define APU_FRAME_COUNTER	0x4017

#define APU_CLOCK_RATE		7457			//1.789773 Mhz / 7457 = ~240Hz; If we clock the APU every 7457 CPU cycles, it will operate at roughly the desired 240Hz


__FILESCOPE__{
	const byte lengthCounterLookupTable[32] =	{
		10, 254, 20, 2, 40, 4, 80, 6, 
		160, 8, 60, 10, 14, 12, 26, 14,
		12, 16, 24, 18, 48, 20, 96, 22, 
		192, 24, 72, 26, 16, 28, 32, 30
	};
}

using namespace MAGSNES;

APU::APU(CPU *pCPU, Bus *pBus)
	: refBus(*pBus), sysCore(Core::get_sys_core()), refCPU(*pCPU),
	refMM(pBus->mainMemory) {

	//TODO: simply 0 out these structs?

	regs = new APUREGISTERS;
	regs->interruptInhibitFlag = false;
	regs->useFiveStepFrameSequencerMode = false;
	regs->masterCounter = 0;
	regs->frameCounter = 0;

	square0Regs = new CHANNELREGISTERS;
	square0Regs->disableEnvelopeDecay = false;
	square0Regs->shouldLoopEnvelope = false;
	square0Regs->volumeFactor = 0;
	square0Regs->envelopeCounter = 0;
	square0Regs->envelopePeriod = 0;
	square0Regs->fadeoutCounter = 0;
	square0Regs->programmableTimer = 0;
	square0Regs->sweepEnabled = false;
	square0Regs->shouldSweepDownward = false;
	square0Regs->sweepShiftAmt = 0;
	square0Regs->sweepRefreshRate = 0;
	square0Regs->sweepCounter = 0;
	square0Regs->lengthCounter = 0;

	square1Regs = new CHANNELREGISTERS;
	square1Regs->disableEnvelopeDecay = false;
	square1Regs->shouldLoopEnvelope = false;
	square1Regs->volumeFactor = 0;
	square1Regs->envelopeCounter = 0;
	square1Regs->envelopePeriod = 0;
	square1Regs->fadeoutCounter = 0;
	square1Regs->programmableTimer = 0;
	square1Regs->sweepEnabled = false;
	square1Regs->shouldSweepDownward = false;
	square1Regs->sweepShiftAmt = 0;
	square1Regs->sweepRefreshRate = 0;
	square1Regs->sweepCounter = 0;
	square1Regs->lengthCounter = 0;

	triangleRegs = new CHANNELREGISTERS;
	triangleRegs->disableEnvelopeDecay = false;
	triangleRegs->shouldLoopEnvelope = false;
	triangleRegs->volumeFactor = 0;
	triangleRegs->envelopeCounter = 0;
	triangleRegs->envelopePeriod = 0;
	triangleRegs->fadeoutCounter = 0;
	triangleRegs->programmableTimer = 0;
	triangleRegs->lengthCounter = 0;
	triangleRegs->linearCounter = 0;
	sysCore.audioRegs.triangleAmp = 0.005; //Triangle wave seems to always be louder than squares, so it must have reduced gain
}

//No cleanup needed
APU::~APU() {
	delete regs;
	delete square0Regs;
	delete square1Regs;
	delete triangleRegs;
}

void APU::tick(const byte CPU_CYCLES) {

	//Check for R/W to memory-mapped I/O registers
	if (refBus.readBus == APUSTATUS) { //APU only has one read register

	}

	if ((refBus.writeBus & 0xFF00) == 0x4000) { //Don't care if write isn't to 0x40XX
		if (refBus.writeBus < 0x4010) { //It's to a sq/tri/noise reg
			switch (refBus.writeBus & 0x0C) {
			case SQUARE_0_WRITE:
				check_channel_square(refMM[refBus.writeBus], refBus.writeBus, SQUARE_0_WRITE);
				break;
			case SQUARE_1_WRITE:
				check_channel_square(refMM[refBus.writeBus], refBus.writeBus, SQUARE_1_WRITE);
				break;
			case TRIANGLE_WRITE:
				check_channel_triangle(refMM[refBus.writeBus], refBus.writeBus);
				break;
			}

		} else { //It's to a DMC or ctrl reg
			switch (refBus.writeBus) {
			case APUCTRL:
				check_apu_ctrl(refMM[APUCTRL]);
				break;
			case APU_FRAME_COUNTER:
				check_apu_frame_counter(refMM[APU_FRAME_COUNTER]);
				break;
			}
		}
	}

	for (int i = 0; i < CPU_CYCLES; i++) {
		regs->masterCounter++;
		if (regs->masterCounter == APU_CLOCK_RATE) {
			regs->masterCounter = 0;

			clock_frame_counter();
		}
	}
}

FORCEINLINE void APU::clock_frame_counter() {

	if (regs->useFiveStepFrameSequencerMode) { //5-step mode
		if (regs->frameCounter == 5) {
			regs->frameCounter = 0;
		}

		if (regs->frameCounter == 4) {

			//Do nothing; effectively lower frame counter frequency to ~192Hz

		} else {
			if (!(regs->frameCounter & 0x01)) { //Clock length & sweep on 0 and 2
				clock_length_and_sweep_counter();
			}

			clock_envelope_and_triangle_counter(); //These get clocked on 0, 1, 2, 3

			//We HAVE to check if we turn on/off the triangle here, because it depends on both its counters being above 0
			if ((triangleRegs->lengthCounter != 0) && (triangleRegs->linearCounter != 0)) {
				sysCore.audioRegs.triangleImplicitOff = false;
			} else {
				sysCore.audioRegs.triangleImplicitOff = true;
			}

		}

	} else { //4-step mode
		if (regs->frameCounter == 4) {
			regs->frameCounter = 0;
		}

		//Fire CPU IRQ on last step of sequence
		if (regs->frameCounter == 3 && !(regs->interruptInhibitFlag)) {
			refCPU.post_interrupt(CPU::INTERRUPT_IRQ);
		}

		if (regs->frameCounter & 0x01) { //Clock length & sweep on 1 and 3
			clock_length_and_sweep_counter();
		}

		clock_envelope_and_triangle_counter(); //Always clock these

		//We HAVE to check if we turn on/off the triangle here, because it depends on both its counters being above 0
		if ((triangleRegs->lengthCounter != 0) && (triangleRegs->linearCounter != 0)) {
			sysCore.audioRegs.triangleImplicitOff = false;
		} else {
			sysCore.audioRegs.triangleImplicitOff = true;
		}
	}

	regs->frameCounter++;
}

FORCEINLINE void APU::clock_envelope_and_triangle_counter() {

	//Square0
	if (square0Regs->disableEnvelopeDecay) { //No envelope; set volume to register val
		sysCore.audioRegs.square0positiveAmp = square0Regs->volumeFactor * sysCore.audioRegs.baseAmp;
		sysCore.audioRegs.square0negativeAmp = square0Regs->volumeFactor * sysCore.audioRegs.baseAmp * -1.0;

	} else { //Volume is determined by the internal envelope counter
		sysCore.audioRegs.square0positiveAmp = square0Regs->envelopeCounter * sysCore.audioRegs.baseAmp;
		sysCore.audioRegs.square0negativeAmp = square0Regs->envelopeCounter * sysCore.audioRegs.baseAmp * -1.0;

		if (square0Regs->envelopeCounter != 0) {

			//Decrement the envelope counter depending on the period
			//envelopePeriod of 0 means amp will decay at 240Hz, 1 at 120Hz, 2 at 60Hz etc.
			if (square0Regs->fadeoutCounter == square0Regs->envelopePeriod) {
				if (square0Regs->envelopeCounter != 0) {
					square0Regs->envelopeCounter--;
					square0Regs->fadeoutCounter = 0;
				}

			} else {
				square0Regs->fadeoutCounter++;
			}

		} else { //envelopeCounter is 0
			if (square0Regs->shouldLoopEnvelope) { //Reset to the specified value if the envelope loops
				square0Regs->envelopeCounter = square0Regs->envelopePeriod;
			}
		}
	}

	//Square1
	if (square1Regs->disableEnvelopeDecay) { //No envelope; set volume to register val
		sysCore.audioRegs.square1positiveAmp = square1Regs->volumeFactor * sysCore.audioRegs.baseAmp;
		sysCore.audioRegs.square1negativeAmp = square1Regs->volumeFactor * sysCore.audioRegs.baseAmp * -1.0;

	} else { //Volume is determined by the internal envelope counter
		sysCore.audioRegs.square1positiveAmp = square1Regs->envelopeCounter * sysCore.audioRegs.baseAmp;
		sysCore.audioRegs.square1negativeAmp = square1Regs->envelopeCounter * sysCore.audioRegs.baseAmp * -1.0;

		if (square1Regs->envelopeCounter != 0) {

			//Decrement the envelope counter depending on the period
			//envelopePeriod of 0 means amp will decay at 240Hz, 1 at 120Hz, 2 at 60Hz etc.
			if (square1Regs->fadeoutCounter == square1Regs->envelopePeriod) {
				if (square1Regs->envelopeCounter != 0) {
					square1Regs->envelopeCounter--;
					square1Regs->fadeoutCounter = 0;
				}

			} else {
				square1Regs->fadeoutCounter++;
			}

		} else { //envelopeCounter is 0
			if (square1Regs->shouldLoopEnvelope) { //Reset to the specified value if the envelope loops
				square1Regs->envelopeCounter = square1Regs->envelopePeriod;
			}
		}
	}

	//Triangle linear counter
	if ((triangleRegs->linearCounter != 0) && (!(triangleRegs->shouldLoopEnvelope))) {
		triangleRegs->linearCounter--;
	} else {
		if ((triangleRegs->shouldLoopEnvelope)) {
			triangleRegs->linearCounter = triangleRegs->envelopePeriod; //'envelopePeriod' is an alias for the linear counter's period
		}
	}

	//Otherwise leave the linear counter at zero

}

FORCEINLINE void APU::clock_length_and_sweep_counter() {

	//Sweep monitoring

	//Square0
	if (square0Regs->sweepEnabled && !(sysCore.audioRegs.square0ImplicitOff)) {
		if (square0Regs->sweepCounter == square0Regs->sweepRefreshRate) {

			//Note that this implementation is imperfect, as the float period needs to be cast to a word in order to be shifted,
			//which means we lose some precision
			if (square0Regs->sweepShiftAmt != 0) {
				if (square0Regs->shouldSweepDownward) {
					sysCore.audioRegs.square0Period -= ((word)sysCore.audioRegs.square0Period >> square0Regs->sweepShiftAmt);
					//Implicit silence at >12.4KHz
					if (sysCore.audioRegs.square0Period < ((sysCore.audioRegs.SAMPLE_FREQUENCY)/12400)) {
						sysCore.audioRegs.square0ImplicitOff = true;
						square0Regs->sweepEnabled = false;
					}
				} else {
					sysCore.audioRegs.square0Period += ((word)sysCore.audioRegs.square0Period >> square0Regs->sweepShiftAmt);
					//Implicit silence at ~<50Hz
					if (sysCore.audioRegs.square0Period > ((sysCore.audioRegs.SAMPLE_FREQUENCY) / 50)) {
						sysCore.audioRegs.square0ImplicitOff = true;
						square0Regs->sweepEnabled = false;
					}
				}
			}

			square0Regs->sweepCounter = 0;
		} else {
			square0Regs->sweepCounter++;
		}
	}

	//Square1 (slight difference in how downsweep works)
	if (square1Regs->sweepEnabled && !(sysCore.audioRegs.square0ImplicitOff)) {
		if (square1Regs->sweepCounter == square1Regs->sweepRefreshRate) {

			//Note that this implementation is imperfect, as the float period needs to be cast to a word in order to be shifted,
			//which means we lose some precision
			if (square1Regs->sweepShiftAmt != 0) {
				if (square1Regs->shouldSweepDownward) {
					sysCore.audioRegs.square1Period -= (((word)sysCore.audioRegs.square1Period >> square1Regs->sweepShiftAmt) - 1); //Square1 goes down a hair faster
					//Implicit silence at >12.4KHz
					if (sysCore.audioRegs.square1Period < ((sysCore.audioRegs.SAMPLE_FREQUENCY) / 12400)) {
						sysCore.audioRegs.square1ImplicitOff = true;
						square1Regs->sweepEnabled = false;
					}
				} else {
					sysCore.audioRegs.square1Period += ((word)sysCore.audioRegs.square1Period >> square1Regs->sweepShiftAmt);
					//Implicit silence at ~<50Hz
					if (sysCore.audioRegs.square1Period >((sysCore.audioRegs.SAMPLE_FREQUENCY) / 50)) {
						sysCore.audioRegs.square1ImplicitOff = true;
						square1Regs->sweepEnabled = false;
					}
				}
			}

			square1Regs->sweepCounter = 0;
		} else {
			square1Regs->sweepCounter++;
		}
	}

	//Length counter monitoring
	if (square0Regs->lengthCounter != 0 && !(square0Regs->shouldLoopEnvelope)) {
		square0Regs->lengthCounter--;
	}

	if (square0Regs->lengthCounter == 0) {
		sysCore.audioRegs.square0ImplicitOff = true;
		refMM[APUCTRL] &= 0xFE; //Turn off status flag for channel
	}


	if (square1Regs->lengthCounter != 0 && !(square1Regs->shouldLoopEnvelope)) {
		square1Regs->lengthCounter--;
	}

	if (square1Regs->lengthCounter == 0) {
		sysCore.audioRegs.square1ImplicitOff = true;
		refMM[APUCTRL] &= 0xFD; //Turn off status flag for channel
	}

	if ((triangleRegs->lengthCounter != 0) && !(triangleRegs->shouldLoopEnvelope)) {
		triangleRegs->lengthCounter--;
	}

	if (triangleRegs->lengthCounter == 0) {
		refMM[APUCTRL] &= 0xFB; //Turn off status flag for channel
	}
}

FORCEINLINE void APU::check_apu_ctrl(const byte val) {
	refMM[APUCTRL] &= 0x7F; //reset IRQ flag for apuctrl reads

	//*****BUG*****: when channels are silenced by an apuctrl write, they can only be re-enabled with another apuctrl write.
	//In this implementation, a write to any register or any event that touches the implicit silence flag for a channel 
	//has the ability to re-enable the channel.

	//*****HACK*****: We do not re-enable channels using the GUID channel switch, although the NES would enable a channel when the corresponding
	//bit is set. The reason we do not do this is because the GUID is affected by a write to the programmable-timer/length register, and 
	//if we re-enable the channels every time this register is written to, then the channel may incorrectly be re-enable a channel that is off due to
	//some other condition (i.e. its length counter is 0), which can cause audio glitches due to the fact that the audio rendering thread may see this 
	//switch as on (incorrectly) and thus render the channel for a brief moment, causing spikes or other artifacts.
	if (val & 0x01) {
		//sysCore.audioRegs.square0ImplicitOff = false;
	} else {
		sysCore.audioRegs.square0ImplicitOff = true;
		square0Regs->lengthCounter = 0;
		refMM[APUCTRL] &= 0xFE; //Turn off status flag
	}

	if (val & 0x02) {
		//sysCore.audioRegs.square1ImplicitOff = false;
	} else {
		sysCore.audioRegs.square1ImplicitOff = true;
		square1Regs->lengthCounter = 0;
		refMM[APUCTRL] &= 0xFD;
	}

	if (val & 0x04) {
		//sysCore.audioRegs.square0ImplicitOff = false;
	} else {
		sysCore.audioRegs.triangleImplicitOff = true;
		triangleRegs->lengthCounter = 0;
		refMM[APUCTRL] &= 0xFB;
	}
}

FORCEINLINE void APU::check_apu_frame_counter(const byte val) {
	regs->interruptInhibitFlag = (val & 0x40) ? true : false;
	regs->useFiveStepFrameSequencerMode = (val & 0x80) ? true : false;

	//********* UNSURE: Some sources say to clock the frame counter an ADDITIONAL time on a $4017 write?
	regs->frameCounter = 0; //Reset frame counter; Note that the frame counter may still be clocked this cycle
}

FORCEINLINE void APU::check_channel_square(const byte val, const word REG_ADDRESS, const byte CHANNEL_ID) {
	CHANNELREGISTERS *currentChannelRegs;
	DutyCycle *pCurrentDutyCycle;
	Core::AudioChannelID currentChannelID;
	bool *currentImplicitOffSwitch;
	float tmp;

	if (CHANNEL_ID == SQUARE_0_WRITE) {
		currentChannelRegs = square0Regs;
		pCurrentDutyCycle = &(sysCore.audioRegs.square0DutyCycle);
		currentChannelID = Core::AudioChannelID::SQUARE_0;
		currentImplicitOffSwitch = &(sysCore.audioRegs.square0ImplicitOff);

	} else {
		currentChannelRegs = square1Regs;
		pCurrentDutyCycle = &(sysCore.audioRegs.square1DutyCycle);
		currentChannelID = Core::AudioChannelID::SQUARE_1;
		currentImplicitOffSwitch = &(sysCore.audioRegs.square1ImplicitOff);
	}

	switch (REG_ADDRESS & 0x03) {
	case REG_0_WRITE: //Volume & duty cycle
		currentChannelRegs->volumeFactor = val & 0x0F;
		currentChannelRegs->envelopePeriod = val & 0x0F; //The envelope decays at a rate of 240Hz / <this value + 1>
		currentChannelRegs->envelopeCounter = val & 0x0F;
		currentChannelRegs->fadeoutCounter = 0;
		currentChannelRegs->disableEnvelopeDecay = (val & 0x10) ? true : false;
		currentChannelRegs->shouldLoopEnvelope = (val & 0x20) ? true : false;

		switch (val & 0xC0) {
		case 0x00:
			*(pCurrentDutyCycle) = DutyCycle::DUTY_CYCLE_EIGHTH;
			break;

		case 0x40:
			*(pCurrentDutyCycle) = DutyCycle::DUTY_CYCLE_QUARTER;
			break;

		case 0x80:
			*(pCurrentDutyCycle) = DutyCycle::DUTY_CYCLE_HALF;
			break;

		case 0xC0:
			*(pCurrentDutyCycle) = DutyCycle::DUTY_CYCLE_QUARTER;
			break;
		}

		break; /* case REG_0_WRITE */

	case REG_1_WRITE: //Sweep register
		currentChannelRegs->sweepEnabled = (val & 0x80) ? true : false;
		currentChannelRegs->sweepRefreshRate = (val & 0x70) >> 4; //bits 6-4; sweep changes frequency at a rate of ~120Hz / (thisVal+1)
		currentChannelRegs->shouldSweepDownward = (val & 0x08) ? true : false;
		currentChannelRegs->sweepShiftAmt = val & 0x07; //bits 0-2
		currentChannelRegs->sweepCounter = 0; //Reset sweep counter (NOT sure if this is correct...)
		break;

		//These two cases are used to change a channel's frequency. 
	case REG_2_WRITE: //Timer lo 8 bits
		currentChannelRegs->programmableTimer &= 0x700;
		currentChannelRegs->programmableTimer |= val;
		tmp = 111860.8 / (currentChannelRegs->programmableTimer + 1);

		//Change frequency depending on conditions; see below
		if ((currentChannelRegs->programmableTimer >= 8) && (currentChannelRegs->programmableTimer <= 0x7FF)) {
			sysCore.set_channel_period(tmp, currentChannelID);
			//*currentImplicitOffSwitch = false; //99% sure that docs say that the off switch is only affected by the hi write
		} else {
			*currentImplicitOffSwitch = true; //But we still need to make sure we silence it if it goes past a threshold period/frequency
		}

		break;

	case REG_3_WRITE: //Timer hi 3 bits, length counter load
		currentChannelRegs->programmableTimer &= 0xFF;
		currentChannelRegs->programmableTimer |= ((val & 0x07) << 8);

		//Magic equation for translating NES period into frequency
		tmp = 111860.8 / (currentChannelRegs->programmableTimer + 1);

		//***In short: Writing a period < 8 to a channel implicitly silences it.
		//Some games (i.e. Mega Man 2) like to shut a channel up by writing a low period (less than 8) or 0 to the timer (as opposed to writing 0 to the volume).
		//On the actual NES this would produce a frequency past the limit of ~12.4KHz that would implicitly silence the channel,
		//but in our implementation we have to check for this since our hardware has no such limit.
		//Implicit silence also occurs at periods above 0x7FF, which equates to frequencies below ~50Hz
		if ((currentChannelRegs->programmableTimer >= 8) && (currentChannelRegs->programmableTimer <= 0x7FF)) {
			sysCore.set_channel_period(tmp, currentChannelID);
			*currentImplicitOffSwitch = false;
		} else {
			*currentImplicitOffSwitch = true;
		}

		currentChannelRegs->envelopeCounter = 0x0F; //Make sure to reset the envelope counter here; on the actual APU the phase of the square is reset too.
		
		currentChannelRegs->lengthCounter = lengthCounterLookupTable[(((val & 0xF8) >> 3) & 0x1F)]; //5 bit index into the 32 entry lookup table (we mask to lo 5 bits at the end to avoid overflowing the buffer)
		
		if (CHANNEL_ID == SQUARE_0_WRITE) {
			refMM[APUCTRL] |= 0x01; //Inform the status register that this channel's length counter is >0
		} else {
			refMM[APUCTRL] |= 0x02;
		}

		break;
	}
}

void APU::check_channel_triangle(const byte val, const word REG_ADDRESS) {
	float tmp;
	switch (REG_ADDRESS & 0x03) {
	case REG_0_WRITE: //Control and linear counter
		triangleRegs->shouldLoopEnvelope = (val & 0x80) ? true : false; //Once again, 'shouldLoopEnvelope' is an alias for length counter halt
		triangleRegs->linearCounter = val & 0x7F;
		triangleRegs->envelopePeriod = val & 0x7F;

		break;

		//These two cases are used to change a channel's frequency. 
	case REG_2_WRITE: //Timer lo 8 bits
		triangleRegs->programmableTimer &= 0x700;
		triangleRegs->programmableTimer |= val;

		if (triangleRegs->programmableTimer < 2) {
			sysCore.audioRegs.triangleImplicitOff = true;
		} else if (triangleRegs->programmableTimer > 0x7FF) {
			sysCore.audioRegs.triangleImplicitOff = true;
		} else {
			//Magic equation for translating NES period into frequency (1 octave lower than squares)
			tmp = 55930.4 / (triangleRegs->programmableTimer);
			sysCore.set_channel_period(tmp, Core::AudioChannelID::TRIANGLE);
			sysCore.audioRegs.triangleImplicitOff = false;
		}

		break;

	case REG_3_WRITE: //Timer hi 3 bits
		triangleRegs->programmableTimer &= 0xFF;
		triangleRegs->programmableTimer |= ((val & 0x07) << 8);

		if (triangleRegs->programmableTimer < 2) {
			sysCore.audioRegs.triangleImplicitOff = true;
		} else if (triangleRegs->programmableTimer > 0x7FF) {
			sysCore.audioRegs.triangleImplicitOff = true;
		} else {
			//Magic equation for translating NES period into frequency (1 octave lower than squares)
			tmp = 55930.4 / (triangleRegs->programmableTimer + 1);

			sysCore.set_channel_period(tmp, Core::AudioChannelID::TRIANGLE);
			sysCore.audioRegs.triangleImplicitOff = false;
		}

		triangleRegs->lengthCounter = lengthCounterLookupTable[(((val & 0xF8) >> 3) & 0x1F)]; //5 bit index into the 32 entry lookup table (we mask to lo 5 bits at the end to avoid overflowing the buffer)


		refMM[APUCTRL] |= 0x40;


		break;
	}
}