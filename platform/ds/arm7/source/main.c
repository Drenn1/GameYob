/*---------------------------------------------------------------------------------

  This is file derived from:

	default ARM7 core

		Copyright (C) 2005 - 2010
		Michael Noland (joat)
		Jason Rogers (dovoto)
		Dave Murphy (WinterMute)

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any
	damages arising from the use of this software.

	Permission is granted to anyone to use this software for any
	purpose, including commercial applications, and to alter it and
	redistribute it freely, subject to the following restrictions:

	1.	The origin of this software must not be misrepresented; you
		must not claim that you wrote the original software. If you use
		this software in a product, an acknowledgment in the product
		documentation would be appreciated but is not required.

	2.	Altered source versions must be plainly marked as such, and
		must not be misrepresented as being the original software.

	3.	This notice may not be removed or altered from any source
		distribution.

---------------------------------------------------------------------------------*/
#include <nds.h>
#include <dswifi7.h>
#include "common.h"

void installGameboySoundFIFO();

void sdmmcMsgHandler(int bytes, void *user_data);
void sdmmcValueHandler(u32 value, void* user_data);
extern bool sleepIsEnabled;

volatile SharedData* sharedData;

void updateChannel(int c);

bool lidClosed = false;

void VblankHandler(void) {
    sharedData->frameFlip_DS = !sharedData->frameFlip_DS;
    sharedData->dsCycles = 0;

    if (sharedData->scalingOn) {
        // Copy from vram bank D to C
        dmaCopyWords(3, (u16*)0x06000000+24*256, (u16*)0x06020000, 256*144*2);
        sharedData->scaleTransferReady = false;
    }

scaling_end:
    Wifi_Update();
}

void VcountHandler() {
    inputGetAndSend();
}

volatile bool exitflag = false;

void powerButtonCB() {
    exitflag = true;
}

// Custom version of a handler in libnds.
// Tells the arm9 we're entering sleep mode to prevent crashes relating to audio 
// code. It's a weird hack but it seems to work.
void powerHandler(u32 value, void* user_data) {
    u32 temp;
    u32 ie_save;
    int battery, backlight, power;

    switch(value & 0xFFFF0000) {
        //power control
        case PM_REQ_LED:
            ledBlink(value);
            break;
        case PM_REQ_ON:
            temp = readPowerManagement(PM_CONTROL_REG);
            writePowerManagement(PM_CONTROL_REG, temp | (value & 0xFFFF));
            break;
        case PM_REQ_OFF:
            temp = readPowerManagement(PM_CONTROL_REG) & (~(value & 0xFFFF));
            writePowerManagement(PM_CONTROL_REG, temp);
            break;

        case PM_REQ_SLEEP:

            // Signal to disable sound
            fifoSendValue32(FIFO_USER_02, FIFOMSG_LID_CLOSED);

            ie_save = REG_IE;
            // Turn the speaker down.
            if (REG_POWERCNT & 1) swiChangeSoundBias(0,0x400);
            // Save current power state.
            power = readPowerManagement(PM_CONTROL_REG);
            // Set sleep LED.
            writePowerManagement(PM_CONTROL_REG, PM_LED_CONTROL(1));
            // Register for the lid interrupt.
            REG_IE = IRQ_LID;

            // Power down till we get our interrupt.
            swiSleep(); //waits for PM (lid open) interrupt

            //100ms
            swiDelay(838000);

            // Restore the interrupt state.
            REG_IE = ie_save;

            // Restore power state.
            writePowerManagement(PM_CONTROL_REG, power);

            // Turn the speaker up.
            if (REG_POWERCNT & 1) swiChangeSoundBias(1,0x400); 

            // update clock tracking
            resyncClock();

            // Signal to re-enable sound
            fifoSendValue32(FIFO_USER_02, FIFOMSG_LID_OPENED);
            break;

        case PM_REQ_SLEEP_DISABLE:
            sleepIsEnabled = false;
            break;

        case PM_REQ_SLEEP_ENABLE:
            sleepIsEnabled = true;
            break;
        case PM_REQ_BATTERY:
            if (!__dsimode) {
                battery = (readPowerManagement(PM_BATTERY_REG) & 1)?3:15;
                backlight = readPowerManagement(PM_BACKLIGHT_LEVEL);
                if (backlight & (1<<6)) battery += (backlight & (1<<3))<<4;
            } else {
                battery = i2cReadRegister(I2C_PM,I2CREGPM_BATTERY);
            }
            fifoSendValue32(FIFO_SYSTEM, battery);
            break;
        case PM_DSI_HACK:
            __dsimode = true;
            break;
    }
}
//---------------------------------------------------------------------------------
int main() {
    //---------------------------------------------------------------------------------
    readUserSettings();
    ledBlink(0);

    irqInit();
    // Start the RTC tracking IRQ
    //initClockIRQ(); //windwakr: Doesn't seem to work on 3DS.
    resyncClock();

    fifoInit();
    touchInit();

    while (!fifoCheckValue32(FIFO_USER_03));
    sharedData = (SharedData*)(fifoGetValue32(FIFO_USER_03) | 0x02000000);

    SetYtrigger(80);

    installWifiFIFO();
    installSoundFIFO();

    installSystemFIFO();
    // Replaces handler established in installSystemFIFO().
    fifoSetValue32Handler(FIFO_PM, powerHandler, 0);

    irqSet(IRQ_VCOUNT, VcountHandler);
    irqSet(IRQ_VBLANK, VblankHandler);

    irqEnable( IRQ_VBLANK | IRQ_VCOUNT | IRQ_NETWORK);

    setPowerButtonCB(powerButtonCB);   


    installGameboySoundFIFO();


    while (!exitflag) {
        if ( 0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R))) {
            exitflag = true;
        }
        swiWaitForVBlank();
    }
    return 0;
}
