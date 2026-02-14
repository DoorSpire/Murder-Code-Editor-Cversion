#define STB_TRUETYPE_IMPLEMENTATION

#include <stdio.h>
#include <glad/glad.h>
#include <stdlib.h>

#include "hotbar.h"
#include "draw.h"
#include "settings.h"
#include "editor.h"

extern char* currentFilePath;
extern char** rawLines;

static void drawHotbarBase(int screenWidth, int screenHeight, float color[4]) {
    int barHeight = 80;

    float vertices[] = {
        pxToNDC_X(0),               pxToNDC_Y(0),         0.0f, // Top left
        pxToNDC_X(screenWidth),     pxToNDC_Y(0),         0.0f, // Top right
        pxToNDC_X(screenWidth),     pxToNDC_Y(barHeight), 0.0f, // Bottom right
        pxToNDC_X(0),               pxToNDC_Y(barHeight), 0.0f  // Bottom left
    };

    drawRectangle(vertices, sizeof(vertices), color);
}

static void drawHotbarBorderline(int screenWidth, int screenHeight, float color[4]) {
    int barHeight = 81;

    float vertices[] = {
        pxToNDC_X(0),               pxToNDC_Y(0),         0.0f, // Top left
        pxToNDC_X(screenWidth),     pxToNDC_Y(0),         0.0f, // Top right
        pxToNDC_X(screenWidth),     pxToNDC_Y(barHeight), 0.0f, // Bottom right
        pxToNDC_X(0),               pxToNDC_Y(barHeight), 0.0f  // Bottom left
    };

    drawRectangle(vertices, sizeof(vertices), color);
}

int drawHotbar(int screenWidth, int screenHeight, float color[4], float borderlineColor[4], int mouseX, int mouseY, int mouseClicked, float dark) {
    if (screenWidth <= 0 || screenHeight <= 0) return 0;
    
    initFont(screenHeight);
    if (!fontLoaded) {
        printf("Font not loaded!\n");
        return -1;
    }

    drawHotbarBorderline(screenWidth, screenHeight, borderlineColor);
    drawHotbarBase(screenWidth, screenHeight, color);

    if (fontLoaded) {
        int file = renderButton("File", 25, 30, 80, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);
        int mode = renderButton("Mode", 125, 30, 80, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);

        static int showFile = 0;
        static int showMode = 0;

        if (file == 1) { if (showFile == 1) { showFile = 0; } else { showFile = 1; showMode = 0; } }
        if (mode == 1) { if (showMode == 1) { showMode = 0; } else { showFile = 0; showMode = 1; } }

        if (showFile == 1) {
            static int newWasDown = 0;
            int New = renderButton("|New", 425, 30, 75, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);
            int OpenFolder = renderButton("|Open Folder", 500, 30, 225, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);
            int Save = renderButton("|Save", 725, 30, 100, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);
            int SaveAs = renderButton("|Save As|", 825, 30, 160, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);

            if (OpenFolder) { openFolder(); loadSettings(); }
            if (Save) saveFile(currentFilePath, rawLines);
            if (SaveAs) saveFileAs(rawLines);
            if (New && !newWasDown) { free(currentFilePath); currentFilePath = newFile(); }
            newWasDown = New;
        }

        if (showMode == 1) {
            int Dark = renderButton("|Dark", 425, 30, 90, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);
            int DarkContrast = renderButton("|Dark Contrast", 525, 30, 275, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);
            int Light = renderButton("|Light", 785, 30, 100, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);
            int LightContrast = renderButton("|Light Contrast|", 900, 30, 275, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);

            const char* text = readFile("src/settings/settings.txt");
            if (!text) return 0;
            char** lines = readText(text);
            free((void*)text);

            if (lines && lines[0]) {
				rebuildRenderLines();
                if (Dark) { free(lines[0]); lines[0] = strdup("2"); }
                else if (DarkContrast) { free(lines[0]); lines[0] = strdup("3"); }
                else if (Light) { free(lines[0]); lines[0] = strdup("0"); }
                else if (LightContrast) { free(lines[0]); lines[0] = strdup("1"); }
            }

            writeText("src/settings/settings.txt", lines);
            loadSettings();
        }
    }
}