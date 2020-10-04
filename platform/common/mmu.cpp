#ifdef DS
extern "C" {
#include "libfat/disc.h"
#include "libfat/partition.h"
}
#endif

#include <stdio.h>
#include <cstdlib>
#include <string.h>
#include "mmu.h"
#include "gameboy.h"
#include "gbgfx.h"
#include "gbmanager.h"
#include "soundengine.h"
#include "inputhelper.h"
#include "nifi.h"
#include "console.h"
#include "menu.h"
#include "gbs.h"
#include "timer.h"
#include "romfile.h"


#define refreshVramBank() { \
    memory[0x8] = vram[vramBank]; \
    memory[0x9] = vram[vramBank]+0x1000; }
#define refreshWramBank() { \
    memory[0xd] = wram[wramBank]; }

void Gameboy::refreshRomBank(int bank) 
{
    romBank = bank % romFile->getNumRomBanks();
    romFile->loadRomBank(romBank); 
    memory[0x4] = romFile->romSlot1;
    memory[0x5] = romFile->romSlot1+0x1000;
    memory[0x6] = romFile->romSlot1+0x2000;
    memory[0x7] = romFile->romSlot1+0x3000;
}

void Gameboy::refreshRamBank (int bank) 
{
    if (bank < getNumSramBanks()) {
        currentRamBank = bank;
        memory[0xa] = externRam+currentRamBank*0x2000;
        memory[0xb] = externRam+currentRamBank*0x2000+0x1000; 
    }
    else
        printLog("Tried to access ram bank %x\n", bank);
}

void Gameboy::writeSram(u16 addr, u8 val) {
    int pos = addr + currentRamBank*0x2000;
    if (externRam[pos] != val) {
        externRam[pos] = val;
        if (autoSavingEnabled) {
            /*
            file_seek(saveFile, currentRamBank*0x2000+addr, SEEK_SET);
            file_putc(val, saveFile);
            */
            saveModified = true;
            dirtySectors[pos/fatBytesPerSector] = true;
            numSaveWrites++;
        }
    }
}

void Gameboy::initMMU()
{
    wramBank = 1;
    vramBank = 0;

    dmaSource=0;
    dmaDest=0;
    dmaLength=0;
    dmaMode=0;

    ramEnabled = false;
    memoryModel = 0;
    romBank = 1;
    currentRamBank = 0;

    readFunc = mbcReads[romFile->getMBC()];
    writeFunc = mbcWrites[romFile->getMBC()];

    /* Rockman8 by Yang Yang uses a slightly different MBC1 variant */
    rockmanMapper = !strcmp(romFile->getRomTitle(), "ROCKMAN 99");

    rumbleValue = 0;
    lastRumbleValue = 0;

    HuC3Mode = 0;
    HuC3Value = 0;
    HuC3Shift = 0;

    mbc7State = 0;
    mbc7Buffer = 0;
    mbc7RA = 0;

    biosOn = false;
    if (biosExists && !probingForBorder && !gbsMode) {
        if (biosEnabled == 2)
            biosOn = true;
        else if (biosEnabled == 1 && resultantGBMode == 0)
            biosOn = true;
    }

    mapMemory();
    for (int i=0; i<8; i++)
        memset(wram[i], 0, 0x1000);
    memset(highram, 0, 0x1000); // Initializes sprites and IO registers

    memset(vram[0], 0, 0x2000);
    memset(vram[1], 0, 0x2000);

    writeIO(0x02, 0x00);
    writeIO(0x05, 0x00);
    writeIO(0x06, 0x00);
    writeIO(0x07, 0x00);
    writeIO(0x40, 0x91);
    writeIO(0x42, 0x00);
    writeIO(0x43, 0x00);
    writeIO(0x45, 0x00);
    writeIO(0x47, 0xfc);
    writeIO(0x48, 0xff);
    writeIO(0x49, 0xff);
    writeIO(0x4a, 0x00);
    writeIO(0x4b, 0x00);
    writeIO(0xff, 0x00);

    ioRam[0x55] = 0xff;

    memset(dirtySectors, 0, sizeof(dirtySectors));

    if (!biosOn)
        initGameboyMode();
}

