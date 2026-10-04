// The GBA's line to the Smash&Clash SDK: a mailbox in RAM that the mGBA script
// (gba/bridge/smashandclash.lua) carries to the SDK bridge (gba/bridge/bridge.mjs) and back.
#ifndef GBA_BRIDGE_H
#define GBA_BRIDGE_H

void gba_bridge_init(void);
void gba_bridge_poll(void);       // once a frame: notices the script's heartbeat
int gba_bridge_script(void);      // the mGBA script is running
int gba_bridge_connected(void);   // ... and has the SDK bridge on the line

// One frame of the game while an SDK call is out: the platform's main loop body. The
// bridge waits through these, so the screen and the buttons stay alive.
void gba_bridge_frame(void);

#endif
