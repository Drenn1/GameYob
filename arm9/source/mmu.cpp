
#include <stdio.h>
#include <cstdlib>
#include "mmu.h"
#include "gbcpu.h"
#include "gameboy.h"
#include "gbgfx.h"
#include "gbsnd.h"
#include "inputhelper.h"
#include "main.h"
#ifdef DS
#include <nds.h>
#endif

#define refreshRomBank() { \
    loadRomBank(); \
    memory[0x4] = rom[currentRomBank]; \
    memory[0x5] = rom[currentRomBank]+0x1000; \
    memory[0x6] = rom[currentRomBank]+0x2000; \
    memory[0x7] = rom[currentRomBank]+0x3000; }
#define refreshVramBank() { \
    memory[0x8] = vram[vramBank]; \
    memory[0x9] = vram[vramBank]+0x1000; }
#define refreshRamBank() { \
    memory[0xa] = externRam[currentRamBank]; \
    memory[0xb] = externRam[currentRamBank]+0x1000; }
#define refreshWramBank() { \
    memory[0xd] = wram[wramBank]; }


int watchAddr=-1;
int readWatchAddr=-1;
int bankWatchAddr=-1;

clockStruct gbClock;

int numRomBanks=0;
int numRamBanks=0;

u8 bios[0x900];
bool biosExists = false;
bool biosEnabled = false;
bool biosOn = false;

u8* memory[0x10];
u8* rom[MAX_ROM_BANKS];
u8 vram[2][0x2000];
u8** externRam = NULL;
u8 wram[8][0x1000];
u8 hram[0x200];
u8* highram = hram-0xe00;
u8* ioRam = &hram[0x100];
u8 spriteData[0xA0];
int wramBank;
int vramBank;

int MBC;
int memoryModel;
bool hasClock=false;
int currentRomBank;
int currentRamBank;

u16 dmaSource;
u16 dmaDest;
u16 dmaLength;
int dmaMode;

extern int dmaLine;

void initMMU()
{
    wramBank = 1;
    vramBank = 0;
    currentRomBank = 1;
    currentRamBank = 0;
    memoryModel = 1;

    if (biosEnabled)
        memory[0x0] = bios;
    else
        memory[0x0] = rom[0];
    memory[0x1] = rom[0]+0x1000;
    memory[0x2] = rom[0]+0x2000;
    memory[0x3] = rom[0]+0x3000;
    memory[0x4] = rom[currentRomBank];
    memory[0x5] = rom[currentRomBank]+0x1000;
    memory[0x6] = rom[currentRomBank]+0x2000;
    memory[0x7] = rom[currentRomBank]+0x3000;
    memory[0x8] = vram[vramBank];
    memory[0x9] = vram[vramBank]+0x1000;
    memory[0xa] = externRam[currentRamBank];
    memory[0xb] = externRam[currentRamBank]+0x1000;
    memory[0xc] = wram[0];
    memory[0xd] = wram[wramBank];
    memory[0xe] = wram[0];
    memory[0xf] = highram;
}

void latchClock()
{
    // +2h, the same as lameboy
    time_t now = time(NULL)-120*60;
    time_t difference = now - gbClock.clockLastTime;

    int seconds = difference%60;
    gbClock.clockSeconds += seconds;
    if (gbClock.clockSeconds >= 60)
    {
        gbClock.clockMinutes++;
        gbClock.clockSeconds -= 60;
    }

    difference /= 60;
    int minutes = difference%60;
    gbClock.clockMinutes += minutes;
    if (gbClock.clockMinutes >= 60)
    {
        gbClock.clockHours++;
        gbClock.clockMinutes -= 60;
    }

    difference /= 60;
    gbClock.clockHours += difference%24;
    if (gbClock.clockHours >= 24)
    {
        gbClock.clockDays++;
        gbClock.clockHours -= 24;
    }

    difference /= 24;
    gbClock.clockDays += difference;
    if (gbClock.clockDays > 0x1FF)
    {
        gbClock.clockControl |= 0x80;
        gbClock.clockDays -= 0x200;
    }
    gbClock.clockControl &= ~1;
    gbClock.clockControl |= gbClock.clockDays>>8;
    gbClock.clockLastTime = now;

    gbClock.clockSecondsL = gbClock.clockSeconds;
    gbClock.clockMinutesL = gbClock.clockMinutes;
    gbClock.clockHoursL = gbClock.clockHours;
    gbClock.clockDaysL = gbClock.clockDays;
    gbClock.clockControlL = gbClock.clockControl;
}