void Gameboy::mapMemory() {
    if (biosOn)
        memory[0x0] = romFile->bios;
    else
        memory[0x0] = romFile->romSlot0;
    memory[0x1] = romFile->romSlot0+0x1000;
    memory[0x2] = romFile->romSlot0+0x2000;
    memory[0x3] = romFile->romSlot0+0x3000;
    refreshRomBank(romBank);
    refreshRamBank(currentRamBank);
    refreshVramBank();
    memory[0xc] = wram[0];
    refreshWramBank();
    memory[0xe] = wram[0];
    memory[0xf] = highram;

    dmaSource = (ioRam[0x51]<<8) | (ioRam[0x52]);
    dmaSource &= 0xFFF0;
    dmaDest = (ioRam[0x53]<<8) | (ioRam[0x54]);
    dmaDest &= 0x1FF0;
}

u8 Gameboy::readMemoryFast(u16 addr) {
    return memory[addr>>12][addr&0xfff];
}

u16 Gameboy::readMemory16(u16 addr) {
    return readMemory(addr) | readMemory(addr+1)<<8;
}

u8 Gameboy::readIO(u8 ioReg)
{
#ifdef SPEEDHAX
    return ioRam[ioReg];
#else
    switch (ioReg)
    {
        case 0x00:
            return sgbReadP1();
        case 0x10: // NR10, sweep register 1, bit 7 set on read
            return ioRam[ioReg] | 0x80;
        case 0x11: // NR11, sound length/pattern duty 1, bits 5-0 set on read
            return ioRam[ioReg] | 0x3F;
        case 0x13: // NR13, sound frequency low byte 1, all bits set on read
            return 0xFF;
        case 0x14: // NR14, sound frequency high byte 1, bits 7,5-0 set on read
            return ioRam[ioReg] | 0xBF;
        case 0x15: // No register, all bits set on read
            return 0xFF;
        case 0x16: // NR21, sound length/pattern duty 2, bits 5-0 set on read
            return ioRam[ioReg] | 0x3F;
        case 0x18: // NR23, sound frequency low byte 2, all bits set on read
            return 0xFF;
        case 0x19: // NR24, sound frequency high byte 2, bits 7,5-0 set on read
            return ioRam[ioReg] | 0xBF;
        case 0x1A: // NR30, sound mode 3, bits 6-0 set on read
            return ioRam[ioReg] | 0x7F;
        case 0x1B: // NR31, sound length 3, all bits set on read
            return 0xFF;
        case 0x1C: // NR32, sound output level 3, bits 7,4-0 set on read
            return ioRam[ioReg] | 0x9F;
        case 0x1D: // NR33, sound frequency low byte 2, all bits set on read
            return 0xFF;
        case 0x1E: // NR34, sound frequency high byte 2, bits 7,5-0 set on read
            return ioRam[ioReg] | 0xBF;
        case 0x1F: // No register, all bits set on read
            return 0xFF;
        case 0x20: // NR41, sound mode/length 4, all bits set on read
            return 0xFF;
        case 0x23: // NR44, sound counter/consecutive, bits 7,5-0 set on read
            return ioRam[ioReg] | 0xBF;
        case 0x26: // NR52, global sound status, bits 6-4 set on read
            return ioRam[ioReg] | 0x70;
        case 0x27: // No register, all bits set on read
        case 0x28:
        case 0x29:
        case 0x2A:
        case 0x2B:
        case 0x2C:
        case 0x2D:
        case 0x2E:
        case 0x2F:
            return 0xFF;
        case 0x70: // wram register
            return ioRam[ioReg] | 0xf8;
        default:
            return ioRam[ioReg];
    }
#endif
}

u8 Gameboy::readMemoryOther(u16 addr) {
    int area = addr>>12;

    if (area == 0xf) {
        if (addr >= 0xff00)
            return readIO(addr&0xff);
        // Check for echo area
        else if (addr < 0xfe00)
            return memory[0xd][addr&0xfff];
    }
    /* Check if in range a000-bfff */
    else if (area == 0xa || area == 0xb) {
        /* Check if there's an handler for this mbc */
        if (readFunc != NULL)
            return (*this.*readFunc)(addr);
        else if (!getNumSramBanks())
            return 0xff;
    }
    return memory[area][addr&0xfff];
}

