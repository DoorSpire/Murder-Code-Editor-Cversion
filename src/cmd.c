#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glad/glad.h>
#include <math.h>

#include "draw.h"
#include "explorer.h"
#include "editor.h"
#include "cmd.h"

#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
typedef HANDLE HPCON;
typedef HRESULT (WINAPI *CreatePseudoConsole_t)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef VOID (WINAPI *ClosePseudoConsole_t)(HPCON);

static CreatePseudoConsole_t pCreatePseudoConsole = NULL;
static ClosePseudoConsole_t  pClosePseudoConsole  = NULL;

#define CMD_MAX_LINES 256
#define CMD_LINE_HEIGHT 32
#define CMD_VIEW_HEIGHT 250.0f
#define CMD_MAX_RAW_LINES 2048
#define CMD_MAX_RENDER_LINES 8192
#define CMD_LINE_LEN 1024

static HPCON  hPC = NULL;
static HANDLE hPtyIn = NULL;
static HANDLE hPtyOut = NULL;
static PROCESS_INFORMATION cmdProc = {0};
static STARTUPINFOEXA si = {0};
static int cmdAccumLen = 0;
static int cmdRunning = 0;
static int cmdRawCount = 0;
static int cmdRenderCount = 0;
static int cmdScreenWidth = 0;
static int cmdExplorerW = 0;
static int cmdLayoutDirty = 0;
static int cmdStartTried = 0;
static char cmdAccum[16384];
static char *cmdRawLines[CMD_MAX_RAW_LINES];
static char *cmdRenderLines[CMD_MAX_RENDER_LINES];
static float cmdScroll = 0.0f;

extern int mode;

static float getTextWidthRange2(const stbtt_bakedchar *cdata, const char *text, int start, int count, float scale) {
    char tmp[CMD_LINE_LEN];
    if (count <= 0) return 0.0f;
    if (count >= CMD_LINE_LEN) count = CMD_LINE_LEN - 1;

    memcpy(tmp, text + start, count);
    tmp[count] = 0;

    return getTextWidth((stbtt_bakedchar*)cdata, tmp, scale);
}

static float cmdMaxScroll() {
    float h = (cmdRenderCount + 1) * CMD_LINE_HEIGHT;
    float max = h - CMD_VIEW_HEIGHT;
    return max > 0 ? max : 0;
}

void cmdPushRawLine(const char *s) {
    if (!s || !*s) return;

    if (cmdRawCount >= CMD_MAX_RAW_LINES) {
        free(cmdRawLines[0]);
        memmove(cmdRawLines, cmdRawLines + 1, sizeof(char*) * (CMD_MAX_RAW_LINES - 1));
        cmdRawCount--;
    }

    cmdRawLines[cmdRawCount++] = _strdup(s);
}

