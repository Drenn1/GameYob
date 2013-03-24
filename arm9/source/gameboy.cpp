#include <nds.h>
#include <stdlib.h>
#include <stdio.h>
#include "gameboy.h"
#include "gbcpu.h"
#include "gbgfx.h"
#include "gbsnd.h"
#include "mmu.h"
#include "timer.h"
#include "main.h"
#include "inputhelper.h"
#include "nifi.h"
#include "console.h"

int mode2Cycles, mode3Cycles;
int scanlineCounter;
int doubleSpeed;

int turbo;
int turboFrameSkip;
int frameskip;
int frameskipCounter;
bool fpsOutput=true;
bool timeOutput=true;
bool fastForwardMode = false; // controlled by the menu
bool fastForwardKey = false;  // only while its hotkey is pressed

// ...what is phase? I think I made that up. Used for timing when the gameboy 
// screen is off.
int phaseCounter;
int dividerCounter;
int serialCounter;

int timerCounter;
int timerPeriod;
long periods[4];

bool screenOn = true;

int cyclesToEvent;
int maxWaitCycles;

inline void setEventCycles(int cycles) {
    if (cycles < cyclesToEvent) {
        cyclesToEvent = cycles;
        /*
        if (cyclesToEvent <= 0) {
           cyclesToEvent = 1;
        }
        */
    }
}

int updateInput() {
    readKeys();
    int retval = handleEvents();		// Input mostly
    if (!consoleDebugOutput && getTimerTicks() >= 1000)
    {
        consoleClear();
        int line=0;
        if (fpsOutput) {
            consoleClear();
            printf("FPS: %d\n", fps);
            line++;
        }
        fps = 0;
        startTimer();
        if (timeOutput) {
            for (; line<23-1; line++)
                printf("\n");
            time_t rawTime = time(NULL);
            char *timeString = ctime(&rawTime);
            for (int i=0;; i++) {
                if (timeString[i] == ':') {
                    timeString += i-2;
                    break;
                }
            }
            char s[50];
            strncpy(s, timeString, 50);
            s[5] = '\0';
            int spaces = 31-strlen(s);
            for (int i=0; i<spaces; i++)
                printf(" ");
            printf("%s\n", s);
        }
    }
    return retval;
}

int extraCycles;
void runEmul()
{
    for (;;)
    {
emuLoopStart:
        cyclesToEvent -= extraCycles;
        int cycles;
        if (halt)
            cycles = cyclesToEvent;
        else
            cycles = runOpcode(cyclesToEvent);
        cycles += extraCycles;

        cyclesToEvent = maxWaitCycles;
        extraCycles=0;

        if (serialCounter > 0) {
            serialCounter -= cycles;
            if (serialCounter <= 0) {
                serialCounter = 0;
                packetData = 0xff;
                transferReady = true;
            }
            else
                setEventCycles(serialCounter);
        }
        if (transferReady) {
            if (!(ioRam[0x02] & 1)) {
                sendPacketByte(56, sendData);
            }
            timerStop(2);
            ioRam[0x01] = packetData;
            requestInterrupt(SERIAL);
            ioRam[0x02] &= ~0x80;
            packetData = -1;
            transferReady = false;
        }
        updateTimers(cycles);
        updateSound(cycles);

        if (updateLCD(cycles))
            // Emulation is being reset or something
            goto emuLoopStart;

        int interruptTriggered = ioRam[0x0F] & ioRam[0xFF];
        // Run another opcode before triggering an interrupt.
        // Robocop 2 needs this.
        if (interruptTriggered) {
            if (!halt)
                extraCycles += runOpcode(4);
            handleInterrupts(interruptTriggered);
        }
    }
}

void initLCD()
{
    // Pokemon Yellow hack: I need to intentionally SLOW DOWN emulation for 
    // Pikachu's pitch to sound right...
    // The exact value of this will vary, so I'm going to leave it commented for 
    // now.
    /*
    if (strcmp(getRomTitle(), "POKEMON YELLOW") == 0)
       maxWaitCycles = 100;
    else
    */
    maxWaitCycles = 800;

    setDoubleSpeed(0);

    scanlineCounter = 456;
    phaseCounter = 456*153;
    timerCounter = 0;
    dividerCounter = 256;
    serialCounter = 0;
    turboFrameSkip = 4;
    turbo=0;

    // Timer stuff
    periods[0] = clockSpeed/4096;
    periods[1] = clockSpeed/262144;
    periods[2] = clockSpeed/65536;
    periods[3] = clockSpeed/16384;
    timerPeriod = periods[0];

    timerStop(2);
}