void Gameboy::writeMemoryOther(u16 addr, u8 val) {
    int area = addr>>12;
    switch (area)
    {
        case 0x8:
        case 0x9:
            if (isMainGameboy())
                writeVram(addr&0x1fff, val);
            vram[vramBank][addr&0x1fff] = val;
            return;
        case 0xE: // Echo area
            wram[0][addr&0xFFF] = val;
            return;
        case 0xF:
            if (addr >= 0xFF00)
                writeIO(addr & 0xFF, val);
            else if (addr >= 0xFE00) {
                writeHram(addr&0x1ff, val);
                hram[addr&0x1ff] = val;
            }
            else // Echo area
                wram[wramBank][addr&0xFFF] = val;
            return;
    }
    if (writeFunc != NULL)
        (*this.*writeFunc)(addr, val);
}

void Gameboy::writeIO(u8 ioReg, u8 val) {
    switch (ioReg)
    {
        case 0x00:
            if (sgbMode)
                sgbHandleP1(val);
            else {
                ioRam[0x00] = val;
#ifdef SPEEDHAX
                refreshP1();
#endif
            }
            return;
        case 0x01:
            ioRam[ioReg] = val;
            return;
        case 0x02:
            {
                if ((ioRam[ioReg] & 0x01) != (val & 0x01)) {
#ifdef LINK_DEBUG
                    if (val & 0x01) {
                        char buf[50];
                        sprintf(buf, "%s (%s)", isMainGameboy() ? "Main" : "Other", (val & 0x02) ? "speedy" : "normal");
                        if (isMainGameboy())
                            printLog("Internal Clock: %s\n", buf);
                        else
                            printLog("Internal Clock: %s\n", buf);
                    }
                    else {
                        if (isMainGameboy())
                            printLog("External Clock: Main\n");
                        else
                            printLog("External Clock: Other\n");
                    }
#endif
                }
                ioRam[ioReg] = val;
                if ((val & 0x81) == 0x81) { // Internal clock
                    if (serialCounter == 0) {
                        // DS can't handle high speed
                        if (false && gbMode == CGB && (val & 0x02)) {
                            serialCounter = clockSpeed/(1024*32);
                        }
                        else
                            serialCounter = clockSpeed/1024;
                        if (cyclesToExecute > serialCounter)
                            cyclesToExecute = serialCounter;
                    }
                }
                else {
                    serialCounter = 0;
                    if (val & 0x80) { // External clock
                        if (mgr_isInternalClockGb(this) || mgr_areBothUsingExternalClock()) {
                            int cycles = linkedGameboy->cycleCount - cycleCount;
                            if (cycles < 0)
                                cycles = 0;
                            if (cyclesToExecute > cycles) {
                                cyclesToExecute = cycles;
                            }
                        }
                    }
                }
                return;
            }
        case 0x04:
            ioRam[ioReg] = 0;
            return;
        case 0x05:
            ioRam[ioReg] = val;
            break;
        case 0x06:
            ioRam[ioReg] = val;
            break;
        case 0x07:
            timerPeriod = timerPeriods[val&0x3];
            ioRam[ioReg] = val;
            break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x24:
        case 0x25:
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
handleSoundReg:
            if (soundDisabled || // If sound is disabled from menu, or
                    // If sound is globally disabled via shutting down the APU,
                    (!(ioRam[0x26] & 0x80)
                     // ignore register writes to between FF10 and FF25 inclusive.
                     && ioReg >= 0x10 && ioReg <= 0x25))
                return;
            ioRam[ioReg] = val;
            soundEngine->handleSoundRegister(ioReg, val);
            return;
        case 0x26:
            ioRam[ioReg] &= ~0x80;
            ioRam[ioReg] |= (val&0x80);

            if (!(val & 0x80)) {
                for (int reg = 0x10; reg <= 0x25; reg++)
                    ioRam[reg] = 0;
                clearSoundChannel(CHAN_1);
                clearSoundChannel(CHAN_2);
                clearSoundChannel(CHAN_3);
                clearSoundChannel(CHAN_4);
            }

            soundEngine->handleSoundRegister(ioReg, val);
            return;
        case 0x14:
            if (soundDisabled || (!(ioRam[0x26] & 0x80)))
                return;
            if (val & 0x80)
                setSoundChannel(CHAN_1);
            goto handleSoundReg;
        case 0x19:
            if (soundDisabled || (!(ioRam[0x26] & 0x80)))
                return;
            if (val & 0x80)
                setSoundChannel(CHAN_2);
            goto handleSoundReg;
        case 0x1A:
            if (soundDisabled || (!(ioRam[0x26] & 0x80)))
                return;
            if (!(val & 0x80))
                clearSoundChannel(CHAN_3);
            goto handleSoundReg;
        case 0x1E:
            if (soundDisabled || (!(ioRam[0x26] & 0x80)))
                return;
            if (val & 0x80)
                setSoundChannel(CHAN_3);
            goto handleSoundReg;
        case 0x23:
            if (soundDisabled || (!(ioRam[0x26] & 0x80)))
                return;
            if (val & 0x80)
                setSoundChannel(CHAN_4);
            goto handleSoundReg;
        case 0x42:
        case 0x43:
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4A:
        case 0x4B:
            if (isMainGameboy())
                handleVideoRegister(ioReg, val);
            ioRam[ioReg] = val;
            return;
        case 0x69: // CGB BG Palette
            if (isMainGameboy())
                handleVideoRegister(ioReg, val);
            {
                int index = ioRam[0x68] & 0x3F;
                bgPaletteData[index] = val;
            }
            if (ioRam[0x68] & 0x80)
                ioRam[0x68] = 0x80 | (ioRam[0x68]+1);
            ioRam[0x69] = bgPaletteData[ioRam[0x68]&0x3F];
            return;
        case 0x6B: // CGB Sprite palette
            if (isMainGameboy())
                handleVideoRegister(ioReg, val);
            {
                int index = ioRam[0x6A] & 0x3F;
                sprPaletteData[index] = val;
            }
            if (ioRam[0x6A] & 0x80)
                ioRam[0x6A] = 0x80 | (ioRam[0x6A]+1);
            ioRam[0x6B] = sprPaletteData[ioRam[0x6A]&0x3F];
            return;
        case 0x46: // Sprite DMA
            if (isMainGameboy())
                handleVideoRegister(ioReg, val);
            ioRam[ioReg] = val;
            {
                int src = val << 8;
                u8* mem = memory[src>>12];
                src &= 0xfff;
                for (int i=0; i<0xA0; i++) {
                    u8 val = mem[src++];
                    hram[i] = val;
                }
            }
            return;
        case 0x40: // LCDC
            if (isMainGameboy())
                handleVideoRegister(ioReg, val);
            ioRam[ioReg] = val;
            if (!(val & 0x80)) {
                ioRam[0x44] = 0;
                ioRam[0x41] &= ~3; // Set video mode 0
            }
            return;

        case 0x41:
            ioRam[ioReg] &= 0x7;
            ioRam[ioReg] |= val&0xF8;
            return;
        case 0x44:
            //ioRam[0x44] = 0;
            printLog("LY Write %d\n", val);
            return;
        case 0x45:
            ioRam[ioReg] = val;
            checkLYC();
            return;
        case 0x68:
            ioRam[ioReg] = val;
            ioRam[0x69] = bgPaletteData[val&0x3F];
            return;
        case 0x6A:
            ioRam[ioReg] = val;
            ioRam[0x6B] = sprPaletteData[val&0x3F];
            return;
        case 0x4D:
            ioRam[ioReg] &= 0x80;
            ioRam[ioReg] |= (val&1);
            return;
        case 0x4F: // Vram bank
            if (gbMode == CGB)
            {
                vramBank = val & 1;
                refreshVramBank();
            }
            ioRam[ioReg] = val&1;
            return;
            // Special register, used by the gameboy bios
        case 0x50:
            biosOn = 0;
            memory[0x0] = romFile->romSlot0;
            initGameboyMode();
            return;
        case 0x55: // CGB DMA
            if (gbMode == CGB)
            {
                if (dmaLength > 0)
                {
                    if ((val&0x80) == 0)
                    {
                        ioRam[ioReg] |= 0x80;
                        dmaLength = 0;
                    }
                    return;
                }
                dmaLength = ((val & 0x7F)+1);
                dmaSource = (ioRam[0x51]<<8) | (ioRam[0x52]);
                dmaSource &= 0xFFF0;
                dmaDest = (ioRam[0x53]<<8) | (ioRam[0x54]);
                dmaDest &= 0x1FF0;
                dmaMode = val>>7;
                ioRam[ioReg] = dmaLength-1;
                if (dmaMode == 0)
                {
                    int i;
                    for (i=0; i<dmaLength; i++)
                    {
                        if (isMainGameboy())
                            writeVram16(dmaDest, dmaSource);
                        for (int i=0; i<16; i++)
                            vram[vramBank][dmaDest++] = quickRead(dmaSource++);
                        dmaDest &= 0x1FF0;
                    }
                    extraCycles += dmaLength*8*(doubleSpeed+1);
                    dmaLength = 0;
                    ioRam[ioReg] = 0xFF;
                    ioRam[0x51] = dmaSource>>8;
                    ioRam[0x52] = dmaSource&0xff;
                    ioRam[0x53] = dmaDest>>8;
                    ioRam[0x54] = dmaDest&0xff;
                }
            }
            else
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
            ioRam[ioReg] = val&0x7;
            return;
        case 0x0F: // IF
            ioRam[ioReg] = val;
            interruptTriggered = val & ioRam[0xff];
            if (interruptTriggered)
                cyclesToExecute = -1;
            break;
        case 0xFF: // IE
            ioRam[ioReg] = val;
            interruptTriggered = val & ioRam[0x0f];
            if (interruptTriggered)
                cyclesToExecute = -1;
            break;
        default:
            ioRam[ioReg] = val;
            return;
    }
}