void writeVram(u16 addr, u8 val) {
    vram[vramBank][addr] = val;

    if (addr < 0x1800) {
        int tileNum = addr/16;
        int scanline = ioRam[0x44];
        if (scanline >= 128 && scanline < 144) {
            if (!changedTileInFrame[vramBank][tileNum]) {
                changedTileInFrame[vramBank][tileNum] = true;
                changedTileInFrameQueue[changedTileInFrameQueueLength++] = tileNum|(vramBank<<9);
            }
        }
        else {
            if (!changedTile[vramBank][tileNum]) {
                changedTile[vramBank][tileNum] = true;
                changedTileQueue[changedTileQueueLength++] = tileNum|(vramBank<<9);
            }
        }
    }
    else {
        int map = (addr-0x1800)/0x400;
        if (map)
            updateTileMap(map, addr-0x1c00, val);
        else
            updateTileMap(map, addr-0x1800, val);
    }
}
void writeVram16(u16 dest, u16 src) {
    for (int i=0; i<16; i++) {
        vram[vramBank][dest++] = readMemory(src++);
    }
    dest -= 16;
    src -= 16;
    if (dest < 0x1800) {
        int tileNum = dest/16;
        if (ioRam[0x44] < 144) {
            if (!changedTileInFrame[vramBank][tileNum]) {
                changedTileInFrame[vramBank][tileNum] = true;
                changedTileInFrameQueue[changedTileInFrameQueueLength++] = tileNum|(vramBank<<9);
            }
        }
        else {
            if (!changedTile[vramBank][tileNum]) {
                changedTile[vramBank][tileNum] = true;
                changedTileQueue[changedTileQueueLength++] = tileNum|(vramBank<<9);
            }
        }
    }
    else {
        for (int i=0; i<16; i++) {
            int addr = dest+i;
            int map = (addr-0x1800)/0x400;
            if (map)
                updateTileMap(map, addr-0x1c00, vram[vramBank][src+i]);
            else
                updateTileMap(map, addr-0x1800, vram[vramBank][src+i]);
        }
    }
}

#ifdef DS
u8 readMemory(u16 addr) ITCM_CODE;
#endif

u8 readMemory(u16 addr)
{
    switch ((addr & 0xF000) >> 12)
    {
        case 0x0:
            if (biosOn)
                return bios[addr];
        case 0x1:
        case 0x2:
        case 0x3:
            return rom[0][addr];
            break;
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            return rom[currentRomBank][addr&0x3fff];
            break;
        case 0x8:
        case 0x9:
            return vram[vramBank][addr&0x1fff];
            break;
        case 0xA:
            //	if (addr == 0xa080)
            //		return 1;
        case 0xB:
            if (MBC == 3)
            {
                switch (currentRamBank)
                {
                    case 0x8:
                        return gbClock.clockSecondsL;
                    case 0x9:
                        return gbClock.clockMinutesL;
                    case 0xA:
                        return gbClock.clockHoursL;
                    case 0xB:
                        return gbClock.clockDaysL&0xFF;
                    case 0xC:
                        return gbClock.clockControlL;
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                        break;
                    default:
                        return 0;
                }
            }
            return externRam[currentRamBank][addr&0x1fff];
            break;
        case 0xC:
            return wram[0][addr&0xFFF];
            break;
        case 0xD:
            return wram[wramBank][addr&0xFFF];
            break;
        case 0xE:
            return wram[0][addr&0xFFF];
            break;
        case 0xF:
            if (addr < 0xFE00)
                return wram[wramBank][addr&0xFFF];
            else if (addr < 0xFF00)
                return hram[addr&0x1ff];
            else
                return readIO(addr&0xff);
            break;
        default:
            //return memory[addr];
            break;
    }
    return 0;
}

