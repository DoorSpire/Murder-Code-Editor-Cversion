#ifndef EXPLORER_H
#define EXPLORER_H

void explorerSetHomeAndCurrent(const char* path);
int drawExplorer(int screenWidth, int screenHeight, float color[4], float borderlineColor[4], char* lastOpened, char* homePath, float dark, int mouseX, int mouseY, int mouseClicked);
void explorerScrollWheel(int delta);

#endif