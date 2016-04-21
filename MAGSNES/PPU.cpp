#include "PPU.h"

#define byte		MAGSNES::byte

//Memory-mapped I/O registers
#define	PPUCTRL																0x2000 
#define PPUMASK																0x2001 
#define PPUSTATUS															0x2002
#define OAMADDR																0x2003
#define OAMDATA																0x2004 
#define PPUSCROLL															0x2005
#define PPUADDR																0x2006 
#define PPUDATA																0x2007 
#define OAMDMA																0x4014

//VRAM addresses to check on
#define UNIVERSAL_BACKGROUND_ADDR							0x3F00 
#define BACKGROUND_PALETTE_ZERO								0x3F01
#define BACKGROUND_PALETTE_ONE								0x3F05 
#define BACKGROUND_PALETTE_TWO								0x3F09
#define BACKGROUND_PALETTE_THREE							0x3F0D 
#define SPRITE_PALETTE_ZERO										0x3F11
#define SPRITE_PALETTE_ONE										0x3F15 
#define SPRITE_PALETTE_TWO										0x3F19
#define SPRITE_PALETTE_THREE									0x3F1D


//The get_*_pixel functions OR these values into the lowest byte of the palette entries they return, as meta information about the pixel.
#define TRANSPARENT_BACKGROUND								0x12
#define OPAQUE_BACKGROUND											0x34
#define TRANSPARENT_SPRITE										0x56
#define OPAQUE_SPRITE													0x78
#define OPAQUE_SPRITE_PRIORITY_FOREGROUND			0x41
#define SPRITE_ZERO_TRANSPARENT								0x9A
#define SPRITE_ZERO_OPAQUE										0xBC

//Magic number that indicates no more sprites are in range of the current scanline;
//will not collide with sprite indices b/c currentSprites holds multiples of 4 only
#define NULL_SPRITE														0xFE

//Start addresses of the 4 nametables
#define NTADDR_A															0x2000
#define NTADDR_B															0x2400
#define NTADDR_C															0x2800
#define NTADDR_D															0x2C00

//Other magic numbers
#define SCANLINE_LIMIT												240
#define PIXEL_LIMIT														256								

using namespace MAGSNES;

PPU::PPU(Bus *pBus, CPU *pCPU, Core &refCore, GLManager *pGLManager)
	: refBus(*pBus),
	refCPU(*pCPU),
	refCore(refCore),
	refGLM(*pGLManager),
	refMM(this->refBus.mainMemory),
	refVM(this->refBus.VM),
	regs(nullptr) {

	regs = new PPUREGISTERS{
		//All bool flags except shouldGenerateNMI are true
		/*bools*/true, true, true, false, true, true, true, true,
		/*words*/0, 0x2000 /*nameTableBaseAddr*/, 0, 0, 0, 0,
		/*bytes*/0, 1 /*ppuIncr*/, 0, 0, 0
	};

}

PPU::~PPU() {
	delete regs;
}

void PPU::tick(const bool shouldMonitor) {
	if (shouldMonitor) {
		monitorAddresses();
	}

	emulateCRT();
}

void PPU::loadMirroringType(const byte mirroringType) {
	regs->mirroringType = mirroringType;
}

const dword PPU::NES_COLOR_PALETTE[0x40] = {
	0x75757500, //dark gray
	0x271B8F00, //dark blue
	0x0000AB00, //med blue
	0x47009F00, //deep purple
	0x8F007700, //dark purple
	0xAB001300, //dark red
	0xA7000000, //dark orange
	0x7F0B0000, //brown

							//0x08
	0x432F0000, //dark brown
	0x00470000, //dark green
	0x00510000, //med green
	0x003F1700, //deep green
	0x1B3F5F00, //blue gray
	0x00000000, //black
	0x00000000,
	0x00000000,

	//0x10
	0xBCBCBC00, //med gray
	0x0073EF00, //med blue
	0x233BEF00, //med blue
	0x8300F300, //indigo
	0xBF00BF00, //med purple
	0xE7005B00, //watermelon
	0xDB2B0000, //med orange
	0xCB4F0F00, //orange brown

							//0x18
	0x8B730000, //pickle
	0x00970000, //med green
	0x00AB0000, //med green
	0x00933B00, //med green
	0x00838B00, //teal
	0x00000000,
	0x00000000,
	0x00000000,

	//0x20
	0xFFFFFF00, //white
	0x3FBFFF00, //cyan
	0x5F97FF00, //med blue
	0xA78BFD00, //indigo
	0xF77BFF00, //indigo
	0xFF77B700, //pink
	0xFF776300, //rose
	0xFF9B3B00, //light orange

							//0x28
	0xF3BF3F00, //light orange
	0x83D31300, //sea green
	0x4FDF4B00, //light green
	0x58F89800, //light green
	0x00EBDB00, //light blue
	0x00000000,
	0x00000000,
	0x00000000,

	//0x30
	0xFFFFFF00,
	0xABE7FF00, //sky blue
	0xC7D7FF00, //light blue
	0xD7CBFF00, //light purple
	0xFFC7FF00, //light pink
	0xFFC7DB00, //light pink
	0xFFBFB300, //light orange
	0xFFDBAB00, //light yellow

							//0x38
	0xFFE7A300, //light yellow
	0xE3FFA300, //light green
	0xABF3BF00, //light green
	0xB3FFCF00, //light green
	0x9FFFF300, //light blue
	0x00000000,
	0x00000000,
	0x00000000
};

