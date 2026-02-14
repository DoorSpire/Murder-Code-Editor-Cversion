#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "editor.h"
#include "draw.h"

static float scrollOffset = 0.0f;
static float scrollOffsetX = 0.0f;
static int g_mouseX = 0;
static int g_mouseY = 0;
static int g_screenWidth = 0;
static int g_screenHeight = 0;
static int selecting = 0;
static int mouseDown = 0;
static char* loadedFile = NULL;
static TextSelection editorSel = {0, 0, 0};

char** rawLines = NULL;
char** renderLines = NULL;
char* currentFilePath = NULL;

extern int ctrlHeld;
extern int mode;

void selectionClear(TextSelection* sel) {
    sel->active    = 0;
    sel->startLine = 0;
    sel->startCol  = 0;
    sel->endLine   = 0;
    sel->endCol    = 0;
}

void deleteSelection(char** lines) {
    int sl, sc, el, ec;
    normalizeSelection(&editorSel, &sl, &sc, &el, &ec);

    if (sl == el) {
        char* line = lines[sl];
        memmove(&line[sc], &line[ec], strlen(line) - ec + 1);
        caretLine = sl;
        caretCol  = sc;
    } else {
        char* first = lines[sl];
        char* last  = lines[el];

        first[sc] = '\0';
        strcat(first, &last[ec]);

        for (int i = el; i > sl; i--) {
            free(lines[i]);
            for (int j = i; j < lineCount; j++) lines[j] = lines[j + 1];
            lineCount--;
        }

        caretLine = sl;
        caretCol  = sc;
    }

    selectionClear(&editorSel);
}

void freeLines(char** lines) {
    if (!lines) return;
    for (int i = 0; lines[i]; i++) {
        free(lines[i]);
    }
    free(lines);
}

char* joinLines(char **lines) {
    if (!lines) return NULL;

    size_t total = 0;
    for (int i = 0; lines[i]; i++) {
        total += strlen(lines[i]) + 1; // + newline
    }

    char *out = malloc(total + 1);
    if (!out) return NULL;

    out[0] = '\0';
    for (int i = 0; lines[i]; i++) {
        strcat(out, lines[i]);
        strcat(out, "\n");
    }

    return out;
}

void rebuildRenderLines() {
    if (renderLines) {
        freeLines(renderLines);
        renderLines = NULL;
    }

    char *joined = joinLines(rawLines);
    if (!joined) return;

    char *processed = preprocessText(joined);
    free(joined);

    renderLines = readText(processed);
    free(processed);
}

void insertTextAtCaret(const char* text) {
    if (!text || !*text || !rawLines || !rawLines[caretLine]) return;

    if (editorSel.active) {
        deleteSelection(rawLines);
    }

    const char* p = text;
    while (*p) {
        if (*p == '\n') {
            char* line = rawLines[caretLine];
            int len = (int)strlen(line);

            if (lineCapacity <= 0) lineCapacity = 16;
            if (lineCount + 1 >= lineCapacity) {
                lineCapacity *= 2;
                rawLines = realloc(rawLines, lineCapacity * sizeof(char*));
            }

            for (int i = lineCount; i > caretLine; i--)
                rawLines[i] = rawLines[i - 1];

            char* newLine = calloc(MAX_LINE_LEN, 1);
            strncpy(newLine, &line[caretCol], MAX_LINE_LEN - 1);
            line[caretCol] = '\0';

            rawLines[caretLine + 1] = newLine;
            lineCount++;
            rawLines[lineCount] = NULL;

            caretLine++;
            caretCol = 0;
            p++;
            continue;
        }

        char* line = rawLines[caretLine];
        int len = (int)strlen(line);

        if (len >= MAX_LINE_LEN - 1) {
            p++;
            continue;
        }

        memmove(&line[caretCol + 1], &line[caretCol], len - caretCol + 1);
        line[caretCol] = *p;
        caretCol++;

        p++;
    }

    rebuildRenderLines();
}

