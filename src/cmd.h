#ifndef CMD_H
#define CMD_H

#include "draw.h"

void drawCMD(int screenWidth, int screenHeight, float color[4], float borderlineColor[4], int keyPressed);
void cmdRebuildRenderLines(stbtt_bakedchar *cdata, float maxWidth, float scale);
void cmdStart(const char* homePath);
void cmdCharInput(unsigned int codepoint);
void cmdKeyDown(int key);
void cmdScrollWheel(float delta);
void cmdShutdown();

#endif