#ifdef DS
u8 readIO(u8 ioReg) ITCM_CODE;
#endif

u8 readIO(u8 ioReg)
{
    switch (ioReg)
    {
        case 0x00:
            if (ioRam[0x00] & 0x20)
                return (ioRam[0x00] & 0xF0) | ((buttonsPressed & 0xF0)>>4);
            else
                return (ioRam[0x00] & 0xF0) | (buttonsPressed & 0xF);
            break;
        case 0x26:
            return ioRam[ioReg];
            break;
        case 0x69:
            {
                int index = ioRam[0x68] & 0x3F;
                return bgPaletteData[index];
                break;
            }
        case 0x6B:
            {
                int index = ioRam[0x6A] & 0x3F;
                return sprPaletteData[index];
                break;
            }
        case 0x6C:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
            return ioRam[ioReg];
            break;
        default:
            return hram[0x100 + ioReg];
    }
}

u16 readhword(u16 addr)
{
    return (readMemory(addr))|(readMemory(addr+1)<<8);
}

#ifdef DS
void writeMemory(u16 addr, u8 val) ITCM_CODE;
#endif

void writeMemory(u16 addr, u8 val)
{
#ifndef DS
    if (addr == watchAddr)
        debugMode = 1;
#endif
    switch ((addr & 0xF000) >> 12)
    {
        case 0x2:
            if (MBC == 5)
            {
                currentRomBank &= 0x100;
                currentRomBank |= val;
                if (currentRomBank >= numRomBanks)
                {
                    currentRomBank = numRomBanks-1;
                    printLog("Game tried to access more rom than it has\n");
                }
                refreshRomBank();
                return;
            }
        case 0x3:
            switch (MBC)
            {
                case 1:
                    currentRomBank &= 0xE0;
                    currentRomBank |= (val & 0x1F);
                    if (currentRomBank == 0)
                        currentRomBank = 1;
                    break;
                case 3:
                    currentRomBank = (val & 0x7F);
                    if (currentRomBank == 0)
                        currentRomBank = 1;
                    break;
                case 5:
                    currentRomBank &= 0xFF;
                    currentRomBank |= (val&1) << 8;
                    break;
                default:
                    break;
            }
            if (currentRomBank >= numRomBanks)
            {
                currentRomBank = numRomBanks-1;
                printLog("Game tried to access more rom than it has\n");
            }
            refreshRomBank();
            return;
        case 0x4:
        case 0x5:
            switch (MBC)
            {
                case 1:
                    if (memoryModel == 0)
                    {
                        currentRomBank &= 0x1F;
                        val &= 0xE0;
                        currentRomBank |= val;
                        if (currentRomBank == 0)
                            currentRomBank = 1;
                        if (currentRomBank >= numRomBanks)
                        {
                            currentRomBank = numRomBanks-1;
                            printLog("Game tried to access more rom than it has\n");
                        }
                        refreshRomBank();
                    }
                    else
                    {
                        currentRamBank = val & 0x3;
                        refreshRamBank();
                    }
                    break;
                case 3:
                case 5:
                    currentRamBank = val;
                    currentRamBank = val;
                    refreshRamBank();
                    break;
                default:
                    break;
            }
            if (currentRamBank >= numRamBanks && MBC != 3)
            {
                if (numRamBanks == 0)
                    currentRamBank = 0;
                else {
                    currentRamBank = numRamBanks-1;
                    printLog("Game tried to access more ram than it has\n");
                    refreshRamBank();
                }
            }
            return;
        case 0x6:
        case 0x7:
            if (MBC == 1)
            {
                memoryModel = val & 1;
            }
            else if (MBC == 3)
            {
                if (val == 1)
                    latchClock();
            }
            return;
        case 0x8:
        case 0x9:
            writeVram(addr&0x1fff, val);
            return;
        case 0xA:
        case 0xB:
            if (MBC == 3)
            {
                switch (currentRamBank)
                {
                    case 0x8:
                        gbClock.clockSeconds = val%60;
                        return;
                    case 0x9:
                        gbClock.clockMinutes = val%60;
                        return;
                    case 0xA:
                        gbClock.clockHours = val%24;
                        return;
                    case 0xB:
                        gbClock.clockDays &= 0x100;
                        gbClock.clockDays |= val;
                        return;
                    case 0xC:
                        gbClock.clockDays &= 0xFF;
                        gbClock.clockDays |= (val&1)<<8;
                        gbClock.clockControl = val;
                        return;
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                        break;
                    default:
                        return;
                }
            }
            externRam[currentRamBank][addr&0x1fff] = val;
            return;
        case 0xC:
            wram[0][addr&0xFFF] = val;
            return;
        case 0xD:
            wram[wramBank][addr&0xFFF] = val;
            return;
        case 0xE:
            return;
        case 0xF:
            if (addr >= 0xFF00) {
                writeIO(addr & 0xFF, val);
            }
            else {
                if (addr >= 0xfe00 && addr < 0xfea0) {
                }
                hram[addr&0x1ff] = val;
            }
            return;
        default:
            return;
            //memory[addr] = val;
    }
}

