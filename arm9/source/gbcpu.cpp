#ifndef DS
#include <string.h>
#endif
#include <stdio.h>
#include "gbcpu.h"
#include "mmu.h"
#include "gbgfx.h"
#include "gbsnd.h"
#include "gameboy.h"
#include "main.h"
#ifdef DS
#include <nds.h>
#endif

#define setZFlag()		af.b.l |= 0x80
#define clearZFlag()	af.b.l &= 0x7F
#define setNFlag()		af.b.l |= 0x40
#define clearNFlag()	af.b.l &= 0xBF
#define setHFlag()		af.b.l |= 0x20
#define clearHFlag()	af.b.l &= 0xDF
#define setCFlag()		af.b.l |= 0x10
#define clearCFlag()	af.b.l &= 0xEF
#define carrySet() 	(af.b.l & 0x10 ? 1 : 0)
#define zeroSet()	(af.b.l & 0x80 ? 1 : 0)
#define negativeSet()	(af.b.l & 0x40 ? 1 : 0)
#define halfSet()		(af.b.l & 0x20 ? 1 : 0)

inline u8 quickRead(u16 addr) {
    return memory[addr>>12][addr&0xFFF];
}
inline u16 quickRead16(u16 addr) {
    return quickRead(addr)|(quickRead(addr+1)<<8);
}
inline void quickWrite(u16 addr, u8 val) {
    memory[addr>>12][addr&0xFFF] = val;
}

u16 gbSP,gbPC;
int halt;

u8 buttonsPressed = 0xff;
int fps;
int gbMode;

Register af;
Register bc;
Register de;
Register hl;

// IMPORTANT: This variable is unchanging, it DOES NOT change in double speed mode!
const int clockSpeed = 4194304;

extern int halt;
int ime;

u8 opCycles[0x100]
#ifdef DS
DTCM_DATA
#endif
    = {
    /* Low nybble -> */
    /* High nybble v */
           /*  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, A, B, C, D, E, F  */
    /* 0X */   4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4,
    /* 1X */   4,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4,
    /* 2X */  12,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4,
    /* 3X */  12,12, 8, 8,12,12,12, 4,12, 8, 8, 8, 4, 4, 8, 4,
    /* 4X */   4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    /* 5X */   4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    /* 6X */   4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    /* 7X */   8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,
    /* 8X */   4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    /* 9X */   4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    /* AX */   4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    /* BX */   4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    /* CX */  16,12,16,16,24,16, 8,16,16,16,16, 0,24,24, 8,16,
    /* DX */  16,12,16,99,24,16, 8,16,16,16,16,99,24,99, 8,16,
    /* EX */  12,12, 8,99,99,16, 8,16,16, 4,16,99,99,99, 8,16,
    /* FX */  12,12, 8, 4,99,16, 8,16,12, 8,16, 4,99,99, 8,16
    /* opcodes that have 99 cycles are undefined, but don't hang on them */
};

u8 CBopCycles[0x100]
#ifdef DS
DTCM_DATA
#endif
    = {
    /* Low nybble -> */
    /* High nybble v */
           /*  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, A, B, C, D, E, F  */
    /* 0X */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* 1X */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* 2X */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* 3X */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* 4X */   8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
    /* 5X */   8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
    /* 6X */   8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
    /* 7X */   8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
    /* 8X */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* 9X */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* AX */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* BX */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* CX */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* DX */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* EX */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,
    /* FX */   8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8
};

void initCPU()
{
	int i;
	gbSP = 0xFFFE;
	ime = 1;			// Correct default value?

	MBC = readMemory(0x147);
	if (MBC == 0)
	{
		MBC = 0;
		printLog("This game doesn't use ROM banking.\n");
	}
	else if (MBC <= 3)
	{
		MBC = 1;
		printLog("This game uses MBC1.\n");
	}
	else if (MBC <= 6 && MBC >= 5)
	{
		MBC = 2;
		printLog("This game uses MBC2.\n");
	}
	else if (MBC == 0x10 || MBC == 0x12 || MBC == 0x13)
	{
		MBC = 3;
		printLog("This game uses MBC3.\n");
	}
	else if ((MBC >= 0x19 && MBC <= 0x1E))
	{
		printLog("This game uses MBC5.\n");
		MBC = 5;
	}
	else
	{
		printLog("This game uses unknown MBC! %x\n", MBC);
		MBC = 5;
	}

    halt = 0;
    doubleSpeed = 0;

    af.w = 0x11B0;
    bc.w = 0x0013;
    de.w = 0x00D8;
    hl.w = 0x014D;

	writeMemory(0xFF05, 0x00);
	writeMemory(0xFF06, 0x00);
	writeMemory(0xFF07, 0x00);
	writeMemory(0xFF10, 0x80);
	writeMemory(0xFF11, 0xBF);
	writeMemory(0xFF12, 0xF3);
	writeMemory(0xFF14, 0xBF);
	writeMemory(0xFF16, 0x3F);
	writeMemory(0xFF17, 0x00);
	writeMemory(0xFF19, 0xbf);
	writeMemory(0xFF1a, 0x7f);
	writeMemory(0xFF1b, 0xff);
	writeMemory(0xFF1c, 0x9f);
	writeMemory(0xFF1e, 0xbf);
	writeMemory(0xFF20, 0xff);
	writeMemory(0xFF21, 0x00);
	writeMemory(0xFF22, 0x00);
	writeMemory(0xFF23, 0xbf);
	writeMemory(0xFF24, 0x77);
	writeMemory(0xFF25, 0xf3);
	writeMemory(0xFF26, 0xf1);
	writeMemory(0xFF40, 0x91);
	writeMemory(0xFF42, 0x00);
	writeMemory(0xFF43, 0x00);
	writeMemory(0xFF45, 0x00);
	writeMemory(0xFF47, 0xfc);
	writeMemory(0xFF48, 0xff);
	writeMemory(0xFF49, 0xff);
	writeMemory(0xFF4a, 0x00);
	writeMemory(0xFF4b, 0x00);
	writeMemory(0xFFff, 0x00);
    // writeMemory shouldn't interfere in this case
    ioRam[0x4d] = 0;

    biosOn = biosEnabled;
	if (biosOn)
	{
		gbPC = 0;
		gbMode = CGB;
	}
	else
	{
		gbPC = 0x100;
		if (rom[0][0x143] == 0x80 || rom[0][0x143] == 0xC0)
			gbMode = CGB;
		else
			gbMode = GB;
	}
}

void enableInterrupts()
{
	ime = 1;
    if (ioRam[0x0f] & ioRam[0xff])
        cyclesToExecute = 0;
}

void disableInterrupts()
{
	ime = 0;
}

void handleInterrupts()
{
    int interruptTriggered = ioRam[0x0F] & ioRam[0xFF];
    if (interruptTriggered & VBLANK)
	{
		halt = 0;
        if (ime) {
            writeMemory(--gbSP, (gbPC) >> 8);
            writeMemory(--gbSP, gbPC & 0xFF);
            gbPC = 0x40;
            ioRam[0x0F] &= ~VBLANK;
            ime = 0;
        }
	}
    else if (interruptTriggered & LCD)
	{
		halt = 0;
        if (ime) {
            writeMemory(--gbSP, (gbPC) >> 8);
            writeMemory(--gbSP, gbPC & 0xFF);
            gbPC = 0x48;
            ioRam[0x0F] &= ~LCD;
            ime = 0;
        }
	}
    else if (interruptTriggered & TIMER)
	{
		halt = 0;
        if (ime) {
            writeMemory(--gbSP, (gbPC) >> 8);
            writeMemory(--gbSP, gbPC & 0xFF);
            gbPC = 0x50;
            ioRam[0x0F] &= ~TIMER;
            ime = 0;
        }
	}
	// Serial IO would go here
	else if (interruptTriggered & JOYPAD)
	{
        halt = 0;
        if (ime) {
            writeMemory(--gbSP, (gbPC) >> 8);
            writeMemory(--gbSP, gbPC & 0xFF);
            gbPC = 0x60;
            ioRam[0x0F] &= ~JOYPAD;
            ime = 0;
            interruptTriggered = 0;
        }
    }
}