inline int updateLCD(int cycles)
{
    if (!(ioRam[0x40] & 0x80))		// If LCD is off
    {
        scanlineCounter = 456;
        ioRam[0x44] = 0;
        ioRam[0x41] &= 0xF8;
        // Normally timing is synchronized with gameboy's vblank. If the screen 
        // is off, this code kicks in. The "phaseCounter" is a counter until the 
        // ds should check for input and whatnot.
        phaseCounter -= cycles;
        if (phaseCounter <= 0) {
            swiIntrWait(interruptWaitMode, 1);
            fps++;
            phaseCounter += 456*153*(doubleSpeed?2:1);
            if (screenOn) {
                disableScreen();
                screenOn = false;
            }
            if (updateInput())
                return 1;
        }
        return 0;
    }

    scanlineCounter -= cycles;

    switch(ioRam[0x41]&3)
    {
        case 2:
            {
                if (scanlineCounter <= mode2Cycles) {
                    ioRam[0x41]++;
                    setEventCycles(scanlineCounter-mode3Cycles);
                }
                else
                    setEventCycles(scanlineCounter-mode2Cycles);
            }
            break;
        case 3:
            {
                if (scanlineCounter <= mode3Cycles) {
                    ioRam[0x41] &= ~3;

                    if (ioRam[0x41]&0x8)
                    {
                        requestInterrupt(LCD);
                    }

                    drawScanline(ioRam[0x44]);
                    if (updateHblankDMA()) {
                        extraCycles += 50;
                    }

                    setEventCycles(scanlineCounter);
                }
                else
                    setEventCycles(scanlineCounter-mode3Cycles);
            }
            break;
        case 0:
            {
                // fall through to next case
            }
        case 1:
            if (scanlineCounter <= 0)
            {
                scanlineCounter += 456*(doubleSpeed?2:1);
                ioRam[0x44]++;

                if (ioRam[0x44] < 144 || ioRam[0x44] >= 153) {
                    setEventCycles(scanlineCounter-mode2Cycles);
                    ioRam[0x41] &= ~3;
                    ioRam[0x41] |= 2;
                    if (ioRam[0x41]&0x20)
                    {
                        requestInterrupt(LCD);
                    }

                    if (ioRam[0x44] >= 153)
                    {
                        ioRam[0x44] = 0;
                    }
                }
                else if (ioRam[0x44] == 144)
                {
                    ioRam[0x41] &= ~3;
                    ioRam[0x41] |= 1;

                    requestInterrupt(VBLANK);
                    if (ioRam[0x41]&0x10)
                    {
                        requestInterrupt(LCD);
                    }

                    fps++;
                    drawScreen();
                    if (!screenOn) {
                        enableScreen();
                        screenOn = true;
                    }
                    if (updateInput())
                        return 1;
                }
                if (ioRam[0x44] >= 144) {
                    setEventCycles(scanlineCounter);
                }

                // LYC check
                if (ioRam[0x44] == ioRam[0x45])
                {
                    ioRam[0x41] |= 4;
                    if (ioRam[0x41]&0x40)
                        requestInterrupt(LCD);
                }
                else
                    ioRam[0x41] &= ~4;

            }
            else {
                setEventCycles(scanlineCounter);
            }
            break;
    }

    return 0;
}

inline void updateTimers(int cycles)
{
    if (ioRam[0x07] & 0x4)
    {
        timerCounter -= cycles;
        while (timerCounter <= 0)
        {
            timerCounter = timerPeriod + timerCounter;
            if ((++ioRam[0x05]) == 0)
            {
                requestInterrupt(TIMER);
                ioRam[0x05] = ioRam[0x06];
            }
        }
        setEventCycles(timerCounter+timerPeriod*(255-ioRam[0x05]));
    }
    dividerCounter -= cycles;
    if (dividerCounter <= 0)
    {
        dividerCounter = 256+dividerCounter;
        ioRam[0x04]++;
    }
    //setEventCycles(dividerCounter);
}


void requestInterrupt(int id)
{
    ioRam[0x0F] |= id;
    if (ioRam[0x0F] & ioRam[0xFF])
        cyclesToExecute = 0;
}

void setDoubleSpeed(int val) {
    if (val == 0) {
        mode2Cycles = 456 - 80;
        mode3Cycles = 456 - 172 - 80;
        doubleSpeed = 0;
        ioRam[0x4D] &= ~0x80;
    }
    else {
        mode2Cycles = (456 - 80)*2;
        mode3Cycles = (456 - 172 - 80)*2;
        doubleSpeed = 1;
        ioRam[0x4D] |= 0x80;
    }
}