static void deleteAnsi(char *s) {
    char out[CMD_LINE_LEN * 8];
    char *d = out;
    size_t remaining = sizeof(out);

    const char *p = s;

    while (*p && remaining > 1) {
        if (*p == '\x1b' && p[1] == '[') {
            p += 2;
            while (*p && !((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) p++;
            if (*p) p++;
            continue;
        }

        *d++ = *p++;
        remaining--;
    }

    *d = 0;

    strncpy(s, out, CMD_LINE_LEN - 1);
    s[CMD_LINE_LEN - 1] = 0;
}

void cmdRebuildRenderLines(stbtt_bakedchar *cdata, float maxWidth, float scale) {
    if (!cdata || maxWidth <= 0) return;
    for (int i = 0; i < cmdRenderCount; i++) free(cmdRenderLines[i]);

    cmdRenderCount = 0;

    for (int i = 0; i < cmdRawCount; i++) {
        char work[CMD_LINE_LEN * 2];
        strncpy(work, cmdRawLines[i], sizeof(work) - 1);
        work[sizeof(work) - 1] = 0;

        deleteAnsi(work);

        const char *p = work;
        int len = (int)strlen(work);
        int start = 0;

        while (start < len) {
            int rawCount = 0;
            float w = 0.0f;

            while (start + rawCount < len) {
                float next = getTextWidthRange2(cdata, p + start, 0, rawCount + 1, scale);
                if (next > maxWidth && rawCount > 0) break;
                rawCount++;
                w = next;
            }

            if (rawCount <= 0) rawCount = 1;

            char *out = (char*)malloc(rawCount + 1);
            memcpy(out, p + start, rawCount);
            out[rawCount] = 0;

            if (cmdRenderCount < CMD_MAX_RENDER_LINES)
                cmdRenderLines[cmdRenderCount++] = out;
            else
                free(out);

            start += rawCount;
        }
    }
}

static void ptyWrite(const char *s) {
    if (!hPtyIn) return;
    DWORD w;
    WriteFile(hPtyIn, s, (DWORD)strlen(s), &w, NULL);
}

void cmdCharInput(unsigned int codepoint) {
    if (!cmdRunning) return;
    if (codepoint < 32 || codepoint > 126) return;

    char c = (char)codepoint;
    char s[2] = { c, 0 };
    ptyWrite(s);
}

void cmdKeyDown(int key) {
    if (!cmdRunning) return;

    switch (key) {
        case GLFW_KEY_BACKSPACE: ptyWrite("\x08"); break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:  ptyWrite("\r"); break;

        case GLFW_KEY_HOME: ptyWrite("\x1b[H"); break;
        case GLFW_KEY_END:  ptyWrite("\x1b[F"); break;

        case GLFW_KEY_DELETE: ptyWrite("\x1b[3~"); break;
    }
}

void cmdStart(const char *homePath) {
    if (cmdStartTried) return;
    cmdStartTried = 1;
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    pCreatePseudoConsole = (CreatePseudoConsole_t)GetProcAddress(k32, "CreatePseudoConsole");
    pClosePseudoConsole = (ClosePseudoConsole_t)GetProcAddress(k32, "ClosePseudoConsole");

    if (!pCreatePseudoConsole) {
        cmdPushRawLine("ConPTY not supported");
        return;
    }

    HANDLE inR, inW, outR, outW;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    CreatePipe(&inR, &inW, &sa, 0);
    CreatePipe(&outR, &outW, &sa, 0);

    COORD size = { 120, 30 };
    pCreatePseudoConsole(size, inR, outW, 0, &hPC);

    CloseHandle(inR);
    CloseHandle(outW);

    hPtyIn  = inW;
    hPtyOut = outR;

    si.StartupInfo.cb = sizeof(si);

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
    si.lpAttributeList = malloc(attrSize);
    InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize);

    UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC, sizeof(hPC), NULL, NULL);

    char cmdLine[] = "powershell.exe";
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, homePath, &si.StartupInfo, &cmdProc)) {
        cmdPushRawLine("Failed to start cmd.exe");
        if (si.lpAttributeList) {
        	DeleteProcThreadAttributeList(si.lpAttributeList);
        	free(si.lpAttributeList);
        	si.lpAttributeList = NULL;
    	}

    	if (pClosePseudoConsole && hPC) {
    	    pClosePseudoConsole(hPC);
    	    hPC = NULL;
    	}

    	CloseHandle(hPtyIn);
    	CloseHandle(hPtyOut);

    	return;
    }

    cmdRunning = 1;
    cmdLayoutDirty = 1;

	ptyWrite("Remove-Module PSReadLine\r");
	ptyWrite("$env:TERM='dumb'\r");
	ptyWrite("function prompt { 'PS ' + (Get-Location) + '> ' }\r");
}

void cmdScrollWheel(float delta) {
    cmdScroll -= delta * CMD_LINE_HEIGHT;
    if (cmdScroll < 0) cmdScroll = 0;

    float max = cmdMaxScroll();
    if (cmdScroll > max) cmdScroll = max;
}

static void drawCMDBase(int w, int h, float color[4]) {
    int x = (int)(w * EXPLORER_RATIO);
    float v[] = {
        pxToNDC_X(x), pxToNDC_Y(h - CMD_VIEW_HEIGHT), 0,
        pxToNDC_X(w), pxToNDC_Y(h - CMD_VIEW_HEIGHT), 0,
        pxToNDC_X(w), pxToNDC_Y(h), 0,
        pxToNDC_X(x), pxToNDC_Y(h), 0
    };
    drawRectangle(v, sizeof(v), color);
}

static void drawCMDBorder(int w, int h, float color[4]) {
    int x = (int)(w * EXPLORER_RATIO);
    float v[] = {
        pxToNDC_X(x), pxToNDC_Y(h - CMD_VIEW_HEIGHT - 1), 0,
        pxToNDC_X(w), pxToNDC_Y(h - CMD_VIEW_HEIGHT - 1), 0,
        pxToNDC_X(w), pxToNDC_Y(h), 0,
        pxToNDC_X(x), pxToNDC_Y(h), 0
    };
    drawRectangle(v, sizeof(v), color);
}

void cmdShutdown() {
    if (cmdRunning) {
        TerminateProcess(cmdProc.hProcess, 0);
        CloseHandle(cmdProc.hProcess);
        CloseHandle(cmdProc.hThread);
        CloseHandle(hPtyIn);
        CloseHandle(hPtyOut);
        pClosePseudoConsole(hPC);
        cmdRunning = 0;
    }

	for (int i = 0; i < CMD_MAX_RAW_LINES; i++) {
        if (cmdRawLines[i]) {
            free(cmdRawLines[i]);
            cmdRawLines[i] = NULL;
        }
    }

    for (int i = 0; i < CMD_MAX_RENDER_LINES; i++) {
        if (cmdRenderLines[i]) {
            free(cmdRenderLines[i]);
            cmdRenderLines[i] = NULL;
        }
    }

    cmdRawCount = 0;
    cmdRenderCount = 0;

	if (si.lpAttributeList) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
        si.lpAttributeList = NULL;
    }
}