void editorKeyDown(int key, char** lines) {
    if (!lines || !lines[caretLine]) return;

    char* line = lines[caretLine];
    int lineLen = (int)strlen(line);

    if (key == GLFW_KEY_BACKSPACE && editorSel.active) {
        deleteSelection(lines);
        rebuildRenderLines();
        return;
    }

    if (ctrlHeld && key == GLFW_KEY_C && editorSel.active) {
        int sl, sc, el, ec;
        normalizeSelection(&editorSel, &sl, &sc, &el, &ec);

        size_t total = 0;
        for (int i = sl; i <= el; i++) {
            int start = (i == sl) ? sc : 0;
            int end   = (i == el) ? ec : strlen(lines[i]);
            total += (end - start) + 1; // + newline
        }

        char* buf = malloc(total + 1);
        char* p = buf;

        for (int i = sl; i <= el; i++) {
            int start = (i == sl) ? sc : 0;
            int end   = (i == el) ? ec : strlen(lines[i]);
            memcpy(p, &lines[i][start], end - start);
            p += end - start;
            if (i != el) *p++ = '\n';
        }
        *p = '\0';

        clipboardSetText(buf);
        free(buf);
        return;
    }

    if (ctrlHeld && key == GLFW_KEY_V) {
        char* clip = clipboardGetText();
        if (clip) {
            insertTextAtCaret(clip);
            free(clip);
        }
        return;
    }

    switch (key) {
        case GLFW_KEY_LEFT:
            if (caretCol > 0) {
                caretCol--;
            } else if (caretLine > 0) {
                caretLine--;
                caretCol = (int)strlen(lines[caretLine]);
            }
            selectionClear(&editorSel);
            break;
        case GLFW_KEY_RIGHT:
            if (caretCol < lineLen) {
                caretCol++;
            } else if (lines[caretLine + 1]) {
                caretLine++;
                caretCol = 0;
            }
            selectionClear(&editorSel);
            break;
        case GLFW_KEY_UP:
            if (caretLine > 0) {
                caretLine--;
                caretMoved = 1;
                int prevLen = (int)strlen(lines[caretLine]);
                if (caretCol > prevLen) caretCol = prevLen;
            }
            selectionClear(&editorSel);
            break;
        case GLFW_KEY_DOWN:
            if (lines[caretLine + 1]) {
                caretLine++;
                caretMoved = 1;
                int nextLen = (int)strlen(lines[caretLine]);
                if (caretCol > nextLen) caretCol = nextLen;
            }
            selectionClear(&editorSel);
            break;
        case GLFW_KEY_BACKSPACE:
            if (caretCol > 0) {
                memmove(&line[caretCol - 1], &line[caretCol], (lineLen - caretCol) + 1);
                caretCol--;
            } else if (caretLine > 0) {
                int prevLen = (int)strlen(lines[caretLine - 1]);
                lines[caretLine - 1] = realloc(lines[caretLine - 1], prevLen + lineLen + 1);
                if (!lines[caretLine - 1]) return;

                strcat(lines[caretLine - 1], line);
                free(line);

                for (int i = caretLine; i < lineCount; i++) {
                    lines[i] = lines[i + 1];
                }
                lineCount--;
                caretLine--;
                caretCol = prevLen;
            }
            rebuildRenderLines();
            break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER: {
            if (lineCapacity <= 0) lineCapacity = 16;
            if (lineCount + 1 >= lineCapacity) {
                lineCapacity *= 2;
                rawLines = realloc(rawLines, lineCapacity * sizeof(char*));
                lines = rawLines;
            }

            for (int i = lineCount; i > caretLine; i--) {
                lines[i] = lines[i - 1];
            }

            char* newLine = calloc(MAX_LINE_LEN, 1);
            if (!newLine) return;

            strncpy(newLine, &line[caretCol], MAX_LINE_LEN - 1);
            line[caretCol] = '\0';

            lines[caretLine + 1] = newLine;
            lineCount++;
            lines[lineCount] = NULL;

            caretLine++;
            caretCol = 0;

            rebuildRenderLines();
            break;
        }
        default:
            if (key >= 32 && key <= 126) {
                if (lineLen >= MAX_LINE_LEN - 1) break;
                if (ctrlHeld) break;
                memmove(&line[caretCol + 1], &line[caretCol], (lineLen - caretCol) + 1);

                char c = tolower((char)key);
                if (shiftHeld == 1) c = toupper(c);
                if (shiftHeld == 1 && (ispunct((char)key) || isdigit((char)key))) {
                    switch ((char)key) {
                        case '1': c = '!'; break;
                        case '2': c = '@'; break;
                        case '3': c = '#'; break;
                        case '4': c = '$'; break;
                        case '5': c = '%'; break;
                        case '6': c = '^'; break;
                        case '7': c = '&'; break;
                        case '8': c = '*'; break;
                        case '9': c = '('; break;
                        case '0': c = ')'; break;
                        case '-': c = '_'; break;
                        case '=': c = '+'; break;
                        case '[': c = '{'; break;
                        case ']': c = '}'; break;
                        case '\\': c = '|'; break;
                        case ';': c = ':'; break;
                        case '\'': c = '"'; break;
                        case ',': c = '<'; break;
                        case '.': c = '>'; break;
                        case '/': c = '?'; break;
                        case '`': c = '~'; break;
                    }
                }

                line[caretCol] = c;
                caretCol++;
                line[lineLen + 1] = '\0';
                rebuildRenderLines();
            }
            break;
    }

    int maxLen = (int)strlen(lines[caretLine]);
    if (caretCol < 0) caretCol = 0;
    if (caretCol > maxLen) caretCol = maxLen;
}

