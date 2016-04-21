#pragma once

#include <cmath>

#include "Core.h"
#include "CPU.h"
#include "GLManager.h"

namespace MAGSNES {

	class PPU {

		friend class Mapper;

	public:
		PPU(Bus *pBus, CPU *pCPU, Core &refCore, GLManager *pGLManager);
		~PPU();

		//Called 3 times for every cycle returned by a call to CPU#executeNext()
		void tick(const bool shouldMonitor);

		void loadMirroringType(const MAGSNES::byte mirroringType);

		//Allows the CPU to connect to these components
		MAGSNES::byte * expose_ppudatabuffer() { return &(regs->ppuDataBuff); }
		word * expose_ppuaddr() { return &(regs->ppuAddr); }
		MAGSNES::byte * expose_ppupalettebaseaddr() { return &(refBus.VM[0x3F00]); }

		enum {
			MIRROR_HORIZONTAL,
			MIRROR_VERTICAL
		};

	private:
		Bus &refBus;
		CPU &refCPU;
		Core &refCore;
		GLManager &refGLM;
		MAGSNES::byte(&refMM)[MM_SIZE];
		MAGSNES::byte(&refVM)[VM_SIZE];

		//The indices in OAM of the 8 sprites to draw; refreshed every scanline.
		byte currentSprites[8];

		//Internal PPU values (i.e. NOT memory-mapped)
		struct PPUREGISTERS {
			bool	usePPUADDRHI, usePPUSCROLLX, //Internal PPU 'toggles'
				spriteSizeIs8x8, //Otherwise use 8x16
				shouldGenerateNMI, //Cause CPU to execute NMI routine on VBLANK start
				shouldShowLeftmostBackground,
				shouldShowLeftmostSprites,
				shouldShowBackground,
				shouldShowSprites;

			word	ppuAddr, //internal PPU VRAM pointer
				nameTableBaseAddr, //Retrieved NT address are offset from here
				patternTableOffset,
				spritePatternTableOffset,
				pixelCounter, //Where the PPU is drawing on the current scanline (0-341)
				scanlineCounter; //The current scanline the PPU is drawing to (0-261)

			MAGSNES::byte	ppuDataBuff, //The data to send to PPUDATA on a read of that register
				ppuIncr, //How much to increment ppuAddr by after certain operations
				fineXOffset,
				fineYOffset,
				mirroringType; //Will usially be MIRROR_HORIZONTAL or MIRROR_VERTICAL
		} *regs;

		//RGBA color values
		static const dword NES_COLOR_PALETTE[0x40];

		/*************************************
		These remaining private member functions use the FORCEINLINE macro but are defined with the macro in the source file, b/c we want the functions
		to be inlined but also to have state access. For this reason, these functions should NOT be called from outside the source file, b/c the
		linker will not be able to find the inlined symbol.
		*************************************/

		//Implements PPU mirroring within range
		word coercePPUAddress(word addr);

		void checkPPUCTRL();

		void checkPPUMASK();

		//Send a byte from main memory to the address in OAM specified in refMM[OAMADDR]
		void readOAMDATA();

		//Change x or y tile offset depending on internal toggle
		void checkPPUSCROLL();

		//Change hi or lo byte of PPU internal address pointer, depending on internal toggle
		void checkPPUADDR();

		//Write the byte in PPUDATA to the address in VRAM pointed to by the internal ppuAddr
		void checkPPUDATA();

		//Listens for CPU R/W to PPU registers; only called on the FIRST cycle after the CPU
		//completes a call to executeNext()
		void monitorAddresses();

		//Called after monitoring for address R/W; draws a single pixel and 
		//increments internal registers
		void emulateCRT();

		//The next two functions are responsible for retrieving the next nametable and sprite pixel to draw. Both store various
		//meta-information (transparent pixel, sprite0, etc.) in the lowest byte of the returned dword.

		/*
		Retrieve the next nametable pixel to draw.

		The algorithm is as follows:

		1) Determine tile in the 32x30 tile map (tileAddr), then retrieve the address in VRAM of the NT entry (ntAddr).

		2) Read the NT entry to determine the address of the pattern table entry to read (patternAddr).

		3) Depending on the X and Y pixel (NOT the tile) coordinate, figure out which byte of the pattern table to read and how to mask it
		and retrieve the bits we need (tmpMask).

		4) Check if both bits are 0; return the universal background color if they are.

		5) Otherwise, retreive the attribute byte to determine which palette to use, and use the retrieved pattern bits to select which of the
		three palette colors to return.
		*/
		const dword get_NT_pixel();

		const dword get_SPR_pixel();

		//Receives nametable and sprite pixels for the XY coord on the active (rendering) scanline, and queries the
		//meta-information in their lowest byte to decide which pixel should be returned and rendered. Also sets 
		//sprite0 hit flag if appropriate.
		const dword multiplex(const dword ntPixel, const dword sprPixel);

		//Retrieves sprite pattern data depending on flip type
		const MAGSNES::byte get_pattern_no_flip(const word xPos, const word yPos, word patternAddr);

		//Flips a sprite around (along the y axis)
		const MAGSNES::byte get_pattern_flip_horizontal(const word xPos, const word yPos, word patternAddr);

		//Flips a sprite upside down (along the x axis)
		const MAGSNES::byte get_pattern_flip_vertical(const word xPos, const word yPos, word patternAddr);

		//Flips a sprite along the x and y axes
		const MAGSNES::byte get_pattern_flip_horizontal_and_vertical(const word xPos, const word yPos, word patternAddr);

	};



} /* namespace NESPP */