void drawCMD(int screenWidth, int screenHeight, float bg[4], float border[4], int keyPressed) {
    cmdScreenWidth = screenWidth;
    cmdExplorerW = (int)(screenWidth * EXPLORER_RATIO);
    if (!cmdRunning) {
        cmdStart(NULL);
        if (!cmdRunning) return;
    }

    if (cmdLayoutDirty) {
        int explorerW = (int)(screenWidth * EXPLORER_RATIO);
        cmdRebuildRenderLines(cdata, (float)(screenWidth - explorerW - 20), 1.0f);

        cmdScroll = cmdMaxScroll();
        cmdLayoutDirty = 0;
    }

    DWORD avail;
    while (PeekNamedPipe(hPtyOut, NULL, 0, NULL, &avail, NULL) && avail > 0) {
        char buf[512];
        DWORD r = 0;

        if (!ReadFile(hPtyOut, buf, sizeof(buf), &r, NULL) || r == 0) break;
        if (cmdAccumLen + r >= (int)sizeof(cmdAccum) - 1) cmdAccumLen = 0;

        memcpy(cmdAccum + cmdAccumLen, buf, r);
		cmdAccumLen += r;
		cmdAccum[cmdAccumLen] = 0;

        char *lineStart = cmdAccum;
        char *nl;

        int pushed = 0;

        for (;;) {
            char *cr = memchr(cmdAccum, '\r', cmdAccumLen);
            char *lf = memchr(cmdAccum, '\n', cmdAccumLen);

            char *nl = NULL;
            int nlLen = 0;

            if (cr && lf) {
                if (cr < lf) {
                    nl = cr;
                    nlLen = (cr + 1 < cmdAccum + cmdAccumLen && cr[1] == '\n') ? 2 : 1;
                } else {
                    nl = lf;
                    nlLen = (lf + 1 < cmdAccum + cmdAccumLen && lf[1] == '\r') ? 2 : 1;
                }
            } else if (cr) {
                nl = cr;
                nlLen = (cr + 1 < cmdAccum + cmdAccumLen && cr[1] == '\n') ? 2 : 1;
            } else if (lf) {
                nl = lf;
                nlLen = (lf + 1 < cmdAccum + cmdAccumLen && lf[1] == '\r') ? 2 : 1;
            } else {
                break;
            }

            int lineLen = (int)(nl - cmdAccum);

            if (lineLen > 0) {
                char tmp[CMD_LINE_LEN];
                if (lineLen >= CMD_LINE_LEN) lineLen = CMD_LINE_LEN - 1;
                memcpy(tmp, cmdAccum, lineLen);
                tmp[lineLen] = 0;

                cmdPushRawLine(tmp);
                pushed = 1;
            }

            int consumed = lineLen + nlLen;
            memmove(cmdAccum, cmdAccum + consumed, cmdAccumLen - consumed);
            cmdAccumLen -= consumed;
        }

        if (pushed) {
            int explorerW = (int)(screenWidth * EXPLORER_RATIO);
            cmdRebuildRenderLines(cdata, (float)(screenWidth - explorerW - 20), 1.0f);
            cmdScroll = cmdMaxScroll();
            pushed = 0;
        }
    }

	drawCMDBorder(screenWidth, screenHeight, border);
    drawCMDBase(screenWidth, screenHeight, bg);

    glEnable(GL_SCISSOR_TEST);

    int explorerW = (int)(screenWidth * EXPLORER_RATIO);
    int scissorX = explorerW;
    int scissorY = 0;
    int scissorW = screenWidth - explorerW;
    int scissorH = (int)CMD_VIEW_HEIGHT;

    if (scissorW <= 0 || scissorH <= 0) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }

    glScissor(scissorX, scissorY, scissorW, scissorH);

    float contentTop = screenHeight - CMD_VIEW_HEIGHT;
    float bufferBottom = contentTop + (cmdRenderCount + 1) * CMD_LINE_HEIGHT;
    float y = bufferBottom - cmdScroll;
    float x = explorerW;

    int firstLine = (int)(cmdScroll / CMD_LINE_HEIGHT);
    int visibleLines = (int)(CMD_VIEW_HEIGHT / CMD_LINE_HEIGHT) + 2;
    int lastLine = firstLine + visibleLines;

    if (lastLine > cmdRenderCount) lastLine = cmdRenderCount;

    float drawY = screenHeight - CMD_VIEW_HEIGHT - fmodf(cmdScroll, CMD_LINE_HEIGHT);

    for (int i = firstLine; i < lastLine; i++) {
        if (mode == 0 || mode == 1) renderText(fontTexture, cdata, cmdRenderLines[i], x, drawY, screenWidth, screenHeight, 1.0f, 0,0,0,1);
		else renderText(fontTexture, cdata, cmdRenderLines[i], x, drawY, screenWidth, screenHeight, 1.0f, 1,1,1,1);
        drawY += CMD_LINE_HEIGHT;
    }
    glDisable(GL_SCISSOR_TEST);
}