static void drawEditorBase(int screenWidth, int screenHeight, float color[4]) {
    int explorerW = (int)(screenWidth * EXPLORER_RATIO);
    float vertices[] = {
        pxToNDC_X(explorerW),   pxToNDC_Y(81),               0.0f,
        pxToNDC_X(screenWidth), pxToNDC_Y(81),               0.0f,
        pxToNDC_X(screenWidth), pxToNDC_Y(screenHeight-250), 0.0f,
        pxToNDC_X(explorerW),   pxToNDC_Y(screenHeight-250), 0.0f
    };

    drawRectangle(vertices, sizeof(vertices), color);
}

void editorScroll(int delta) {
    int editorX = (int)(g_screenWidth * EXPLORER_RATIO);
    int editorY = 81;
    int editorW = g_screenWidth - editorX;
    int editorH = g_screenHeight - 250 - editorY;

    if (g_mouseX >= editorX && g_mouseX <= editorX + editorW && g_mouseY >= editorY && g_mouseY <= editorY + editorH) {
        scrollOffset += delta * 32.5f;
        if (scrollOffset < 0) scrollOffset = 0;
    }
}

void editorScrollHorizontal(int delta) {
    int editorX = (int)(g_screenWidth * EXPLORER_RATIO);
    int editorY = 81;
    int editorW = g_screenWidth - editorX;
    int editorH = g_screenHeight - 250 - editorY;

    if (g_mouseX >= editorX && g_mouseX <= editorX + editorW && g_mouseY >= editorY && g_mouseY <= editorY + editorH) {
        scrollOffsetX += delta * 32.5f;
        if (scrollOffsetX < 0) scrollOffsetX = 0;
    }
}

float measureTextWidth(const char* text, stbtt_bakedchar* cdata, float scale) {
    float width = 0.0f;

    for (const char* p = text; *p; p++) {
        stbtt_bakedchar* b = &cdata[(int)*p - 32];
        width += b->xadvance * scale;
    }

    return width;
}