#ifdef DS
void writeIO(u8 ioReg, u8 val) ITCM_CODE;
#endif

void writeIO(u8 ioReg, u8 val)
{
    switch (ioReg)
    {
        case 0x04:
            ioRam[0x04] = 0;
            return;
        case 0x05:
            ioRam[0x05] = val;
            break;
        case 0x07:
            timerPeriod = periods[val&0x3];
            ioRam[0x07] = val;
            break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x1E:
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:
        case 0x3A:
        case 0x3B:
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
            handleSoundRegister(0xFF00 + ioReg, val);
            return;
        case 0x40:    // LCDC
            if ((val & 0x7F) != (ioRam[0x40] & 0x7F))
                lineModified = true;
            ioRam[0x40] = val;
            return;
        case 0x41:
            ioRam[0x41] &= 0x7;
            ioRam[0x41] |= val&0xF8;
            return;
        case 0x46:				// DMA
            {
                u16 src = val << 8;
                int i;
                for (i=0; i<0xA0; i++) {
                    u8 val = readMemory(src+i);
                    hram[i] = val;
                    if (ioRam[0x44] >= 144 || ioRam[0x44] <= 1)
                        spriteData[i] = val;
                }

                totalCycles += 50;
                dmaLine = ioRam[0x44];
                //printLog("dma write %d\n", ioRam[0x44]);
                return;
            }
        case 0x42:
        case 0x43:
        case 0x4B:
            {
                if (val != ioRam[ioReg]) {
                    ioRam[ioReg] = val;
                    lineModified = true;
                }
            }
            break;
            // winY
        case 0x4A:
            if (ioRam[0x44] >= 144 || val > ioRam[0x44])
                winPosY = -1;
            else {
            // Signal that winPosY must be reset according to winY
                winPosY = -2;
            }
            lineModified = true;
            ioRam[0x4a] = val;
            break;
        case 0x44:
            //ioRam[0x44] = 0;
            return;
        case 0x47:				// BG Palette (GB classic only)
            ioRam[0x47] = val;
            if (gbMode == GB)
            {
                updateClassicBgPalette();
            }
            return;
        case 0x48:				// Spr Palette (GB classic only)
            ioRam[0x48] = val;
            if (gbMode == GB)
            {
                updateClassicSprPalette(0);
            }
            return;
        case 0x49:				// Spr Palette (GB classic only)
            ioRam[0x49] = val;
            if (gbMode == GB)
            {
                updateClassicSprPalette(1);
            }
            return;
        case 0x68:				// BG Palette Index (GBC only)
            ioRam[0x68] = val;
            return;
        case 0x69:				// BG Palette Data (GBC only)
            {
                int index = ioRam[0x68] & 0x3F;
                bgPaletteData[index] = val;
                if (index%8 == 7)
                    updateBgPalette(index/8);

                if (ioRam[0x68] & 0x80)
                    ioRam[0x68]++;
                return;
            }
        case 0x6B:				// Sprite Palette Data (GBC only)
            {
                int index = ioRam[0x6A] & 0x3F;
                sprPaletteData[index] = val;
                if (index%8 == 7)
                    updateSprPalette(index/8);

                if (ioRam[0x6A] & 0x80)
                    ioRam[0x6A]++;
                return;
            }
        case 0x4D:
            ioRam[0x4D] &= ~1;
            ioRam[0x4D] |= (val&1);
            return;
        case 0x4F:
            if (gbMode == CGB)
            {
                vramBank = val & 1;
                refreshVramBank();
            }
            ioRam[0x4F] = val&1;
            return;
            // Special register, used by the gameboy bios
        case 0x50:
            biosOn = 0;
            memory[0x0] = rom[0];
            if (rom[0][0x143] == 0x80 || rom[0][0x143] == 0xC0)
                gbMode = CGB;
            else
                gbMode = GB;
            return;
        case 0x55: // CGB DMA
            if (gbMode == CGB)
            {
                if (dmaLength > 0)
                {
                    if ((val&0x80) == 0)
                    {
                        ioRam[0x55] |= 0x80;
                        dmaLength = 0;
                    }
                    return;
                }
                int i;
                dmaLength = ((val & 0x7F)+1);
                int length = dmaLength*0x10;
                int source = (ioRam[0x51]<<8) | (ioRam[0x52]);
                source &= 0xFFF0;
                int dest = (ioRam[0x53]<<8) | (ioRam[0x54]);
                dest &= 0x1FF0;
                dmaSource = source;
                dmaDest = dest;
                dmaMode = val&0x80;
                ioRam[0x55] = dmaLength-1;
                if (dmaMode == 0)
                {
                    int i;
                    for (i=0; i<dmaLength; i++)
                    {
                        writeVram16(dest, source);
                        dest += 0x10;
                        source += 0x10;
                    }
                    totalCycles += dmaLength*8*(doubleSpeed+1);
                    dmaLength = 0;
                    ioRam[0x55] = 0xFF;
                }
            }
            else
                ioRam[0x55] = val;
            return;
        case 0x6C:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
            ioRam[ioReg] = val;
            return;
        case 0x70:				// WRAM bank, for CGB only
            if (gbMode == CGB)
            {
                wramBank = val & 0x7;
                if (wramBank == 0)
                    wramBank = 1;
                refreshWramBank();
            }
            ioRam[0x70] = val&0x7;
            return;
        case 0x0F:
            ioRam[0x0f] = val;
            if (val & ioRam[0xff])
                cyclesToExecute = 0;
            break;
        case 0xFF:
            ioRam[0xff] = val;
            if (val & ioRam[0x0f])
                cyclesToExecute = 0;
            break;
        default:
            hram[0x100 + ioReg] = val;
            return;
    }
}

void writehword(u16 addr, u16 val)
{
    writeMemory(addr, val&0xFF);
    writeMemory(addr+1, val>>8);
}

bool updateHblankDMA()
{
    static int dmaCycles;

    if (dmaLength > 0)
    {
        int i;
        for (i=0; i<0x10; i++)
        {
            writeVram((dmaDest++)&0x1fff, readMemory(dmaSource++));
        }
        dmaLength --;
        if (dmaLength == 0)
        {
            ioRam[0x55] = 0xFF;
        }
        else
            ioRam[0x55] = dmaLength-1;
        return true;
    }
    else
        return false;
}
