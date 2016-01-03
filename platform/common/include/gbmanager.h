class Gameboy;

extern Gameboy* gameboy;
extern Gameboy* gb2;

extern Gameboy* hostGb;

void mgr_init();
void mgr_runFrame();
void mgr_startGb2(const char* filename);
void mgr_swapFocus();
void mgr_setInternalClockGb(Gameboy* g);
bool mgr_isInternalClockGb(Gameboy* g);
bool mgr_isExternalClockGb(Gameboy* g);
void mgr_pause();
void mgr_unpause();
bool mgr_isPaused();

void mgr_loadRom(const char* filename);
void mgr_selectRom();
void mgr_save();

void mgr_updateVBlank();

void mgr_exit();