int drawEditor(int screenWidth, int screenHeight, float color[4], int mouseX, int mouseY, int mouseClicked, int keyPressed) {
    if (screenWidth <= 0 || screenHeight <= 0) return 0;

    static int lastFontH = -1;
    if (screenHeight != lastFontH) {
        initFont(screenHeight);
        lastFontH = screenHeight;
    }

    g_mouseX = mouseX;
    g_mouseY = mouseY;
    g_screenWidth = screenWidth;
    g_screenHeight = screenHeight;

    initFont(screenHeight);
    if (!fontLoaded) {
        printf("Font not loaded!\n");
        return -1;
    }

    drawEditorBase(screenWidth, screenHeight, color);

    float editorX = (int)(screenWidth * EXPLORER_RATIO);
    float editorY = 81;
    float editorW = screenWidth - editorX;
    float editorH = screenHeight - 250 - editorY;

    static int prevMouseClicked = 0;

    int mousePressed  =  mouseClicked && !prevMouseClicked;
    int mouseReleased = !mouseClicked &&  prevMouseClicked;
    int mouseHeld     =  mouseClicked;

    prevMouseClicked = mouseClicked;

    if (fontLoaded && fileChosen) {
        if (!loadedFile || strcmp(loadedFile, fileChosen) != 0) {
            freeLines(rawLines);
            freeLines(renderLines);

            rawLines = NULL;
            renderLines = NULL;

            caretLine = 0;
            caretCol = 0;
            scrollOffset = 0.0f;
            scrollOffsetX = 0.0f;

            free(loadedFile);
            loadedFile = _strdup(fileChosen);
            free(currentFilePath);
            currentFilePath = _strdup(fileChosen);
        }

        if (!rawLines) {
            const char* text = readFile(fileChosen);
            rawLines = readText(text);
            lineCount = 0;
            while (rawLines[lineCount]) lineCount++;

            lineCapacity = lineCount + 128;
            rawLines = realloc(rawLines, lineCapacity * sizeof(char*));
            rawLines[lineCount] = NULL;

            for (int i = 0; i < lineCount; i++) {
                char* buf = calloc(MAX_LINE_LEN, 1);
                if (!buf) continue;
                strncpy(buf, rawLines[i], MAX_LINE_LEN - 1);
                free(rawLines[i]);
                rawLines[i] = buf;
            }

            char *processed = preprocessText(text);
            renderLines = readText(processed);
            free(processed);
            free((void*)text);
        }

        char** lines = rawLines;
        if (!lines || !lines[caretLine]) return -1;

        if (keyPressed) {
            editorKeyDown(keyPressed, lines);
        }

        float lineHeight = 32.5f;
        float yStart = 125.0f - scrollOffset;

        if (mousePressed) {
            int clickedLine = (int)((mouseY - yStart) / lineHeight);
            if (clickedLine >= 0 && clickedLine < lineCount) {
                caretLine = clickedLine;

                float editorTextX = editorX + 10.0f - scrollOffsetX;
                caretCol = caretIndexFromMouse(rawLines[caretLine], mouseX - editorTextX);

                editorSel.startLine = caretLine;
                editorSel.startCol  = caretCol;
                editorSel.endLine   = caretLine;
                editorSel.endCol    = caretCol;
                editorSel.active = 1;
                selecting = 1;
            }
        }

        if (mouseReleased && selecting) {
            selecting = 0;
        }

        if (selecting && mouseHeld) {
            int hoveredLine = (int)((mouseY - yStart) / lineHeight);
            if (hoveredLine < 0) hoveredLine = 0;
            if (hoveredLine >= lineCount) hoveredLine = lineCount - 1;

            caretLine = hoveredLine;

            float editorTextX = editorX + 10.0f - scrollOffsetX;
            caretCol = caretIndexFromMouse(rawLines[caretLine], mouseX - editorTextX);

            editorSel.endLine = caretLine;
            editorSel.endCol  = caretCol;
        }

        int numLines = 0;
        for (; lines[numLines]; numLines++);
        float contentHeight = numLines * lineHeight;
        if (scrollOffset > contentHeight - editorH + lineHeight) scrollOffset = contentHeight - editorH + lineHeight;

        float maxLineWidth = 0.0f;
        for (int i = 0; lines[i]; i++) {
            float width = getTextWidth(cdata, lines[i], 1.0f);
            if (width > maxLineWidth) maxLineWidth = width;
        }

        if (scrollOffsetX > maxLineWidth - editorW + 40.0f) scrollOffsetX = maxLineWidth - editorW + 40.0f;
        if (scrollOffsetX < 0) scrollOffsetX = 0;

        glEnable(GL_SCISSOR_TEST);
        int scissorX = (int)editorX;
        int scissorY = screenHeight - (int)(editorY + editorH);
        int scissorW = (int)editorW;
        int scissorH = (int)editorH;
		if (scissorW <= 0 || scissorH <= 0) {
            glDisable(GL_SCISSOR_TEST);
            return 0;
        }
        glScissor(scissorX, scissorY, scissorW, scissorH);

        for (int i = 0; lines[i]; i++) {
            float lineY = yStart + i * lineHeight;
            lineY = FLOORF(lineY);
            if (lineY + lineHeight < editorY || lineY > editorY + editorH) continue;

            char buffer[4096];
            char buffer2[128];

            snprintf(buffer2, sizeof(buffer2), "%d|", i + 1);
            strncpy(buffer, renderLines[i], sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';

            float numberX = editorX + 5.0f;
            float numberWidth = measureTextWidth(buffer2, cdata, 1.0f);
            float textX = editorX + numberWidth + 10.0f - scrollOffsetX;
            textX = FLOORF(textX);

            if (i == caretLine) {
                char renderedCaret[4096];
                renderedCaret[0] = '\0';

                int rawCount = 0;
                for (const char* p = renderLines[i]; *p && rawCount < caretCol; p++) {
                    if (*p == '[') {
                        const char* end = strchr(p, ']');
                        if (!end) break;
                        p = end;
                        continue;
                    }

                    size_t len = strlen(renderedCaret);
                    renderedCaret[len] = *p;
                    renderedCaret[len + 1] = '\0';
                    rawCount++;
                }

                float caretX = textX + measureTextWidth(renderedCaret, cdata, 1.0f);
                float caretY = lineY - 25.0f;
                float caretWidth = 2.0f;
                float caretHeight = lineHeight;

                caretX = FLOORF(caretX);
                caretY = FLOORF(caretY);

                float cx1 = pxToNDC_X((int)caretX);
                float cy1 = pxToNDC_Y((int)caretY);
                float cx2 = pxToNDC_X((int)(caretX + caretWidth));
                float cy2 = pxToNDC_Y((int)(caretY + caretHeight));

                float caretVerts[] = {
                    cx1, cy1, 0.0f,
                    cx2, cy1, 0.0f,
                    cx2, cy2, 0.0f,
                    cx1, cy2, 0.0f
                };

                float caretColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				if (mode == 0 || mode == 1) memcpy(caretColor, black, sizeof(caretColor));
                drawRectangle(caretVerts, sizeof(caretVerts), caretColor);
            }

            if (editorSel.active) {
                int sl, sc, el, ec;
                normalizeSelection(&editorSel, &sl, &sc, &el, &ec);

                if (i < sl || i > el) {
                    // no selection on this line
                } else {
                    int lineLen = strlen(lines[i]);

                    int selStartCol = (i == sl) ? sc : 0;
                    int selEndCol   = (i == el) ? ec : lineLen;

                    float x1 = textX + getTextWidthRange(cdata, lines[i], selStartCol, 1.0f);
                    float x2 = textX + getTextWidthRange(cdata, lines[i], selEndCol,   1.0f);

                    float selTop    = lineY - lineHeight + 6.0f;
                    float selBottom = lineY + 6.0f;

                    drawSelectionRect(
                        x1, selTop, x2, selBottom,
                        (float[]){0.25f, 0.25f, 0.5f, 0.5f}
                    );
                }
            }

            if (editorSel.active) {
                int sl, sc, el, ec;
                normalizeSelection(&editorSel, &sl, &sc, &el, &ec);

                if (i >= sl && i <= el) {
                    int lineLen = strlen(lines[i]);

                    int a = (i == sl) ? sc : 0;
                    int b = (i == el) ? ec : lineLen;

                    if (a < 0) a = 0;
                    if (b > lineLen) b = lineLen;

                    if (a != b) {
                        char dbg[1024];
                        int len = b - a;
                        if (len > 1023) len = 1023;

                        memcpy(dbg, &lines[i][a], len);
                        dbg[len] = '\0';
                    }
                }
            }

            if (mode == 0 || mode == 1) renderText(fontTexture, cdata, buffer2, numberX, lineY, screenWidth, screenHeight, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
			else renderText(fontTexture, cdata, buffer2, numberX, lineY, screenWidth, screenHeight, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

            renderColoredText(fontTexture, cdata, buffer, textX, lineY, screenWidth, screenHeight, 1.0f);
        }

        if (caretMoved) {
            float caretY = yStart + caretLine * lineHeight;
            if (caretY < editorY) scrollOffset = caretLine * lineHeight; else if (caretY + lineHeight > editorY + editorH) scrollOffset = caretLine * lineHeight - editorH + lineHeight;
            caretMoved = 0;
        }

        glDisable(GL_SCISSOR_TEST);
    }
    return 0;
}