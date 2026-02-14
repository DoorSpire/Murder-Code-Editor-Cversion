#include <stdio.h>
#include <dirent/dirent.h>

#include "explorer.h"
#include "draw.h"

static int ex_mouseX = 0;
static int ex_mouseY = 0;
static int ex_screenW = 0;
static int ex_screenH = 0;
static char* currentDir = NULL;
static float explorerScroll = 0.0f;

char* fileChosen;

void explorerScrollWheel(int delta) {
    int explorerW = (int)(ex_screenW * EXPLORER_RATIO);
    int explorerX = 0;
    int explorerY = 81;
    int explorerH = ex_screenH - explorerY;

    if (ex_mouseX >= explorerX && ex_mouseX <= explorerW &&
        ex_mouseY >= explorerY && ex_mouseY <= explorerY + explorerH) {

        explorerScroll += delta * 32.5f;
        if (explorerScroll < 0) explorerScroll = 0;
    }
}

static void drawExplorerBase(int screenWidth, int screenHeight, float color[4]) {
    int explorerW = (int)(screenWidth * EXPLORER_RATIO);
    float vertices[] = {
        pxToNDC_X(0),           pxToNDC_Y(81),           0.0f, // Top left
        pxToNDC_X(explorerW-1), pxToNDC_Y(81),           0.0f, // Top right
        pxToNDC_X(explorerW-1), pxToNDC_Y(screenHeight), 0.0f, // Bottom right
        pxToNDC_X(0),          pxToNDC_Y(screenHeight), 0.0f  // Bottom left
    };

    drawRectangle(vertices, sizeof(vertices), color);
}

static void drawExplorerBorderline(int screenWidth, int screenHeight, float color[4]) {
    int explorerW = (int)(screenWidth * EXPLORER_RATIO);
    float vertices[] = {
        pxToNDC_X(0),         pxToNDC_Y(81),           0.0f, // Top left
        pxToNDC_X(explorerW), pxToNDC_Y(81),           0.0f, // Top right
        pxToNDC_X(explorerW), pxToNDC_Y(screenHeight), 0.0f, // Bottom right
        pxToNDC_X(0),         pxToNDC_Y(screenHeight), 0.0f  // Bottom left
    };

    drawRectangle(vertices, sizeof(vertices), color);
}

void explorerSetCurrentDir(const char* path) {
    if (currentDir) {
        free(currentDir);
    }
    currentDir = _strdup(path);
}

void explorerSetHomeAndCurrent(const char* path) {
    explorerSetCurrentDir(path);

    const char* text = readFile("src/settings/settings.txt");
    if (!text) return;

    char** lines = readText(text);
    free((void*)text);

    if (!lines) return;
    if (lines[1]) free(lines[1]);
    if (lines[2]) free(lines[2]);

    lines[1] = _strdup(path);
    lines[2] = _strdup(path);

    writeText("src/settings/settings.txt", lines);
}

int drawExplorer(int screenWidth, int screenHeight, float color[4], float borderlineColor[4], char* lastOpened, char* homePath, float dark, int mouseX, int mouseY, int mouseClicked) {
    if (screenWidth <= 0 || screenHeight <= 0) return 0;

    static int lastFontH = -1;
    if (screenHeight != lastFontH) {
        initFont(screenHeight);
        lastFontH = screenHeight;
    }

    initFont(screenHeight);
    if (!fontLoaded) {
        printf("Font not loaded!\n");
        return -1;
    }

    if (!currentDir && lastOpened) {
        currentDir = _strdup(lastOpened);
    }

	ex_mouseX = mouseX;
	ex_mouseY = mouseY;
	ex_screenW = screenWidth;
	ex_screenH = screenHeight;

	drawExplorerBorderline(screenWidth, screenHeight, borderlineColor);
    int explorerW = (int)(screenWidth * EXPLORER_RATIO);
    int minLength = explorerW - 51;
    drawExplorerBase(screenWidth, screenHeight, color);

    if (fontLoaded) {
        DIR *d;
        struct dirent *dir;
        if (!currentDir) return 0;
        d = opendir(currentDir);

        if (d) {
            int depth = (int)(200 - explorerScroll);
            int wentBack = 0;

			int totalItems = 0;
			while (readdir(d)) totalItems++;
			rewinddir(d);

			float contentHeight = totalItems * 50.0f + 200.0f;
			float explorerH = screenHeight - 81;

			if (explorerScroll > contentHeight - explorerH) explorerScroll = contentHeight - explorerH;
			if (explorerScroll < 0) explorerScroll = 0;

			glEnable(GL_SCISSOR_TEST);
			int explorerW = (int)(screenWidth * EXPLORER_RATIO);
			int scX = 0;
			int scY = 0;
			int scW = explorerW;
			int scH = (int)(screenHeight - 81);

			if (scW < 0) scW = 0;
			if (scH < 0) scH = 0;
			glScissor(scX, scY, scW, scH);

            if (strcmp(currentDir, homePath) != 0) wentBack = renderButton("Go to home dir", 25, 150, minLength, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);
            if (wentBack) {
                closedir(d);
                explorerSetCurrentDir(homePath);
                const char* text = readFile("src/settings/settings.txt");
                if (text) {
                    char** lines = readText(text);
                    free((void*)text);

                    if (lines) {
                        if (lines[1]) { free(lines[1]); }
                        lines[1] = _strdup(homePath);
                        writeText("src/settings/settings.txt", lines);
                    }
                }
                return 0;
            }

            while ((dir = readdir(d)) != NULL) {
                char* name = dir->d_name;
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) { continue; }
                int clicked = renderButton(name, 25, depth, minLength, 40, screenWidth, screenHeight, mouseX, mouseY, mouseClicked, dark, dark, dark, 1.0f);
                if (clicked) {
                    char* fullPath = malloc(strlen(currentDir) + strlen(name) + 2);
                    if (!fullPath) {
                        perror("malloc failed");
                        closedir(d);
                        return -1;
                    }

                    sprintf(fullPath, "%s\\%s", currentDir, name);
                    if (isDirectory(fullPath)) {
                        closedir(d);
                        explorerSetCurrentDir(fullPath);
                        free(fullPath);
                        const char* text = readFile("src/settings/settings.txt");
                        if (text) {
                            char** lines = readText(text);
                            free((void*)text);

                            if (lines) {
                                if (lines[1]) { free(lines[1]); }
                                lines[1] = _strdup(currentDir);
                                writeText("src/settings/settings.txt", lines);
                            }
                        }
                        return 0;
                    } else {
                        if (fileChosen) free(fileChosen);
                        fileChosen = fullPath;
                    }
                }
                depth += 50;
            }
			glDisable(GL_SCISSOR_TEST);
            closedir(d);
        }
    }
    return 0;
}