void Gameboy::refreshP1() {
    int controller = 0;

    // Check if input register is being used for sgb packets
    if (sgbPacketBit == -1) {
        if ((ioRam[0x00] & 0x30) == 0x30) {
            if (!sgbMode)
                ioRam[0x00] |= 0x0F;
        }
        else {
            ioRam[0x00] &= 0xF0;
            if (!(ioRam[0x00]&0x20))
                ioRam[0x00] |= (controllers[controller] & 0xF);
            else
                ioRam[0x00] |= ((controllers[controller] & 0xF0)>>4);
        }
        ioRam[0x00] |= 0xc0;
    }
}

bool Gameboy::updateHBlankDMA()
{
    if (dmaLength > 0)
    {
        if (isMainGameboy())
            writeVram16(dmaDest, dmaSource);
        for (int i=0; i<16; i++)
            vram[vramBank][dmaDest++] = quickRead(dmaSource++);
        dmaDest &= 0x1FF0;
        dmaLength --;
        ioRam[0x55] = dmaLength-1;
        ioRam[0x51] = dmaSource>>8;
        ioRam[0x52] = dmaSource&0xff;
        ioRam[0x53] = dmaDest>>8;
        ioRam[0x54] = dmaDest&0xff;
        return true;
    }
    else
        return false;
}

// This bypasses libfat's cache to directly write a single sector of the save 
// file. This reduces lag.
void Gameboy::writeSaveFileSectors(int startSector, int numSectors) {
#ifdef DS
    if (saveFileSectors[startSector] == -1) {
        flushFatCache();
        return;
    }
    devoptab_t* devops = (devoptab_t*)GetDeviceOpTab ("sd");
    PARTITION* partition = (PARTITION*)devops->deviceData;

	_FAT_disc_writeSectors(partition->disc, saveFileSectors[startSector], numSectors, externRam+startSector*fatBytesPerSector);
#endif
}