FORCEINLINE word PPU::coercePPUAddress(word addr) {
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

FORCEINLINE void PPU::checkPPUCTRL() {
	byte	ppuctrlVal = refMM[PPUCTRL],
		NTID = ppuctrlVal & 0x03;

	switch (NTID) {
	case 0:
		regs->nameTableBaseAddr = 0x2000;
		break;
	case 1:
		regs->nameTableBaseAddr = 0x2400;
		break;
	case 2:
		regs->nameTableBaseAddr = 0x2800;
		break;
	case 3:
		regs->nameTableBaseAddr = 0x2C00;
		break;
	}

	regs->spriteSizeIs8x8 = (ppuctrlVal & 0x20) ? false : true;
	regs->patternTableOffset = (ppuctrlVal & 0x10) ? 0x1000 : 0;
	regs->spritePatternTableOffset = (ppuctrlVal & 0x08) ? 0x1000 : 0;
	regs->ppuIncr = (ppuctrlVal & 0x04) ? 32 : 1;
	regs->shouldGenerateNMI = (ppuctrlVal & 0x80) ? true : false;
}

FORCEINLINE void PPU::checkPPUMASK() {
	byte ppumaskVal = refMM[PPUMASK];

	regs->shouldShowLeftmostBackground = (ppumaskVal & 0x02) ? true : false;
	regs->shouldShowLeftmostSprites = (ppumaskVal & 0x04) ? true : false;
	regs->shouldShowBackground = (ppumaskVal & 0x08) ? true : false;
	regs->shouldShowSprites = (ppumaskVal & 0x10) ? true : false;
}

FORCEINLINE void PPU::readOAMDATA() {
	byte dataToCopy = refMM[OAMDATA];
	byte destination = refMM[OAMADDR];
	refBus.OAM[destination] = dataToCopy;
	refMM[OAMADDR] += 1;
};

FORCEINLINE void PPU::checkPPUSCROLL() {
	byte ppuscrollVal = refMM[PPUSCROLL];
	bool shouldChangeX = regs->usePPUSCROLLX;

	/***** TODO: Should both registers only be updated when the toggle is off, a la checkPPUADDR? *****/

	if (shouldChangeX) {
		regs->fineXOffset = ppuscrollVal;
	} else {
		regs->fineYOffset = ppuscrollVal;
	}

	regs->usePPUSCROLLX = !shouldChangeX;
}

FORCEINLINE void PPU::checkPPUADDR() {
	byte ppuaddrVal = refMM[PPUADDR];
	bool shouldWriteHI = regs->usePPUADDRHI;

	static word addrHiByte = 0x00;	//We don't change the address register until both bytes have been written

	if (shouldWriteHI) {
		addrHiByte = ppuaddrVal;
	} else {

		/****************** BEGIN HACKY SECTION *******************/

		word newAddr = (addrHiByte << 8) | ppuaddrVal;

		/*
		A request to write to a mirrored NT means that the ROM implicitly wants to change the base NT address, so we handle that here.
		This is because changing PPUADDR during rendering changes the address in memory from which the PPU gets the next tile to draw.

		For example, in Super Mario Bros, two NTs are used: NTADDR_A and NTADDR_B. As part of vertical scrolling, the PPU alternates between
		A and B as the player advances right. To draw the status bar on top of the screen, however, the PPU uses the NT data from A. Therefore,
		when the PPU rolls over to B at the end of mirroring, the ROM writes the new NT base address to PPUCTRL as expected, but as it renders the
		status bar on top it writes to an address in a NTADDR_C, understanding that this will instruct the PPU to use the data from A
		due to vertical mirroring. When it detects the sprite0 hit, it writes to PPUCTRL and changes the NT back to B.

		In short, without this code, SMB will never detect the sprite0 hit which tells it to stop rendering the status bar and return to the playfield,
		and thus the game will freeze.

		A nearly identical issue is described (and solved) here: http://forums.nesdev.com/viewtopic.php?f=3&t=12185
		An in depth explanation can be found here: http://forums.nesdev.com/viewtopic.php?t=664

		*/

		if (regs->mirroringType == MIRROR_VERTICAL && newAddr >= NTADDR_C) {
			newAddr -= 0x800;
			if (newAddr > 0x23FF) {
				regs->nameTableBaseAddr = NTADDR_A;
			} else {
				regs->nameTableBaseAddr = NTADDR_B;
			}
		}

		/********************* END HACKY SECTION ******************/

		regs->ppuAddr = (addrHiByte << 8) | ppuaddrVal;
	}


	regs->usePPUADDRHI = !shouldWriteHI;
}

FORCEINLINE void PPU::checkPPUDATA() {
	word dstAddr = coercePPUAddress(regs->ppuAddr);

	refVM[dstAddr] = refMM[PPUDATA];

	regs->ppuAddr += regs->ppuIncr;
}

FORCEINLINE void PPU::monitorAddresses() {

	//If address is not in these ranges, it isn't a register
	const word readHI = refBus.readBus & 0xF000, writeHI = refBus.writeBus & 0xF000;

	if ((readHI == 0x2000)) {
		switch (refBus.readBus) {
		case PPUSTATUS:
			//Reset bit 7 of PPUSTATUS if it was just read from.
			//TODO: documentation is unclear if this also resets PPUSCROLL and PPUADDR?			
			refMM[PPUSTATUS] = refMM[PPUSTATUS] & 0x7F;
			//Reset PPU toggle
			regs->usePPUADDRHI = true;
			regs->usePPUSCROLLX = true;
			break;

		case PPUDATA:
			//A read to PPUDATA needs to be handled as a special case
			word coercedAddr = coercePPUAddress(regs->ppuAddr);
			byte buffedVal;

			//CPU retrieves the actual data from the databuffer or palette RAM. PPU is responsible for incrementing ppuAddr and 
			//placing the correct value into the buffer on a read of PPUDATA
			if (coercedAddr >= 0x3F00) { //Return a palette entry immediately
				buffedVal = refVM[coercedAddr];
				regs->ppuDataBuff = buffedVal;
			} else {
				//Send the data in the buffer to PPUDATA, then put the value at the coerced address into the buffer
				buffedVal = regs->ppuDataBuff;
				regs->ppuDataBuff = refVM[coercedAddr];
			}

			//Don't need to actually place anything at refMM[PPUDATA] ***** TODO: Should we put the value there? ******
			//refMM[PPUDATA] = refVM[coercedAddr];

			//Increment PPU's VRAM address by 1 or 32 on a read to PPUDATA
			regs->ppuAddr += regs->ppuIncr;

			break;
		}
	}

	//Monitor writes
	if ((writeHI == 0x2000) || (writeHI == 0x4000)) {
		switch (refBus.writeBus) {
		case PPUCTRL:
			checkPPUCTRL();
			break;
		case PPUMASK:
			checkPPUMASK();
			break;
		case OAMDATA:
			readOAMDATA();
			break;
		case PPUSCROLL:
			checkPPUSCROLL();
			break;
		case PPUADDR:
			checkPPUADDR();
			break;
		case PPUDATA:
			checkPPUDATA();
			break;
		case OAMDMA:
			word startAddr = refMM[OAMDMA] * 0x100;
			refCPU.start_DMA(startAddr);
			break;
		}
	}

	//Make sure we do not erroneously respond to it again next cycle group; take value off the"bus"
	refBus.readBus = NULL;
	refBus.writeBus = NULL;

}

FORCEINLINE void PPU::emulateCRT() {

	/*******************IMPORTANT********************
	pixelCounter and scanlineCounter determine the index in the undeflying software video buffer to write to,
	so drawPixel must NOT be called when either of these counter is greater than their respective limits, as this will
	cause weird overflow bugs in the heap, where the vbuffer lives.
	*************************************************/
	if (regs->scanlineCounter < NES_SCREEN_HEIGHT) {
		//executeScanlineTick.call(this);

		//Render on first 256 pixels per scanline
		if (regs->pixelCounter < NES_SCREEN_WIDTH) {
			//Get NT pixel
			//TODO: cache NT every 8 pixels?
			if (regs->shouldShowBackground) {
				dword ntPx, sprPx;

				if (regs->shouldShowBackground) {
					ntPx = get_NT_pixel();
				} else {
					ntPx = 0;
				}

				if (regs->shouldShowSprites) {
					sprPx = get_SPR_pixel();
				} else {
					sprPx = NULL_SPRITE;
				}

				refGLM.draw_pixel(regs->pixelCounter, regs->scanlineCounter, multiplex(ntPx, sprPx));
			}
		}

	}

	if (regs->pixelCounter > 340) {
		regs->pixelCounter = 0;
		regs->scanlineCounter++;

		//Do a linear search for the 8 sprites to draw if we are rendering the scanline
		if (regs->scanlineCounter < 240) {
			int	diff;
			byte	numSpritesDrawn = 0, spriteLimit = (regs->spriteSizeIs8x8) ? 8 : 16;

			byte tmpIdx;
			for (int i = 0; i < 64; i++) {
				tmpIdx = i * 4;
				diff = regs->scanlineCounter - (refBus.OAM[tmpIdx] + 1); //Y coord is stored -1

				if ((diff >= 0) && (diff < spriteLimit)) { //Is the Y coord in range (Similar to algorithm on actual NES PPU)?
					currentSprites[numSpritesDrawn] = tmpIdx;
					numSpritesDrawn++;
					if (numSpritesDrawn == 8) { //Actual NES hardware only renders first 8 sprites it finds in range of the current scanline
																			//Set sprite overflow flag
						refMM[PPUSTATUS] |= 0x20;
						break;
					}
				}
			}

			//Clear out remainder of sprite buffer if necessary.
			for (int i = numSpritesDrawn; i < 8; i++) {
				currentSprites[i] = NULL_SPRITE; //Indicate that no more sprites were found   
			}
		}

		if (regs->scanlineCounter > 261) {
			regs->scanlineCounter = 0;
		} else if (regs->scanlineCounter == 0) {
			//TODO: cache values here (since rendering starts here)
			//Cache: colors, maybe patterns, sprites?

		} else if (regs->scanlineCounter == 1) {
			//Turn off vblank flag, overflow flag, and sprite 0 hit
			refMM[PPUSTATUS] &= 0x1F;
		} else if (regs->scanlineCounter == 241) {
			if (regs->shouldGenerateNMI) {
				refCPU.post_interrupt(CPU::INTERRUPT_NMI);
			}
			//Set vblank flag
			refMM[PPUSTATUS] |= 0x80;

			//refGLM.update_screen();

			//Signal to the video thread to draw the next frame
			refCore.shouldDrawFrame = true;
		}

	} else {
		regs->pixelCounter++;
	}
}

FORCEINLINE const dword PPU::get_NT_pixel() {

	word	tmpX = (word)regs->pixelCounter + (word)regs->fineXOffset,
				tmpY = (word)regs->scanlineCounter + (word)regs->fineYOffset,
				tileAddr, ntAddr, patternAddr, ntOffset = regs->nameTableBaseAddr; //Initialize to the current NT

	bool xOverflow = false;

	if (tmpX >= PIXEL_LIMIT) {					//We will get a seam btwn the NTs if we don't check for equality
		xOverflow = true;

		switch (regs->mirroringType) {
		case MIRROR_HORIZONTAL:		//Roll back to start of same NT
			ntOffset = regs->nameTableBaseAddr;
			break;
		case MIRROR_VERTICAL:			//Roll over to next NT
			switch (regs->nameTableBaseAddr) {
			case NTADDR_A:
				ntOffset = NTADDR_B;
				break;
			case NTADDR_B:
				ntOffset = NTADDR_A;
				break;
			case NTADDR_C:
				ntOffset = NTADDR_D;
				break;
			case NTADDR_D:
				ntOffset = NTADDR_C;
				break;
			default:
				refCore.alert_error("Invalid value in PPU nameTableBaseAddr register; must be one of: 0x2000, 0x2400, 0x2800, or 0x2C00");
				break;
			}

			break;
		default:
			refCore.alert_error("Invalid mirroring type detected");
			break;
		}
		tmpX = regs->pixelCounter - (PIXEL_LIMIT - regs->fineXOffset); //Take offset off of tmpX
	}

	if (tmpY >= SCANLINE_LIMIT) {			//Will get a seam btwn NTs if we don't check for equality
		switch (regs->mirroringType) {
		case MIRROR_HORIZONTAL:		//Roll over to next NT
			switch (regs->nameTableBaseAddr) {
			case NTADDR_A:
				ntOffset = NTADDR_C;
				break;
			case NTADDR_B:
				ntOffset = NTADDR_D;
				break;
			case NTADDR_C:
				ntOffset = NTADDR_A;
				break;
			case NTADDR_D:
				ntOffset = NTADDR_B;
				break;
			default:
				refCore.alert_error("Invalid value in PPU nameTableBaseAddr register; must be one of: 0x2000, 0x2400, 0x2800, or 0x2C00");
				break;
			}
			break;
		case MIRROR_VERTICAL:			//Roll back to start of same NT ONLY if we have not overflowed on tmpX (otherwise the NT will incorrectly wrap on the X axis)
			if (!xOverflow) {				//Note that this is a little hacky...
				ntOffset = regs->nameTableBaseAddr;
			}
			break;
		default:
			refCore.alert_error("Invalid mirroring type detected");
			break;
		}
		tmpY = regs->scanlineCounter - (SCANLINE_LIMIT - regs->fineYOffset); //Take offset off of tmpX.
		/*
			IMPORTANT:
				The -16 here is due to the fact that there are 240 scanlines as opposed to 256. This means that tmpY must account for this discrepancy when overflowing,
				otherwise the NT will be drawn 16 scanlines too high. The -16 also ensures that the tileAddr will be correct by making sure that the NT will wrap/crossover
				to the top of the same/next NT instead of incorrectly accessing the attribute tables as NT data.

		*/
		//if (regs->mirroringType == MIRROR_HORIZONTAL) { tmpY += 16; }

	} /* if (tmpY > SCANLINE_LIMIT) */

	tileAddr = (tmpX >> 3) + ((tmpY >> 3) << 5); //Divide pixel counter by 8, divide scanline counter by 8 then mult. by 32
	ntAddr = tileAddr + ntOffset;
	patternAddr = (refVM[ntAddr] * 16) + regs->patternTableOffset; //New pattern every 16 bytes

																																 //Which row of the pattern entry to use
	byte patternYIndex = tmpY & 0x07;

	//Which bit in the row to use
	byte tmpMask;

	switch (tmpX & 0x07) {
	case 0:
		tmpMask = 0x80;
		break;
	case 1:
		tmpMask = 0x40;
		break;
	case 2:
		tmpMask = 0x20;
		break;
	case 3:
		tmpMask = 0x10;
		break;
	case 4:
		tmpMask = 0x08;
		break;
	case 5:
		tmpMask = 0x04;
		break;
	case 6:
		tmpMask = 0x02;
		break;
	case 7:
		tmpMask = 0x01;
		break;
	}

	byte lobit = (refVM[patternAddr + patternYIndex] & tmpMask) ? 0x01 : 0x00;
	byte hibit = refVM[patternAddr + patternYIndex + 8] & tmpMask ? 0x02 : 0x00;
	byte colorSelect = lobit | hibit;

	//Return the universal background color if 0; no need for attr. logic
	if (colorSelect == 0) {
		return NES_COLOR_PALETTE[refVM[UNIVERSAL_BACKGROUND_ADDR] & 0x3F] | TRANSPARENT_BACKGROUND;  //OR in meta info
	}

	//Getting the attribute entry is a pain in the ass :/

	//First, we coerce the offset of the name table entry to an index into the corresponding attribute table.
	//Since there are 32 tiles are in 1 row, and each byte maps to 4x4 tiles, every 128 tiles maps to
	//one row of the attribute table

	//32x32 attr tiles, so we rsh by 5 (same as divide by 32)
	word attrHi = tmpY >> 5, attrLo = tmpX >> 5;
	//Convert the shifted counters into an index into the 8x8 attr table
	word attrIdx = (attrHi << 3) | attrLo;
	//word attrEntryOffset = ((tileAddr / 128) * 8) + ((tileAddr & 0x1F) / 4);
	word attrEntryAddr = attrIdx + 0x3C0 /*Attr table starts at 0x3C0th NT byte*/ + ntOffset;
	//attrEntryAddr = coercePPUAddress(attrEntryAddr);
	byte attribByte = refVM[attrEntryAddr];

	//Then, we have to figure out which quadrant the entry will use:
	//Using the result, we can determine how to mask the byte we retrieved from the attribute table,
	//which will select the palette to use for the pattern we got from the NT
	//byte locationInfo = attrEntryOffset & 0x42; //01000010
	byte paletteSelection;

	byte locationInfo = ((tmpX & 0x10) >> 4) |  //Pixel counter has bit 5 set is the tile is on the bottom of the attr entry
		((tmpY & 0x10) >> 3); // //Pixel counter has bit 5 set is the tile is on the right of the attr entry

	switch (locationInfo) {
	case 0x00:
		paletteSelection = attribByte & 0x03; //Top left
		break;

	case 0x01:
		paletteSelection = (attribByte & 0x0C) >> 2; //Top right
		break;

	case 0x02:
		paletteSelection = (attribByte & 0x30) >> 4; //Bottom left
		break;

	case 0x03:
		paletteSelection = (attribByte & 0xC0) >> 6; //Bottom right
		break;
	}

	word basePaletteAddr;

	switch (paletteSelection) {
	case 0:
		basePaletteAddr = BACKGROUND_PALETTE_ZERO;
		break;
	case 1:
		basePaletteAddr = BACKGROUND_PALETTE_ONE;
		break;
	case 2:
		basePaletteAddr = BACKGROUND_PALETTE_TWO;
		break;
	case 3:
		basePaletteAddr = BACKGROUND_PALETTE_THREE;
		break;
	}

	//FINALLY, get the damn color!
	switch (colorSelect) {
	case 1:
		return NES_COLOR_PALETTE[refVM[basePaletteAddr] & 0x3F] | OPAQUE_BACKGROUND;
		break;
	case 2:
		return NES_COLOR_PALETTE[refVM[basePaletteAddr + 1] & 0x3F] | OPAQUE_BACKGROUND;
		break;
	case 3:
		return NES_COLOR_PALETTE[refVM[basePaletteAddr + 2] & 0x3F] | OPAQUE_BACKGROUND;
		break;
	}
}


/*
Retrieve the sprite pixel to draw.

The algorithm is as follows:

1) Return NULL_SPRITE if the first sprite in currentSprites is NULL_SPRITE (no sprites on the current scanline).

2) Find the first sprite in currentSprites that is in range of the current pixel; return NULL_SPRITE if none are in range.

3) Assess sprite attributes, and find the appropriate pattern data using an algorithm determined by the flip type.

4) Return the universal background color if the pattern data is 00, otherwise use the retrieved attributes to determine the color ro return.
*/
FORCEINLINE const dword PPU::get_SPR_pixel() {
	if (currentSprites[0] == NULL_SPRITE) { //Don't bother if no sprites on the current scanline
		return NULL_SPRITE;
	}

	int diff, i = 0;
	bool foundSprite = false;
	byte currentIdx, idxToDraw;

	//TODO: replace this ugly goto & label!
FINDSPRITE:

	for (; i < 8; i++) {
		currentIdx = currentSprites[i];
		if (currentSprites[i] == NULL_SPRITE) { //NULL_SPRITEs will incorrectly trigger the diff condition below, and cause 'ghost' sprites to appear on the left of the screen
			continue;
		}

		diff = ((dword)refBus.OAM[currentIdx + 3] + 7) - (regs->pixelCounter);		//Offset xPos by 7 to display the sprite in the right place

		if ((diff >= 0) && (diff < 8)) { //Is the X coord in range?
			idxToDraw = currentIdx;

			foundSprite = true;
			break;
		}
	}

	if (!foundSprite) {
		return NULL_SPRITE;
	}

	//8x16 sprites can use either pattern table: they ignore the spritePatternTableOffset and instead go by bit 0 of the second byte in the OAM entry.
	//8x16 patterns in the 0x1000 pattern table need to be one entry lower (i.e. 16 bytes) less in this implementation (TODO: WHY?).
	word tmpPatternOffset;
	bool patternCorrection;
	if (regs->spriteSizeIs8x8) {
		tmpPatternOffset = regs->spritePatternTableOffset;
		patternCorrection = false;
	} else {
		tmpPatternOffset = (refBus.OAM[idxToDraw + 1] & 0x01) ? 0x1000 : 0;
		if (refBus.OAM[idxToDraw + 1] & 0x01) {
			tmpPatternOffset = 0x1000;
			patternCorrection = true;
		} else {
			tmpPatternOffset = 0;
			patternCorrection = false;
		}
	}

	//Extract sprite info from OAM.
	word	yPos = refBus.OAM[idxToDraw] + 1,
		patternAddr = (refBus.OAM[idxToDraw + 1] * 16) + tmpPatternOffset,
		tmpAttrs = refBus.OAM[idxToDraw + 2],
		xPos = refBus.OAM[idxToDraw + 3] + 7;	//Offset xPos by 7 to display the sprite in the right place

	byte colorSelect;

	//Go to next bitmap when rendering lower half of an 8x16 sprite
	if ((!(regs->spriteSizeIs8x8)) && ((regs->scanlineCounter - yPos) >= 8)) {
		patternAddr += 16;
	}

	if (patternCorrection) {
		patternAddr -= 16;
	}

	//Get pattern data according to flip type
	switch (tmpAttrs & 0xC0) { //Bit 6 -> flip horizontal; bit 7 -> flip vertical
	case 0:
		colorSelect = get_pattern_no_flip(xPos, yPos, patternAddr);
		break;
	case 0x40:
		colorSelect = get_pattern_flip_horizontal(xPos, yPos, patternAddr);
		break;
	case 0x80:
		colorSelect = get_pattern_flip_vertical(xPos, yPos, patternAddr);
		break;
	case 0xC0:
		colorSelect = get_pattern_flip_horizontal_and_vertical(xPos, yPos, patternAddr);
		break;
	}

	//Return the universal background color if 0; no need for attr. logic
	if (colorSelect == 0) {
		//An opaque sprite behind a transparent sprite must be chosen!
		//TODO: get rid of this ugly goto!
		if (i < 8) {
			i++;
			goto FINDSPRITE;
		}
		return NES_COLOR_PALETTE[refVM[UNIVERSAL_BACKGROUND_ADDR] & 0x3F] | TRANSPARENT_SPRITE;  //OR in meta info
	}

	word basePaletteAddr;

	switch (tmpAttrs & 0x03) {
	case 0:
		basePaletteAddr = SPRITE_PALETTE_ZERO;
		break;
	case 1:
		basePaletteAddr = SPRITE_PALETTE_ONE;
		break;
	case 2:
		basePaletteAddr = SPRITE_PALETTE_TWO;
		break;
	case 3:
		basePaletteAddr = SPRITE_PALETTE_THREE;
		break;
	}

	dword wordOut;

	switch (colorSelect) {
	case 1:
		wordOut = NES_COLOR_PALETTE[refVM[basePaletteAddr] & 0x3F];
		break;
	case 2:
		wordOut = NES_COLOR_PALETTE[refVM[basePaletteAddr + 1] & 0x3F];
		break;
	case 3:
		wordOut = NES_COLOR_PALETTE[refVM[basePaletteAddr + 2] & 0x3F];
		break;
	}

	//Catch if we are returning an opaque sprite0 pixel
	if (idxToDraw == 0) {
		return wordOut | SPRITE_ZERO_OPAQUE;
	}

	//Finally, return the pixel ORed with the priority state
	if (!(tmpAttrs & 0x20)) { //0 means in FRONT of background
		return wordOut | OPAQUE_SPRITE_PRIORITY_FOREGROUND;
	} else {
		return wordOut | OPAQUE_SPRITE;
	}

}

FORCEINLINE const dword PPU::multiplex(const dword ntPixel, const dword sprPixel) {
	if (sprPixel == NULL_SPRITE) {
		return ntPixel;
	}

	//Sprite zero hit when opaque sprite0 overlaps opaque background
	if (((sprPixel & 0xFF) == SPRITE_ZERO_OPAQUE) && ((ntPixel & 0xFF) == OPAQUE_BACKGROUND)) {
		refMM[PPUSTATUS] |= 0x40;	//Set sprite0 hit flag

															//Draw the sprite0 pixel only if it is priority
		byte tmpAttrs = refBus.OAM[2];
		if (tmpAttrs & 0x20) {
			return ntPixel;
		} else {
			return sprPixel;
		}
	}

	//For a sprite pixel to be displayed, it must not be transparent, and it either needs to hold priority or be against a transparent background
	if ((((sprPixel & 0xFF) == OPAQUE_SPRITE_PRIORITY_FOREGROUND) || ((ntPixel & 0xFF) == TRANSPARENT_BACKGROUND)) && ((sprPixel & 0xFF) != TRANSPARENT_SPRITE)) {
		return sprPixel;

	} else {
		return ntPixel;
	}
}

//To flip across y axis: reverse bitmap order
//To flip across x axis: do ((yPos - regs->scanlineCounter) & 0x07) for patternYIndex

//TODO: DRY these 4 functions up

FORCEINLINE const byte PPU::get_pattern_no_flip(const word xPos, const word yPos, word patternAddr) {
	//Which row of the pattern entry to use (diff btwn sprite Y and current scanline)
	byte patternYIndex = (regs->scanlineCounter - yPos) & 0x07;

	//Which bit in the row to use
	byte tmpMask;

	switch ((xPos - regs->pixelCounter) & 0x07) {
	case 0:
		tmpMask = 0x01; //Use leftmost bit if diff(xPos, pixelCounter) == 0
		break;
	case 1:
		tmpMask = 0x02;
		break;
	case 2:
		tmpMask = 0x04;
		break;
	case 3:
		tmpMask = 0x08;
		break;
	case 4:
		tmpMask = 0x10;
		break;
	case 5:
		tmpMask = 0x20;
		break;
	case 6:
		tmpMask = 0x40;
		break;
	case 7:
		tmpMask = 0x80;
		break;
	}

	byte lobit = (refVM[patternAddr + patternYIndex] & tmpMask) ? 0x01 : 0x00;
	byte hibit = refVM[patternAddr + patternYIndex + 8] & tmpMask ? 0x02 : 0x00;
	return lobit | hibit;
}

FORCEINLINE const byte PPU::get_pattern_flip_horizontal(const word xPos, const word yPos, word patternAddr) {
	//Which row of the pattern entry to use (diff btwn sprite Y and current scanline)
	byte patternYIndex = (regs->scanlineCounter - yPos) & 0x07;

	//Which bit in the row to use
	byte tmpMask;

	switch ((xPos - regs->pixelCounter) & 0x07) {
	case 0:
		tmpMask = 0x80;
		break;
	case 1:
		tmpMask = 0x40;
		break;
	case 2:
		tmpMask = 0x20;
		break;
	case 3:
		tmpMask = 0x10;
		break;
	case 4:
		tmpMask = 0x08;
		break;
	case 5:
		tmpMask = 0x04;
		break;
	case 6:
		tmpMask = 0x02;
		break;
	case 7:
		tmpMask = 0x01;
		break;
	}

	byte lobit = (refVM[patternAddr + patternYIndex] & tmpMask) ? 0x01 : 0x00;
	byte hibit = refVM[patternAddr + patternYIndex + 8] & tmpMask ? 0x02 : 0x00;
	return lobit | hibit;
}

FORCEINLINE const byte PPU::get_pattern_flip_vertical(const word xPos, const word yPos, word patternAddr) {
	//Which row of the pattern entry to use (diff btwn sprite Y and current scanline)

	//Add 1 to make sure correct bitmap is selected when flipping vertically
	//TODO: find a less hacky way to do this if possible
	byte patternYIndex = (yPos - (regs->scanlineCounter + 1)) & 0x07;

	if (!(regs->spriteSizeIs8x8)) {
		//Reverse the order of the two bitmaps when flipping an 8x16 sprite vertically
		if ((regs->scanlineCounter - yPos) >= 8) {
			patternAddr -= 16;
		} else {
			patternAddr += 16;
		}
	}

	//Which bit in the row to use
	byte tmpMask;

	switch ((xPos - regs->pixelCounter) & 0x07) {
	case 0:
		tmpMask = 0x01; //Use leftmost bit if diff(xPos, pixelCounter) == 0
		break;
	case 1:
		tmpMask = 0x02;
		break;
	case 2:
		tmpMask = 0x04;
		break;
	case 3:
		tmpMask = 0x08;
		break;
	case 4:
		tmpMask = 0x10;
		break;
	case 5:
		tmpMask = 0x20;
		break;
	case 6:
		tmpMask = 0x40;
		break;
	case 7:
		tmpMask = 0x80;
		break;
	}

	byte lobit = (refVM[patternAddr + patternYIndex] & tmpMask) ? 0x01 : 0x00;
	byte hibit = refVM[patternAddr + patternYIndex + 8] & tmpMask ? 0x02 : 0x00;
	return lobit | hibit;
}

FORCEINLINE const byte PPU::get_pattern_flip_horizontal_and_vertical(const word xPos, const word yPos, word patternAddr) {
	//Which row of the pattern entry to use (diff btwn sprite Y and current scanline)
	//Add 1 to make sure correct bitmap is selected when flipping vertically
	//TODO: find a less hacky way to do this if possible
	byte patternYIndex = (yPos - (regs->scanlineCounter + 1)) & 0x07;

	if (!(regs->spriteSizeIs8x8)) {
		//Reverse the order of the two bitmaps when flipping an 8x16 sprite vertically
		if ((regs->scanlineCounter - yPos) >= 8) {
			patternAddr -= 16;
		} else {
			patternAddr += 16;
		}
	}

	//Which bit in the row to use
	byte tmpMask;

	switch ((xPos - regs->pixelCounter) & 0x07) {
	case 0:
		tmpMask = 0x80;
		break;
	case 1:
		tmpMask = 0x40;
		break;
	case 2:
		tmpMask = 0x20;
		break;
	case 3:
		tmpMask = 0x10;
		break;
	case 4:
		tmpMask = 0x08;
		break;
	case 5:
		tmpMask = 0x04;
		break;
	case 6:
		tmpMask = 0x02;
		break;
	case 7:
		tmpMask = 0x01;
		break;
	}

	byte lobit = (refVM[patternAddr + patternYIndex] & tmpMask) ? 0x01 : 0x00;
	byte hibit = refVM[patternAddr + patternYIndex + 8] & tmpMask ? 0x02 : 0x00;
	return lobit | hibit;
}