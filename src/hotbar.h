#ifndef HOTBAR_H
#define HOTBAR_H

#include <glad/glad.h>

extern GLuint fontTexture;
extern int fontLoaded;

int drawHotbar(int screenWidth, int screenHeight, float color[4], float borderlineColor[4], int mouseX, int mouseY, int mouseClicked, float dark);

#endif