u8* const reg8[] = {&bc.b.h,&bc.b.l,&de.b.h,&de.b.l,&hl.b.h,&hl.b.l,0,&af.b.h};

int cyclesToExecute;

#ifdef DS
int runOpcode(int cycles) ITCM_CODE;
#endif

int runOpcode(int cycles) {
    cyclesToExecute = cycles;
    // Having these commonly-used registers in local variables should improve speed
    u16 locPC=gbPC;
    u16 locSP=gbSP;

	int totalCycles=0;

	while (totalCycles < cyclesToExecute)
	{
		u8 opcode = quickRead(locPC++);
		totalCycles += opCycles[opcode];

		switch(opcode)
		{
			// 8-bit loads
			case 0x06:		// LD B, n		8
			case 0x0E:		// LD C, n		8
			case 0x16:		// LD D, n		8
			case 0x1E:		// LD E, n		8
			case 0x26:		// LD H, n		8
			case 0x2E:		// LD L, n		8
			case 0x3E:		// LD A, n		8
				(*reg8[opcode>>3]) = quickRead(locPC++);
				break;
			case 0x7F:		// LD A, A		4
			case 0x78:		// LD A, B		4
			case 0x79:		// LD A, C		4
			case 0x7A:		// LD A, D		4
			case 0x7B:		// LD A, E		4
			case 0x7C:		// LD A, H		4
			case 0x7D:		// LD A, L		4
			case 0x40:		// LD B, B		4
			case 0x41:		// LD B, C		4
			case 0x42:		// LD B, D		4
			case 0x43:		// LD B, E		4
			case 0x44:		// LD B, H		4
			case 0x45:		// LD B, L		4
			case 0x48:		// LD C, B		4
			case 0x49:		// LD C, C		4
			case 0x4A:		// LD C, D		4
			case 0x4B:		// LD C, E		4
			case 0x4C:		// LD C, H		4
			case 0x4D:		// LD C, L		4
			case 0x50:		// LD D, B		4
			case 0x51:		// LD D, C		4
			case 0x52:		// LD D, D		4
			case 0x53:		// LD D, E		4
			case 0x54:		// LD D, H		4
			case 0x55:		// LD D, L		4
			case 0x58:		// LD E, B		4
			case 0x59:		// LD E, C		4
			case 0x5A:		// LD E, D		4
			case 0x5B:		// LD E, E		4
			case 0x5C:		// LD E, H		4
			case 0x5D:		// LD E, L		4
			case 0x60:		// LD H, B		4
			case 0x61:		// LD H, C		4
			case 0x62:		// LD H, D		4
			case 0x63:		// LD H, E		4
			case 0x64:		// LD H, H		4
			case 0x65:		// LD H, L		4
			case 0x68:		// LD L, B		4
			case 0x69:		// LD L, C		4
			case 0x6A:		// LD L, D		4
			case 0x6B:		// LD L, E		4
			case 0x6C:		// LD L, H		4
			case 0x6D:		// LD L, L		4
			case 0x47:		// LD B, A		4
			case 0x4F:		// LD C, A		4
			case 0x57:		// LD D, A		4
			case 0x5F:		// LD E, A		4
			case 0x67:		// LD H, A		4
			case 0x6F:		// LD L, A		4
                (*reg8[(opcode>>3)&7]) = *reg8[opcode&7];
                break;
			case 0x7E:		// LD A, (hl)	8
			case 0x46:		// LD B, (hl)	8
			case 0x4E:		// LD C, (hl)	8
			case 0x56:		// LD D, (hl)	8
			case 0x5E:		// LD E, (hl)	8
			case 0x66:		// LD H, (hl)	8
			case 0x6E:		// LD L, (hl)	8
                (*reg8[(opcode>>3)&7]) = readMemory(hl.w);
                break;
			case 0x77:		// LD (hl), A	8
			case 0x70:		// LD (hl), B	8
			case 0x71:		// LD (hl), C	8
			case 0x72:		// LD (hl), D	8
			case 0x73:		// LD (hl), E	8
			case 0x74:		// LD (hl), H	8
			case 0x75:		// LD (hl), L	8
                writeMemory(hl.w, *reg8[opcode&7]);
                break;
			case 0x36:		// LD (hl), n	12
				writeMemory(hl.w, quickRead(locPC++));
				break;
			case 0x0A:		// LD A, (BC)	8
				af.b.h = readMemory(bc.w);
				break;
			case 0x1A:		// LD A, (de)	8
				af.b.h = readMemory(de.w);
				break;
			case 0xFA:		// LD A, (nn)	16
				af.b.h = readMemory(quickRead(locPC) | (quickRead(locPC+1) << 8));
				locPC += 2;
				break;
			case 0x02:		// LD (BC), A	8
				writeMemory(bc.w, af.b.h);
				break;
			case 0x12:		// LD (de), A	8
				writeMemory(de.w, af.b.h);
				break;
			case 0xEA:		// LD (nn), A	16
				writeMemory(quickRead(locPC) | (quickRead(locPC+1) << 8), af.b.h);
				locPC += 2;
				break;
			case 0xF2:		// LD A, (C)	8
				af.b.h = readMemory(0xFF00 + bc.b.l);
				break;
			case 0xE2:		// LD (C), A	8
				writeMemory(0xFF00 + bc.b.l, af.b.h);
				break;
			case 0x3A:		// LDD A, (hl)	8
				af.b.h = readMemory(hl.w--);
				break;
			case 0x32:		// LDD (hl), A	8
				writeMemory(hl.w--, af.b.h);
				break;
			case 0x2A:		// LDI A, (hl)	8
				af.b.h = readMemory(hl.w++);
				break;
			case 0x22:		// LDI (hl), A	8
				writeMemory(hl.w++, af.b.h);
				break;
			case 0xE0:		// LDH (n), A   12
				writeMemory(0xFF00 + quickRead(locPC++), af.b.h);
				break;
			case 0xF0:		// LDH A, (n)   12
				af.b.h = readMemory(0xFF00 + quickRead(locPC++));
				break;

				// 16-bit loads

			case 0x01:		// LD BC, nn	12
				bc.b.l = quickRead(locPC++);
				bc.b.h = quickRead(locPC++);
				break;
			case 0x11:		// LD de, nn	12
				de.b.l = quickRead(locPC++);
				de.b.h = quickRead(locPC++);
				break;
			case 0x21:		// LD hl, nn	12
				hl.b.l = quickRead(locPC++);
				hl.b.h = quickRead(locPC++);
				break;
			case 0x31:		// LD SP, nn	12
				locSP = quickRead(locPC) | (quickRead(locPC+1) << 8);
				locPC += 2;
				break;
			case 0xF9:		// LD SP, hl	8
				locSP = hl.w;
				break;
			case 0xF8:		// LDHL SP, n   12
                {
				int sval = (s8)quickRead(locPC++);
				int val = (locSP + sval)&0xFFFF;
				if ((sval >= 0 && locSP > val))// || (sval<0 && locSP < val))
					setCFlag();
				else
					clearCFlag();
				if ((locSP&0xFFF)+(sval) >= 0x1000)// || (locSP&0xF)+(sval&0xF) < 0)
					setHFlag();
				else
					clearHFlag();
				clearZFlag();
				clearNFlag();
				hl.w = val;
				break;
                }
			case 0x08:		// LD (nn), SP	20
                {
				int val = quickRead(locPC) | (quickRead(locPC+1) << 8);
				locPC += 2;
				writeMemory(val, locSP & 0xFF);
				writeMemory(val+1, (locSP) >> 8);
				break;
                }
			case 0xF5:		// PUSH AF
				quickWrite(--locSP, af.b.h);
				quickWrite(--locSP, af.b.l);
				break;
			case 0xC5:		// PUSH BC			16
				quickWrite(--locSP, bc.b.h);
				quickWrite(--locSP, bc.b.l);
				break;
			case 0xD5:		// PUSH de			16
				quickWrite(--locSP, de.b.h);
				quickWrite(--locSP, de.b.l);
				break;
			case 0xE5:		// PUSH hl			16
				quickWrite(--locSP, hl.b.h);
				quickWrite(--locSP, hl.b.l);
				break;
			case 0xF1:		// POP AF				12
				af.b.l = quickRead(locSP++);
				af.b.h = quickRead(locSP++);
				af.b.l &= 0xF0;
				break;
			case 0xC1:		// POP BC				12
				bc.b.l = quickRead(locSP++);
				bc.b.h = quickRead(locSP++);
				break;
			case 0xD1:		// POP de				12
				de.b.l = quickRead(locSP++);
				de.b.h = quickRead(locSP++);
				break;
			case 0xE1:		// POP hl				12
				hl.b.l = quickRead(locSP++);
				hl.b.h = quickRead(locSP++);
				break;

				// 8-bit arithmetic
			case 0x87:		// ADD A, A			4
			case 0x80:		// ADD A, B			4
			case 0x81:		// ADD A, C			4
			case 0x82:		// ADD A, D			4
			case 0x83:		// ADD A, E			4
			case 0x84:		// ADD A, H			4
			case 0x85:		// ADD A, L			4
                {
                    u8 r = *reg8[opcode&7];
                    if (af.b.h + r > 0xFF)
                        setCFlag();
                    else
                        clearCFlag();
                    if ((af.b.h & 0xF) + (r & 0xF) > 0xF)
                        setHFlag();
                    else
                        clearHFlag();
                    af.b.h += r;
                    if (af.b.h == 0)
                        setZFlag();
                    else
                        clearZFlag();
                    clearNFlag();
                    break;
                }
			case 0x86:		// ADD A, (hl)	8
                {
				int val = readMemory(hl.w);
				if (af.b.h + val > 0xFF)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) + (val & 0xF) > 0xF)
					setHFlag();
				else
					clearHFlag();
				af.b.h += val;
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				break;
                }
			case 0xC6:		// ADD A, n			8
                {
				int val = quickRead(locPC++);
				if (af.b.h + val > 0xFF)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) + (val & 0xF) > 0xF)
					setHFlag();
				else
					clearHFlag();
				af.b.h += val;
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				break;
                }


			case 0x8F:		// ADC A, A			4
			case 0x88:		// ADC A, B			4
			case 0x89:		// ADC A, C			4
			case 0x8A:		// ADC A, D			4
			case 0x8B:		// ADC A, E			4
			case 0x8C:		// ADC A, H			4
			case 0x8D:		// ADC A, L			4
                {
				int val = carrySet();
                u8 r = *reg8[opcode&7];
				if (af.b.h + r + val > 0xFF)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) + (r & 0xF) + val > 0xF)
					setHFlag();
				else
					clearHFlag();
				af.b.h += r + val;
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				break;
                }
			case 0x8E:		// ADC A, (hl)	8
                {
				int val = readMemory(hl.w);
				int val2 = carrySet();
				if (af.b.h + val + val2 > 0xFF)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) + (val & 0xF) + val2 > 0xF)
					setHFlag();
				else
					clearHFlag();
				af.b.h += val + val2;
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				break;
                }
			case 0xCE:		// ADC A, n			8
                {
				int val = quickRead(locPC++);
				int val2 = carrySet();
				if (af.b.h + val + val2 > 0xFF)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) + (val & 0xF) + val2 > 0xF)
					setHFlag();
				else
					clearHFlag();
				af.b.h += val + val2;
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				break;
                }

			case 0x97:		// SUB A, A			4
			case 0x90:		// SUB A, B			4
			case 0x91:		// SUB A, C			4
			case 0x92:		// SUB A, D			4
			case 0x93:		// SUB A, E			4
			case 0x94:		// SUB A, H			4
			case 0x95:		// SUB A, L			4
                {
                    u8 r = *reg8[opcode&7];
                    if (af.b.h < r)
                        setCFlag();
                    else
                        clearCFlag();
                    if ((af.b.h & 0xF) < (r & 0xF))
                        setHFlag();
                    else
                        clearHFlag();
                    af.b.h -= r;
                    if (af.b.h == 0)
                        setZFlag();
                    else
                        clearZFlag();
                    setNFlag();
                    break;
                }
			case 0x96:		// SUB A, (hl)	8
                {
				int val = readMemory(hl.w);
				if (af.b.h < val)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) < (val & 0xF))
					setHFlag();
				else
					clearHFlag();
				af.b.h -= val;
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				setNFlag();
				break;
                }
			case 0xD6:		// SUB A, n			8
                {
				int val = quickRead(locPC++);
				if (af.b.h < val)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) < (val & 0xF))
					setHFlag();
				else
					clearHFlag();
				af.b.h -= val;
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				setNFlag();
				break;

                }
			case 0x9F:		// SBC A, A			4
			case 0x98:		// SBC A, B			4
			case 0x99:		// SBC A, C			4
			case 0x9A:		// SBC A, D			4
			case 0x9B:		// SBC A, E			4
			case 0x9C:		// SBC A, H			4
			case 0x9D:		// SBC A, L			4
                {
                    u8 r = *reg8[opcode&7];
                    int val2 = carrySet();
                    if (af.b.h < r + val2)
                        setCFlag();
                    else
                        clearCFlag();
                    if ((af.b.h & 0xF) < (r & 0xF) + val2)
                        setHFlag();
                    else
                        clearHFlag();
                    af.b.h -= (r + val2);
                    if (af.b.h == 0)
                        setZFlag();
                    else
                        clearZFlag();
                    setNFlag();
                    break;
                }
			case 0x9E:		// SBC A, (hl)	8
                {
				int val2 = carrySet();
				int val = readMemory(hl.w);
				if (af.b.h < val + val2)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) < (val & 0xF)+val2)
					setHFlag();
				else
					clearHFlag();
				af.b.h -= val + val2;
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				setNFlag();
				break;
                }
			case 0xde:		// SBC A, n			4
                {
				int val = quickRead(locPC++);
				int val2 = carrySet();
				if (af.b.h <val + val2)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) < (val & 0xF)+val2)
					setHFlag();
				else
					clearHFlag();
				af.b.h -= (val + val2);
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				setNFlag();
				break;
                }

			case 0xA7:		// AND A, A		4
			case 0xA0:		// AND A, B		4
			case 0xA1:		// AND A, C		4
			case 0xA2:		// AND A, D		4
			case 0xA3:		// AND A, E		4
			case 0xA4:		// AND A, H		4
			case 0xA5:		// AND A, L		4
				af.b.h &= *reg8[opcode&7];
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				setHFlag();
				clearCFlag();
				break;
			case 0xA6:		// AND A, (hl)	8
				af.b.h &= readMemory(hl.w);
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				setHFlag();
				clearCFlag();
				break;
			case 0xE6:		// AND A, n			8
				af.b.h &= quickRead(locPC++);
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				setHFlag();
				clearCFlag();
				break;

			case 0xB7:		// OR A, A			4
			case 0xB0:		// OR A, B			4
			case 0xB1:		// OR A, C			4
			case 0xB2:		// OR A, D			4
			case 0xB3:		// OR A, E			4
			case 0xB4:		// OR A, H			4
			case 0xB5:		// OR A, L			4
				af.b.h |= *reg8[opcode&7];
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				clearHFlag();
				clearCFlag();
				break;
			case 0xB6:		// OR A, (hl)		8
				af.b.h |= readMemory(hl.w);
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				clearHFlag();
				clearCFlag();
				break;
			case 0xF6:		// OR A, n			4
				af.b.h |= quickRead(locPC++);
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				clearHFlag();
				clearCFlag();
				break;

			case 0xAF:		// XOR A, A			4
			case 0xA8:		// XOR A, B			4
			case 0xA9:		// XOR A, C			4
			case 0xAA:		// XOR A, D			4
			case 0xAB:		// XOR A, E			4
			case 0xAC:		// XOR A, H			4
			case 0xAD:		// XOR A, L			4
				af.b.h ^= *reg8[opcode&7];
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				clearHFlag();
				clearCFlag();
				break;
			case 0xAE:		// XOR A, (hl)	8
				af.b.h ^= readMemory(hl.w);
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				clearHFlag();
				clearCFlag();
				break;
			case 0xEE:		// XOR A, n			8
				af.b.h ^= quickRead(locPC++);
				if (af.b.h == 0)
					setZFlag();
				else
					clearZFlag();
				clearNFlag();
				clearHFlag();
				clearCFlag();
				break;

			case 0xBF:		// CP A					4
			case 0xB8:		// CP B					4
			case 0xB9:		// CP C				4
			case 0xBA:		// CP D					4
			case 0xBB:		// CP E					4
			case 0xBC:		// CP H					4
			case 0xBD:		// CP L					4
                {
                    u8 r = *reg8[opcode&7];
                    if (af.b.h < r)
                        setCFlag();
                    else
                        clearCFlag();
                    if ((af.b.h & 0xF) < (r & 0xF))
                        setHFlag();
                    else
                        clearHFlag();
                    if (af.b.h - r == 0)
                        setZFlag();
                    else
                        clearZFlag();
                    setNFlag();
                    break;
                }
			case 0xBE:		// CP (hl)			8
                {
				int val = readMemory(hl.w);
				if (af.b.h < val)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) < (val & 0xF))
					setHFlag();
				else
					clearHFlag();
				if (af.b.h - val == 0)
					setZFlag();
				else
					clearZFlag();
				setNFlag();
				break;
                }
			case 0xFE:		// CP n					8
                {
				int val = quickRead(locPC++);
				if (af.b.h < val)
					setCFlag();
				else
					clearCFlag();
				if ((af.b.h & 0xF) < (val & 0xF))
					setHFlag();
				else
					clearHFlag();
				if (af.b.h - val == 0)
					setZFlag();
				else
					clearZFlag();
				setNFlag();
				break;
                }


			case 0x3C:		// INC A				4
			case 0x04:		// INC B				4
			case 0x0C:		// INC C				4
			case 0x14:		// INC D				4
			case 0x1C:		// INC E				4
			case 0x24:		// INC H				4
			case 0x2C:		// INC L				4
                {
                    u8* reg = reg8[opcode>>3];
                    (*reg)++;
                    u8 r = *reg;
                    if (r == 0)
                        setZFlag();
                    else
                        clearZFlag();
                    if ((r & 0xF) == 0)
                        setHFlag();
                    else
                        clearHFlag();
                    clearNFlag();
                    break;
                }
			case 0x34:		// INC (hl)		12
                {
				u8 val = readMemory(hl.w)+1;
				writeMemory(hl.w, val);
				if (val == 0)
					setZFlag();
				else
					clearZFlag();
				if ((val & 0xF) == 0)
					setHFlag();
				else
					clearHFlag();
				clearNFlag();
				break;
                }

			case 0x3D:		// DEC A				4
			case 0x05:		// DEC B				4
			case 0x0D:		// DEC C				4
			case 0x15:		// DEC D				4
			case 0x1D:		// DEC E				4
			case 0x25:		// DEC H				4
			case 0x2D:		// DEC L				4
                {
                    u8 *reg = reg8[opcode>>3];
                    (*reg)--;
                    u8 r = *reg;
                    if (r == 0)
                        setZFlag();
                    else
                        clearZFlag();
                    if ((r & 0xF) == 0xF)
                        setHFlag();
                    else
                        clearHFlag();
                    setNFlag();
                    break;
                }
			case 0x35:		// deC (hl)			12
                {
				u8 val = readMemory(hl.w)-1;
				writeMemory(hl.w, val);
				if (val == 0)
					setZFlag();
				else
					clearZFlag();
				if ((val & 0xF) == 0xF)
					setHFlag();
				else
					clearHFlag();
				setNFlag();
				break;
                }

				// 16-bit Arithmetic

			case 0x09:		// ADD hl, BC		8
				if (hl.w + bc.w > 0xFFFF)
					setCFlag();
				else
					clearCFlag();
				if ((hl.w & 0xFFF) + (bc.w & 0xFFF) > 0xFFF)
					setHFlag();
				else
					clearHFlag();
				clearNFlag();
				hl.w += bc.w;
				break;
			case 0x19:		// ADD hl, de		8
				if (hl.w + de.w > 0xFFFF)
					setCFlag();
				else
					clearCFlag();
				if ((hl.w & 0xFFF) + (de.w & 0xFFF) > 0xFFF)
					setHFlag();
				else
					clearHFlag();
				clearNFlag();
				hl.w += de.w;
				break;
			case 0x29:		// ADD hl, hl		8
				if (hl.w + hl.w > 0xFFFF)
					setCFlag();
				else
					clearCFlag();
				if ((hl.w & 0xFFF) + (hl.w & 0xFFF) > 0xFFF)
					setHFlag();
				else
					clearHFlag();
				clearNFlag();
				hl.w += hl.w;
				break;
			case 0x39:		// ADD hl, SP		8
				if (hl.w + locSP > 0xFFFF)
					setCFlag();
				else
					clearCFlag();
				if ((hl.w & 0xFFF) + (locSP & 0xFFF) > 0xFFF)
					setHFlag();
				else
					clearHFlag();
				clearNFlag();
				hl.w += locSP;
				break;

			case 0xE8:		// ADD SP, n		16
                {
				int sval = (s8)quickRead(locPC++);
				if ((locSP + sval) > 0xFFFF)
					setCFlag();
				else
					clearCFlag();
				if (((locSP & 0xFFF) + sval) > 0xFFF)
					setHFlag();
				else
					clearHFlag();
				clearNFlag();
				clearZFlag();
				locSP += sval;
				break;
                }
			case 0x03:		// INC BC				8
				bc.w++;
				break;
			case 0x13:		// INC de				8
				de.w++;
				break;
			case 0x23:		// INC hl				8
				hl.w++;
				break;
			case 0x33:		// INC SP				8
				locSP++;
				break;

			case 0x0B:		// DEC BC				8
				bc.w--;
				break;
			case 0x1B:		// DEC de				8
				de.w--;
				break;
			case 0x2B:		// DEC hl				8
				hl.w--;
				break;
			case 0x3B:		// DEC SP				8
				locSP--;
				break;

			case 0x27:		// DAA					4
				{
					int a = af.b.h;

					if (!negativeSet())
					{
						if (halfSet() || (a & 0xF) > 9)
							a += 0x06;

						if (carrySet() || a > 0x9F)
							a += 0x60;
					}
					else
					{
						if (halfSet())
							a = (a - 6) & 0xFF;

						if (carrySet())
							a -= 0x60;
					}

					af.b.l &= ~(0x20 | 0x80);

					if ((a & 0x100) == 0x100)
						setCFlag();

					a &= 0xFF;

					if (a == 0)
						setZFlag();

					af.b.h = a;

				}
				break;

			case 0x2F:		// CPL					4
				af.b.h = ~af.b.h;
				setNFlag();
				setHFlag();
				break;

			case 0x3F:		// CCF					4
				if (carrySet())
					clearCFlag();
				else
					setCFlag();
				clearNFlag();
				clearHFlag();
				break;

			case 0x37:		// SCF					4
				setCFlag();
				clearNFlag();
				clearHFlag();
				break;

			case 0x00:		// NOP					4
				break;

			case 0x76:		// HALT					4
				halt = 1;
                totalCycles += 4;
				goto end;
				break;

			case 0x10:		// STOP					4
				if (ioRam[0x4D] & 1 && gbMode == CGB)
				{
					if (ioRam[0x4D] & 0x80)
					{
						mode2Cycles /= 2;
						mode3Cycles /= 2;
						doubleSpeed = 0;
						//clockSpeed /= 2;
						ioRam[0x4D] &= ~0x80;
					}
					else
					{
						mode2Cycles *= 2;
						mode3Cycles *= 2;
						doubleSpeed = 1;
						//clockSpeed *= 2;
						ioRam[0x4D] |= 0x80;
					}

					ioRam[0x4D] &= ~1;
				}
				else
				{
					halt = 2;
					goto end;
				}
				break;

			case 0xF3:		// DI   4
				disableInterrupts();
				break;

			case 0xFB:		// EI   4
				enableInterrupts();
				break;

			case 0x07:		// RLCA 4
                {
				int val = af.b.h;
				af.b.h <<= 1;
				if (val & 0x80)
				{
					setCFlag();
					af.b.h |= 1;
				}
				else
					clearCFlag();
				clearZFlag();
				clearNFlag();
				clearHFlag();
				break;
                }

			case 0x17:		// RLA					4
                {
				int val = (af.b.h & 0x80);
				af.b.h <<= 1;
				af.b.h |= carrySet();
				if (val)
					setCFlag();
				else
					clearCFlag();
				clearZFlag();
				clearNFlag();
				clearHFlag();
				break;
                }

			case 0x0F:		// RRCA 4
                {
				int val = af.b.h;
				af.b.h >>= 1;
				if ((val & 1))
				{
					setCFlag();
					af.b.h |= 0x80;
				}
				else
					clearCFlag();
				clearZFlag();
				clearNFlag();
				clearHFlag();
				break;
                }

			case 0x1F:		// RRA					4
                {
				int val = af.b.h & 1;
				af.b.h >>= 1;
				af.b.h |= (carrySet() << 7);
				if (val)
					setCFlag();
				else
					clearCFlag();
				clearZFlag();
				clearNFlag();
				clearHFlag();
				break;
                }

			case 0xC3:		// JP				16
				locPC = quickRead(locPC) | (quickRead(locPC+1) << 8);
				break;
			case 0xC2:		// JP NZ, nn	16/12
				if (!zeroSet())
				{
					locPC = quickRead(locPC) | (quickRead(locPC+1) << 8);
				}
				else {
					locPC += 2;
                    totalCycles -= 4;
                }
				break;
			case 0xCA:		// JP Z, nn		16/12
				if (zeroSet())
				{
					locPC = quickRead(locPC) | (quickRead(locPC+1) << 8);
				}
				else {
					locPC += 2;
                    totalCycles -= 4;
                }
				break;
			case 0xD2:		// JP NC, nn	16/12
				if (!carrySet())
				{
					locPC = quickRead(locPC) | (quickRead(locPC+1) << 8);
				}
				else {
					locPC += 2;
                    totalCycles -= 4;
                }
				break;
			case 0xDA:		// JP C, nn	12
				if (carrySet())
				{
					locPC = quickRead(locPC) | (quickRead(locPC+1) << 8);
				}
				else {
					locPC += 2;
                    totalCycles -= 4;
                }
				break;
			case 0xE9:		// JP (hl)	4
				locPC = hl.w;
				break;
			case 0x18:		// JR n 12
				locPC += (s8)quickRead(locPC++);
				break;
			case 0x20:		// JR NZ n  8/12
				if (!zeroSet())
                    locPC += (s8)quickRead(locPC++);
				else {
					locPC ++;
                    totalCycles -= 4;
                }
				break;
			case 0x28:		// JR Z, n  8/12
				if (zeroSet())
				{
                    locPC += (s8)quickRead(locPC++);
				}
				else {
					locPC ++;
                    totalCycles -= 4;
                }
				break;
			case 0x30:		// JR NC, n 8/12
				if (!carrySet())
				{
                    locPC += (s8)quickRead(locPC++);
				}
				else {
					locPC ++;
                    totalCycles -= 4;
                }
				break;
			case 0x38:		// JR C, n  8/12
				if (carrySet())
				{
                    locPC += (s8)quickRead(locPC++);
				}
				else {
					locPC ++;
                    totalCycles -= 4;
                }
				break;

			case 0xCD:		// CALL nn			24
                {
				int val = locPC + 2;
				quickWrite(--locSP, (val) >> 8);
				quickWrite(--locSP, (val & 0xFF));
				locPC = quickRead(locPC) | (quickRead(locPC+1) << 8);
				break;
                }
			case 0xC4:		// CALL NZ, nn	12/24
				if (!zeroSet())
				{
					int val = locPC + 2;
					quickWrite(--locSP, (val) >> 8);
					quickWrite(--locSP, (val & 0xFF));
					locPC = quickRead(locPC) | (quickRead(locPC+1) << 8);
				}
				else {
					locPC += 2;
                    totalCycles -= 12;
                }
				break;
			case 0xCC:		// CALL Z, nn		12/24
				if (zeroSet())
				{
					int val = locPC + 2;
					quickWrite(--locSP, (val) >> 8);
					quickWrite(--locSP, (val & 0xFF));
					locPC = quickRead(locPC) | (quickRead(locPC+1) << 8);
				}
				else {
					locPC += 2;
                    totalCycles -= 12;
                }
				break;
			case 0xD4:		// CALL NC, nn	12/24
				if (!carrySet())
				{
					int val = locPC + 2;
					quickWrite(--locSP, (val) >> 8);
					quickWrite(--locSP, (val & 0xFF));
					locPC = quickRead(locPC) | (quickRead(locPC+1) << 8);
				}
				else {
					locPC += 2;
                    totalCycles -= 12;
                }
				break;
			case 0xDC:		// CALL C, nn	12/24
				if (carrySet())
				{
					int val = locPC + 2;
					quickWrite(--locSP, (val) >> 8);
					quickWrite(--locSP, (val & 0xFF));
					locPC = quickRead(locPC) | (quickRead(locPC+1) << 8);
				}
				else {
					locPC += 2;
                    totalCycles -= 12;
                }
				break;

			case 0xC7:		// RST 00H			16
				quickWrite(--locSP, (locPC) >> 8);
				quickWrite(--locSP, (locPC & 0xFF));
				locPC = 0x0;
				break;
			case 0xCF:		// RST 08H			16
				quickWrite(--locSP, (locPC) >> 8);
				quickWrite(--locSP, (locPC & 0xFF));
				locPC = 0x8;
				break;
			case 0xD7:		// RST 10H			16
				quickWrite(--locSP, (locPC) >> 8);
				quickWrite(--locSP, (locPC & 0xFF));
				locPC = 0x10;
				break;
			case 0xDF:		// RST 18H			16
				quickWrite(--locSP, (locPC) >> 8);
				quickWrite(--locSP, (locPC & 0xFF));
				locPC = 0x18;
				break;
			case 0xE7:		// RST 20H			16
				quickWrite(--locSP, (locPC) >> 8);
				quickWrite(--locSP, (locPC & 0xFF));
				locPC = 0x20;
				break;
			case 0xEF:		// RST 28H			16
				quickWrite(--locSP, (locPC) >> 8);
				quickWrite(--locSP, (locPC & 0xFF));
				locPC = 0x28;
				break;
			case 0xF7:		// RST 30H			16
				quickWrite(--locSP, (locPC) >> 8);
				quickWrite(--locSP, (locPC & 0xFF));
				locPC = 0x30;
				break;
			case 0xFF:		// RST 38H			16
				quickWrite(--locSP, (locPC) >> 8);
				quickWrite(--locSP, (locPC & 0xFF));
				locPC = 0x38;
				break;

			case 0xC9:		// RET					16
				locPC = quickRead(locSP) + (quickRead(locSP+1) << 8);
				locSP += 2;
				break;
			case 0xC0:		// RET NZ				8/20
				if (!zeroSet())
				{
					locPC = quickRead(locSP) + (quickRead(locSP+1) << 8);
					locSP += 2;
				}
                else
                    totalCycles -= 12;
				break;
			case 0xC8:		// RET Z				8/20
				if (zeroSet())
				{
					locPC = quickRead(locSP) + (quickRead(locSP+1) << 8);
					locSP += 2;
				}
                else
                    totalCycles -= 12;
				break;
			case 0xD0:		// RET NC				8/20
				if (!carrySet())
				{
					locPC = quickRead(locSP) + (quickRead(locSP+1) << 8);
					locSP += 2;
				}
                else
                    totalCycles -= 12;
				break;
			case 0xD8:		// RET C				8/20
				if (carrySet())
				{
					locPC = quickRead(locSP) + (quickRead(locSP+1) << 8);
					locSP += 2;
				}
                else
                    totalCycles -= 12;
				break;
			case 0xD9:		// RETI					16
				locPC = quickRead(locSP) + (quickRead(locSP+1) << 8);
				locSP += 2;
				enableInterrupts();
				break;

			case 0xCB:
                opcode = quickRead(locPC++);
				totalCycles += CBopCycles[opcode];
				switch(opcode)
				{
					case 0x37:		// SWAP A			8
					case 0x30:		// SWAP B			8
					case 0x31:		// SWAP C			8
					case 0x32:		// SWAP D			8
					case 0x33:		// SWAP E			8
					case 0x34:		// SWAP H			8
					case 0x35:		// SWAP L			8
                        {
                            u8 *reg = reg8[opcode&7];
                            u8 r = *reg;
                            int val = r >> 4;
                            r <<= 4;
                            r |= val;
                            if (r == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            clearHFlag();
                            clearCFlag();
                            *reg = r;
                            break;
                        }
					case 0x36:		// SWAP (hl)		16
                        {
                            int val = readMemory(hl.w);
                            int val2 = val >> 4;
                            val <<= 4;
                            val |= val2;
                            writeMemory(hl.w, val);
                            if (val == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            clearHFlag();
                            clearCFlag();
                            break;
                        }

					case 0x07:		// RLC A					8
					case 0x00:		// RLC B					8
					case 0x01:		// RLC C					8
					case 0x02:		// RLC D					8
					case 0x03:		// RLC E					8
					case 0x04:		// RLC H					8
					case 0x05:		// RLC L					8
                        {
                            u8 *reg = reg8[opcode];
                            u8 r = *reg;
                            r <<= 1;
                            if (((*reg) & 0x80) != 0)
                            {
                                setCFlag();
                                r |= 1;
                            }
                            else
                                clearCFlag();
                            if (r == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            clearHFlag();
                            *reg = r;
                            break;
                        }

					case 0x06:		// RLC (hl)				16
                        {
						int val = readMemory(hl.w);
						int val2 = val;
						val2 <<= 1;
						if ((val & 0x80) != 0)
						{
							setCFlag();
							val2 |= 1;
						}
						else
							clearCFlag();
						if (val2 == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						clearHFlag();
						writeMemory(hl.w, val2);
						break;

                        }
					case 0x17:		// RL A				8
					case 0x10:		// RL B				8
					case 0x11:		// RL C				8
					case 0x12:		// RL D				8
					case 0x13:		// RL E				8
					case 0x14:		// RL H				8
					case 0x15:		// RL L				8
                        {
                            u8 *reg = reg8[opcode&7];
                            u8 r = *reg;
                            int val = (r & 0x80);
                            r <<= 1;
                            r |= carrySet();
                            if (val)
                                setCFlag();
                            else
                                clearCFlag();
                            if (r == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            clearHFlag();
                            *reg = r;
                            break;
                        }
					case 0x16:		// RL (hl)			16
                        {
						u8 val2 = readMemory(hl.w);
						int val = (val2 & 0x80);
						val2 <<= 1;
						val2 |= carrySet();
						if (val)
							setCFlag();
						else
							clearCFlag();
						if (val2 == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						clearHFlag();
						writeMemory(hl.w, val2);
						break;
                        }
					case 0x0F:		// RRC A					8
					case 0x08:		// RRC B					8
					case 0x09:		// RRC C					8
					case 0x0A:		// RRC D					8
					case 0x0B:		// RRC E					8
					case 0x0C:		// RRC H					8
					case 0x0D:		// RRC L					8
                        {
                            u8 *reg = reg8[opcode&7];
                            u8 r = *reg;
                            int val = r;
                            r >>= 1;
                            if (val&1)
                            {
                                setCFlag();
                                r |= 0x80;
                            }
                            else
                                clearCFlag();
                            if (r == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            clearHFlag();
                            *reg = r;
                            break;
                        }
					case 0x0E:		// RRC (hl)				16
                        {
						u8 val2 = readMemory(hl.w);
						int val = val2;
						val2 >>= 1;
						if ((val & 1) != 0)
						{
							setCFlag();
							val2 |= 0x80;
						}
						else
							clearCFlag();
						if (val2 == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						clearHFlag();
						writeMemory(hl.w, val2);
						break;
                        }

					case 0x1F:		// RR A					8
					case 0x18:		// RR B					8
					case 0x19:		// RR C					8
					case 0x1A:		// RR D					8
					case 0x1B:		// RR E					8
					case 0x1C:		// RR H					8
					case 0x1D:		// RR L					8
                        {
                            u8 *reg = reg8[opcode&7];
                            u8 r = *reg;
                            int val = r & 1;
                            r >>= 1;
                            r |= carrySet() << 7;
                            if (val)
                                setCFlag();
                            else
                                clearCFlag();
                            if (r == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            clearHFlag();
                            *reg = r;
                            break;
                        }
					case 0x1E:		// RR (hl)			16
                        {
						u8 val2 = readMemory(hl.w);
						int val = val2 & 1;
						val2 >>= 1;
						val2 |= carrySet() << 7;
						if (val)
							setCFlag();
						else
							clearCFlag();
						if (val2 == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						clearHFlag();
						writeMemory(hl.w, val2);
						break;
                        }


					case 0x27:		// SLA A				8
					case 0x20:		// SLA B				8
					case 0x21:		// SLA C				8
					case 0x22:		// SLA D				8
					case 0x23:		// SLA E				8
					case 0x24:		// SLA H				8
					case 0x25:		// SLA L				8
                        {
                            u8 *reg = reg8[opcode&7];
                            u8 r = *reg;
                            int val = (r & 0x80);
                            r <<= 1;
                            if (val)
                                setCFlag();
                            else
                                clearCFlag();
                            if (r == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            clearHFlag();
                            *reg = r;
                            break;
                        }
					case 0x26:		// SLA (hl)			16
                        {
						u8 val2 = readMemory(hl.w);
						int val = (val2 & 0x80);
						val2 <<= 1;
						if (val)
							setCFlag();
						else
							clearCFlag();
						if (val2 == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						clearHFlag();
						writeMemory(hl.w, val2);
						break;
                        }

					case 0x2F:		// SRA A				8
					case 0x28:		// SRA B				8
					case 0x29:		// SRA C				8
					case 0x2A:		// SRA D				8
					case 0x2B:		// SRA E				8
					case 0x2C:		// SRA H				8
					case 0x2D:		// SRA L				8
                        {
                            u8 *reg = reg8[opcode&7];
                            u8 r = *reg;
                            if (r & 1)
                                setCFlag();
                            else
                                clearCFlag();
                            r >>= 1;
                            if (r & 0x40)
                                r |= 0x80;
                            if (r == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            clearHFlag();
                            *reg = r;
                            break;
                        }
					case 0x2E:		// SRA (hl)			16
                        {
						int val = readMemory(hl.w);
						if (val & 1)
							setCFlag();
						else
							clearCFlag();
						val >>= 1;
						if (val & 0x40)
							val |= 0x80;
						if (val == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						clearHFlag();
						writeMemory(hl.w, val);
						break;
                        }

					case 0x3F:		// SRL A				8
					case 0x38:		// SRL B				8
					case 0x39:		// SRL C				8
					case 0x3A:		// SRL D				8
					case 0x3B:		// SRL E				8
					case 0x3C:		// SRL H				8
					case 0x3D:		// SRL L				8
                        {
                            u8 *reg = reg8[opcode&7];
                            u8 r = *reg;
                            if (r & 1)
                                setCFlag();
                            else
                                clearCFlag();
                            r >>= 1;
                            if (r == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            clearHFlag();
                            *reg = r;
                            break;
                        }
					case 0x3E:		// SRL (hl)			16
                        {
                            int val = readMemory(hl.w);
                            if (val & 1)
                                setCFlag();
                            else
                                clearCFlag();
                            val >>= 1;
                            if (val == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            clearHFlag();
                            writeMemory(hl.w, val);
                            break;
                        }

					case 0x47:		// BIT 0, A     8
					case 0x40:		// BIT 0, B     8
					case 0x41:		// BIT 0, C     8
					case 0x42:		// BIT 0, D     8
					case 0x43:		// BIT 0, E     8
					case 0x44:		// BIT 0, H     8
					case 0x45:		// BIT 0, L     8
					case 0x48:		// BIT 1, B     8
					case 0x49:		// BIT 1, C
					case 0x4A:		// BIT 1, D
					case 0x4B:		// BIT 1, E
					case 0x4C:		// BIT 1, H
					case 0x4D:		// BIT 1, L
					case 0x4F:		// BIT 1, A
					case 0x50:		// BIT 2, B
					case 0x51:		// BIT 2, C
					case 0x52:		// BIT 2, D
					case 0x53:		// BIT 2, E
					case 0x54:		// BIT 2, H
					case 0x55:		// BIT 2, L
					case 0x57:		// BIT 2, A
					case 0x58:		// BIT 3, B
					case 0x59:		// BIT 3, C
					case 0x5A:		// BIT 3, D
					case 0x5B:		// BIT 3, E
					case 0x5C:		// BIT 3, H
					case 0x5D:		// BIT 3, L
					case 0x5F:		// BIT 3, A
					case 0x60:		// BIT 4, B
					case 0x61:		// BIT 4, C
					case 0x62:		// BIT 4, D
					case 0x63:		// BIT 4, E
					case 0x64:		// BIT 4, H
					case 0x65:		// BIT 4, L
					case 0x67:		// BIT 4, A
					case 0x68:		// BIT 5, B
					case 0x69:		// BIT 5, C
					case 0x6A:		// BIT 5, D
					case 0x6B:		// BIT 5, E
					case 0x6C:		// BIT 5, H
					case 0x6D:		// BIT 5, L
					case 0x6F:		// BIT 5, A
					case 0x70:		// BIT 6, B
					case 0x71:		// BIT 6, C
					case 0x72:		// BIT 6, D
					case 0x73:		// BIT 6, E
					case 0x74:		// BIT 6, H
					case 0x75:		// BIT 6, L
					case 0x77:		// BIT 6, A
					case 0x78:		// BIT 7, B
					case 0x79:		// BIT 7, C
					case 0x7A:		// BIT 7, D
					case 0x7B:		// BIT 7, E
					case 0x7C:		// BIT 7, H
					case 0x7D:		// BIT 7, L
					case 0x7F:		// BIT 7, A
                        {
                            if (((*reg8[opcode&7]) & (1<<((opcode>>3)&7))) == 0)
                                setZFlag();
                            else
                                clearZFlag();
                            clearNFlag();
                            setHFlag();
                            break;
                        }
					case 0x46:		// BIT 0, (hl)      12
						if ((readMemory(hl.w) & 0x1) == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						setHFlag();
						break;
					case 0x4E:		// BIT 1, (hl)
						if ((readMemory(hl.w) & 0x2) == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						setHFlag();
						break;
					case 0x56:		// BIT 2, (hl)
						if ((readMemory(hl.w) & 0x4) == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						setHFlag();
						break;
					case 0x5E:		// BIT 3, (hl)
						if ((readMemory(hl.w) & 0x8) == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						setHFlag();
						break;
					case 0x66:		// BIT 4, (hl)
						if ((readMemory(hl.w) & 0x10) == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						setHFlag();
						break;
					case 0x6E:		// BIT 5, (hl)
						if ((readMemory(hl.w) & 0x20) == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						setHFlag();
						break;
					case 0x76:		// BIT 6, (hl)
						if ((readMemory(hl.w) & 0x40) == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						setHFlag();
						break;
					case 0x7E:		// BIT 7, (hl)
						if ((readMemory(hl.w) & 0x80) == 0)
							setZFlag();
						else
							clearZFlag();
						clearNFlag();
						setHFlag();
						break;
					case 0xC0:		// SET 0, B
						bc.b.h |= 1;
						break;
					case 0xC1:		// SET 0, C
						bc.b.l |= 1;
						break;
					case 0xC2:		// SET 0, D
						de.b.h |= 1;
						break;
					case 0xC3:		// SET 0, E
						de.b.l |= 1;
						break;
					case 0xC4:		// SET 0, H
						hl.b.h |= 1;
						break;
					case 0xC5:		// SET 0, L
						hl.b.l |= 1;
						break;
					case 0xC6:		// SET 0, (hl)  16
                        {
						int val = readMemory(hl.w);
						val |= 1;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xC7:		// SET 0, A
						af.b.h |= 1;
						break;
					case 0xC8:		// SET 1, B
						bc.b.h |= 2;
						break;
					case 0xC9:		// SET 1, C
						bc.b.l |= 2;
						break;
					case 0xCA:		// SET 1, D
						de.b.h |= 2;
						break;
					case 0xCB:		// SET 1, E
						de.b.l |= 2;
						break;
					case 0xCC:		// SET 1, H
						hl.b.h |= 2;
						break;
					case 0xCD:		// SET 1, L
						hl.b.l |= 2;
						break;
					case 0xCE:		// SET 1, (hl)
                        {
						int val = readMemory(hl.w);
						val |= 2;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xCF:		// SET 1, A
						af.b.h |= 2;
						break;
					case 0xD0:		// SET 2, B
						bc.b.h |= 4;
						break;
					case 0xD1:		// SET 2, C
						bc.b.l |= 4;
						break;
					case 0xD2:		// SET 2, D
						de.b.h |= 4;
						break;
					case 0xD3:		// SET 2, E
						de.b.l |= 4;
						break;
					case 0xD4:		// SET 2, H
						hl.b.h |= 4;
						break;
					case 0xD5:		// SET 2, L
						hl.b.l |= 4;
						break;
					case 0xD6:		// SET 2, (hl)
                        {
						int val = readMemory(hl.w);
						val |= 4;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xD7:		// SET 2, A
						af.b.h |= 4;
						break;
					case 0xD8:		// SET 3, B
						bc.b.h |= 8;
						break;
					case 0xD9:		// SET 3, C
						bc.b.l |= 8;
						break;
					case 0xDA:		// SET 3, D
						de.b.h |= 8;
						break;
					case 0xDB:		// SET 3, E
						de.b.l |= 8;
						break;
					case 0xDC:		// SET 3, H
						hl.b.h |= 8;
						break;
					case 0xDD:		// SET 3, L
						hl.b.l |= 8;
						break;
					case 0xDE:		// SET 3, (hl)
                        {
						int val = readMemory(hl.w);
						val |= 8;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xDF:		// SET 3, A
						af.b.h |= 8;
						break;
					case 0xE0:		// SET 4, B
						bc.b.h |= 0x10;
						break;
					case 0xE1:		// SET 4, C
						bc.b.l |= 0x10;
						break;
					case 0xE2:		// SET 4, D
						de.b.h |= 0x10;
						break;
					case 0xE3:		// SET 4, E
						de.b.l |= 0x10;
						break;
					case 0xE4:		// SET 4, H
						hl.b.h |= 0x10;
						break;
					case 0xE5:		// SET 4, L
						hl.b.l |= 0x10;
						break;
					case 0xE6:		// SET 4, (hl)
                        {
						int val = readMemory(hl.w);
						val |= 0x10;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xE7:		// SET 4, A
						af.b.h |= 0x10;
						break;
					case 0xE8:		// SET 5, B
						bc.b.h |= 0x20;
						break;
					case 0xE9:		// SET 5, C
						bc.b.l |= 0x20;
						break;
					case 0xEA:		// SET 5, D
						de.b.h |= 0x20;
						break;
					case 0xEB:		// SET 5, E
						de.b.l |= 0x20;
						break;
					case 0xEC:		// SET 5, H
						hl.b.h |= 0x20;
						break;
					case 0xED:		// SET 5, L
						hl.b.l |= 0x20;
						break;
					case 0xEE:		// SET 5, (hl)
                        {
						int val = readMemory(hl.w);
						val |= 0x20;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xEF:		// SET 5, A
						af.b.h |= 0x20;
						break;
					case 0xF0:		// SET 6, B
						bc.b.h |= 0x40;
						break;
					case 0xF1:		// SET 6, C
						bc.b.l |= 0x40;
						break;
					case 0xF2:		// SET 6, D
						de.b.h |= 0x40;
						break;
					case 0xF3:		// SET 6, E
						de.b.l |= 0x40;
						break;
					case 0xF4:		// SET 6, H
						hl.b.h |= 0x40;
						break;
					case 0xF5:		// SET 6, L
						hl.b.l |= 0x40;
						break;
					case 0xF6:		// SET 6, (hl)
                        {
						int val = readMemory(hl.w);
						val |= 0x40;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xF7:		// SET 6, A
						af.b.h |= 0x40;
						break;
					case 0xF8:		// SET 7, B
						bc.b.h |= 0x80;
						break;
					case 0xF9:		// SET 7, C
						bc.b.l |= 0x80;
						break;
					case 0xFA:		// SET 7, D
						de.b.h |= 0x80;
						break;
					case 0xFB:		// SET 7, E
						de.b.l |= 0x80;
						break;
					case 0xFC:		// SET 7, H
						hl.b.h |= 0x80;
						break;
					case 0xFD:		// SET 7, L
						hl.b.l |= 0x80;
						break;
					case 0xFE:		// SET 7, (hl)
                        {
						int val = readMemory(hl.w);
						val |= 0x80;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xFF:		// SET 7, A
						af.b.h |= 0x80;
						break;

					case 0x80:		// RES 0, B
						bc.b.h &= 0xFE;
						break;
					case 0x81:		// RES 0, C
						bc.b.l &= 0xFE;
						break;
					case 0x82:		// RES 0, D
						de.b.h &= 0xFE;
						break;
					case 0x83:		// RES 0, E
						de.b.l &= 0xFE;
						break;
					case 0x84:		// RES 0, H
						hl.b.h &= 0xFE;
						break;
					case 0x85:		// RES 0, L
						hl.b.l &= 0xFE;
						break;
					case 0x86:		// RES 0, (hl)
                        {
						int val = readMemory(hl.w);
						val &= 0xFE;
						writeMemory(hl.w, val);
						break;
                        }
					case 0x87:		// RES 0, A
						af.b.h &= 0xFE;
						break;
					case 0x88:		// RES 1, B
						bc.b.h &= 0xFD;
						break;
					case 0x89:		// RES 1, C
						bc.b.l &= 0xFD;
						break;
					case 0x8A:		// RES 1, D
						de.b.h &= 0xFD;
						break;
					case 0x8B:		// RES 1, E
						de.b.l &= 0xFD;
						break;
					case 0x8C:		// RES 1, H
						hl.b.h &= 0xFD;
						break;
					case 0x8D:		// RES 1, L
						hl.b.l &= 0xFD;
						break;
					case 0x8E:		// RES 1, (hl)
                        {
						int val = readMemory(hl.w);
						val &= 0xFD;
						writeMemory(hl.w, val);
						break;
                        }
					case 0x8F:		// RES 1, A
						af.b.h &= 0xFD;
						break;
					case 0x90:		// RES 2, B
						bc.b.h &= 0xFB;
						break;
					case 0x91:		// RES 2, C
						bc.b.l &= 0xFB;
						break;
					case 0x92:		// RES 2, D
						de.b.h &= 0xFB;
						break;
					case 0x93:		// RES 2, E
						de.b.l &= 0xFB;
						break;
					case 0x94:		// RES 2, H
						hl.b.h &= 0xFB;
						break;
					case 0x95:		// RES 2, L
						hl.b.l &= 0xFB;
						break;
					case 0x96:		// RES 2, (hl)
                        {
						int val = readMemory(hl.w);
						val &= 0xFB;
						writeMemory(hl.w, val);
						break;
                        }
					case 0x97:		// RES 2, A
						af.b.h &= 0xFB;
						break;
					case 0x98:		// RES 3, B
						bc.b.h &= 0xF7;
						break;
					case 0x99:		// RES 3, C
						bc.b.l &= 0xF7;
						break;
					case 0x9A:		// RES 3, D
						de.b.h &= 0xF7;
						break;
					case 0x9B:		// RES 3, E
						de.b.l &= 0xF7;
						break;
					case 0x9C:		// RES 3, H
						hl.b.h &= 0xF7;
						break;
					case 0x9D:		// RES 3, L
						hl.b.l &= 0xF7;
						break;
					case 0x9E:		// RES 3, (hl)
                        {
						int val = readMemory(hl.w);
						val &= 0xF7;
						writeMemory(hl.w, val);
						break;
                        }
					case 0x9F:		// RES 3, A
						af.b.h &= 0xF7;
						break;
					case 0xA0:		// RES 4, B
						bc.b.h &= 0xEF;
						break;
					case 0xA1:		// RES 4, C
						bc.b.l &= 0xEF;
						break;
					case 0xA2:		// RES 4, D
						de.b.h &= 0xEF;
						break;
					case 0xA3:		// RES 4, E
						de.b.l &= 0xEF;
						break;
					case 0xA4:		// RES 4, H
						hl.b.h &= 0xEF;
						break;
					case 0xA5:		// RES 4, L
						hl.b.l &= 0xEF;
						break;
					case 0xA6:		// RES 4, (hl)
                        {
						int val = readMemory(hl.w);
						val &= 0xEF;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xA7:		// RES 4, A
						af.b.h &= 0xEF;
						break;
					case 0xA8:		// RES 5, B
						bc.b.h &= 0xDF;
						break;
					case 0xA9:		// RES 5, C
						bc.b.l &= 0xDF;
						break;
					case 0xAA:		// RES 5, D
						de.b.h &= 0xDF;
						break;
					case 0xAB:		// RES 5, E
						de.b.l &= 0xDF;
						break;
					case 0xAC:		// RES 5, H
						hl.b.h &= 0xDF;
						break;
					case 0xAD:		// RES 5, L
						hl.b.l &= 0xDF;
						break;
					case 0xAE:		// RES 5, (hl)
                        {
						int val = readMemory(hl.w);
						val &= 0xDF;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xAF:		// RES 5, A
						af.b.h &= 0xDF;
						break;
					case 0xB0:		// RES 6, B
						bc.b.h &= 0xBF;
						break;
					case 0xB1:		// RES 6, C
						bc.b.l &= 0xBF;
						break;
					case 0xB2:		// RES 6, D
						de.b.h &= 0xBF;
						break;
					case 0xB3:		// RES 6, E
						de.b.l &= 0xBF;
						break;
					case 0xB4:		// RES 6, H
						hl.b.h &= 0xBF;
						break;
					case 0xB5:		// RES 6, L
						hl.b.l &= 0xBF;
						break;
					case 0xB6:		// RES 6, (hl)
                        {
						int val = readMemory(hl.w);
						val &= 0xBF;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xB7:		// RES 6, A
						af.b.h &= 0xBF;
						break;
					case 0xB8:		// RES 7, B
						bc.b.h &= 0x7F;
						break;
					case 0xB9:		// RES 7, C
						bc.b.l &= 0x7F;
						break;
					case 0xBA:		// RES 7, D
						de.b.h &= 0x7F;
						break;
					case 0xBB:		// RES 7, E
						de.b.l &= 0x7F;
						break;
					case 0xBC:		// RES 7, H
						hl.b.h &= 0x7F;
						break;
					case 0xBD:		// RES 7, L
						hl.b.l &= 0x7F;
						break;
					case 0xBE:		// RES 7, (hl)
                        {
						int val = readMemory(hl.w);
						val &= 0x7F;
						writeMemory(hl.w, val);
						break;
                        }
					case 0xBF:		// RES 7, A
						af.b.h &= 0x7F;
						break;
					default:
						break;
				}
				break;
			default:
				break;
		}
	}

end:
    gbPC = locPC;
    gbSP = locSP;
	return totalCycles;
}
