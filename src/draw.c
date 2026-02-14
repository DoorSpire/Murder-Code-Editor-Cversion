#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>

#include "draw.h"
#include "hotbar.h"
#include "explorer.h"

GLuint fontTexture = 0;
int fontLoaded = 0;
stbtt_bakedchar cdata[96];
unsigned char fontBitmap[BITMAP_W * BITMAP_H];

extern int screenWidth;
extern int screenHeight;
extern int mode;

static GLuint textVAO = 0;
static GLuint textVBO = 0;
static GLuint textShaderProgram = 0;
static int textBuffersInitialized = 0;
static GLuint solidProgram = 0;
static int prevMouseDown = 0;
static GLuint solidVAO = 0;
static GLuint solidVBO = 0;
static GLint solidColorLoc = -1;

float pxToNDC_X(int x) { return 2.0f * ((float)x / screenWidth) - 1.0f; }
float pxToNDC_Y(int y) { return 1.0f - 2.0f * ((float)y / screenHeight); }

static unsigned char* loadFile(const char* filename, size_t* outSize) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    rewind(f);
    unsigned char* buffer = (unsigned char*)malloc(size);
    if (!buffer) { fclose(f); return NULL; }
    fread(buffer, 1, size, f);
    fclose(f);
    *outSize = size;
    return buffer;
}

static GLuint compileShaderChecked(const char* src, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        printf("Shader compile error (%s):\n%s\n",
               type == GL_VERTEX_SHADER ? "VS" : "FS", log);
    }
    return shader;
}

static GLuint linkProgramChecked(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof log, NULL, log);
        printf("Program link error:\n%s\n", log);
    }
    glDetachShader(prog, vs);
    glDetachShader(prog, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static void initTextShaderOnce() {
    if (textShaderProgram) return;

    const char* vsSrc =
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "layout(location=2) in vec4 aColor;\n"
        "out vec2 vUV;\n"
        "out vec4 vColor;\n"
        "void main(){\n"
        "  gl_Position = vec4(aPos,1.0);\n"
        "  vUV = aUV;\n"
        "  vColor = aColor;\n"
        "}\n";

    const char* fsSrc =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "in vec4 vColor;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D uTex;\n"
        "void main(){\n"
        "  float a = texture(uTex, vUV).r;\n"
        "  FragColor = vec4(vColor.rgb, a * vColor.a);\n"
        "}\n";

    GLuint vs = compileShaderChecked(vsSrc, GL_VERTEX_SHADER);
    GLuint fs = compileShaderChecked(fsSrc, GL_FRAGMENT_SHADER);
    textShaderProgram = linkProgramChecked(vs, fs);
}

static void initTextBuffersOnce() {
    if (textBuffersInitialized) return;
    textBuffersInitialized = 1;

    glGenVertexArrays(1, &textVAO);
    glGenBuffers(1, &textVBO);

    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);

    glBufferData(GL_ARRAY_BUFFER, MAX_TEXT_CHARS * 6 * 9 * sizeof(float), NULL, GL_DYNAMIC_DRAW);

    // layout: pos (x,y,z)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // layout: uv (s,t)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // layout: color (r,g,b,a)
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

static GLuint createFontTexture() {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, BITMAP_W, BITMAP_H, 0, GL_RED, GL_UNSIGNED_BYTE, fontBitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_RED };
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void initFont(int h) {
    if (fontLoaded) return;

    size_t TTFsize = 0;
    unsigned char* TTFbuffer = loadFile("src/font/codenamecoderfree4f-Bold.ttf", &TTFsize);
    if (!TTFbuffer) {
        printf("Failed to load font file\n");
        return;
    }

    int pixelHeight = h > 0 ? (h / 30) : 18;
    if (stbtt_BakeFontBitmap(TTFbuffer, 0, (float)pixelHeight, fontBitmap, BITMAP_W, BITMAP_H, 32, 96, cdata) <= 0) {
        printf("Failed to bake font bitmap\n");
        free(TTFbuffer);
        return;
    }
    free(TTFbuffer);

    initTextShaderOnce();
    initTextBuffersOnce();

    fontTexture = createFontTexture();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    fontLoaded = 1;
}

static void renderTextChunk(GLuint fontTex, const stbtt_bakedchar* cdata, const char* text, int count, float baseX, float baseY, int screenWidth, int screenHeight, float scale, float r, float g, float b, float a) {
    int maxCount = (int)strlen(text);
    if (count > maxCount) count = maxCount;
    
    float* vertexBuffer = persistentVertexBuffer;
    if (!vertexBuffer) return;

    int vertCount = 0;

    float x = FLOORF(baseX * scale);
    float y = FLOORF(baseY * scale);

    for (int i = 0; i < count; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32 || c >= 128) continue;

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad((stbtt_bakedchar*)cdata, BITMAP_W, BITMAP_H, c - 32, &x, &y, &q, 1);

        q.x0 = FLOORF(q.x0);
        q.y0 = FLOORF(q.y0);
        q.x1 = FLOORF(q.x1);
        q.y1 = FLOORF(q.y1);

        float x0 =  2.0f * q.x0 / screenWidth  - 1.0f;
        float y0 =  1.0f - 2.0f * q.y0 / screenHeight;
        float x1 =  2.0f * q.x1 / screenWidth  - 1.0f;
        float y1 =  1.0f - 2.0f * q.y1 / screenHeight;

        float verts[6][9] = {
            {x0, y0, 0.0f, q.s0, q.t0, r, g, b, a},
            {x1, y0, 0.0f, q.s1, q.t0, r, g, b, a},
            {x1, y1, 0.0f, q.s1, q.t1, r, g, b, a},

            {x0, y0, 0.0f, q.s0, q.t0, r, g, b, a},
            {x1, y1, 0.0f, q.s1, q.t1, r, g, b, a},
            {x0, y1, 0.0f, q.s0, q.t1, r, g, b, a}
        };

        memcpy(&vertexBuffer[vertCount * 9], verts, sizeof(verts));
        vertCount += 6;
    }

    glUseProgram(textShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glUniform1i(glGetUniformLocation(textShaderProgram, "uTex"), 0);

    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertCount * 9 * sizeof(float), vertexBuffer);
    glDrawArrays(GL_TRIANGLES, 0, vertCount);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

int renderText(GLuint fontTex, const stbtt_bakedchar* cdata, const char* text, float x, float y, int screenWidth, int screenHeight, float scale, float r, float g, float b, float a) {
    if (!text || !*text) return 0;

    initTextBuffersOnce();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    x = FLOORF(x);
    y = FLOORF(y);

    int len = (int)strlen(text);
    int offset = 0;

    while (offset < len) {
        int count = len - offset;
        if (count > MAX_TEXT_CHARS)
            count = MAX_TEXT_CHARS;

        renderTextChunk(fontTex, cdata, text + offset, count, x, y, screenWidth, screenHeight, scale, r, g, b, a);
        offset += count;
    }

    return 0;
}

static const char* solidVS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "void main(){ gl_Position = vec4(aPos,1.0); }\n";

static const char* solidFS =
    "#version 330 core\n"
    "uniform vec4 uColor;\n"
    "out vec4 FragColor;\n"
    "void main(){ FragColor = uColor; }\n";

static void ensureSolidProgram() {
    if (solidProgram) return;
    GLuint vs = compileShaderChecked(solidVS, GL_VERTEX_SHADER);
    GLuint fs = compileShaderChecked(solidFS, GL_FRAGMENT_SHADER);
    solidProgram = linkProgramChecked(vs, fs);
}

static void initSolidBuffersOnce() {
    if (solidVAO) return;

    glGenVertexArrays(1, &solidVAO);
    glGenBuffers(1, &solidVBO);

    glBindVertexArray(solidVAO);
    glBindBuffer(GL_ARRAY_BUFFER, solidVBO);

    glBufferData(GL_ARRAY_BUFFER, 6 * 3 * sizeof(float), NULL, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    ensureSolidProgram();
    solidColorLoc = glGetUniformLocation(solidProgram, "uColor");
}

void drawTriangle(float vertices[], size_t size, float color[4]) {
    initSolidBuffersOnce(); // lazy init

    glUseProgram(solidProgram);
    if (solidColorLoc != -1) {
        glUniform4f(solidColorLoc, color[0], color[1], color[2], color[3]);
    }

    glBindVertexArray(solidVAO);

    glBindBuffer(GL_ARRAY_BUFFER, solidVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, size, vertices);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glUseProgram(0);
}

void drawRectangle(float vertices[], size_t size, float color[4]) {
    float triA[] = {
        vertices[0], vertices[1], vertices[2],
        vertices[3], vertices[4], vertices[5],
        vertices[6], vertices[7], vertices[8],
    };
    float triB[] = {
        vertices[0],  vertices[1],  vertices[2],
        vertices[6],  vertices[7],  vertices[8],
        vertices[9],  vertices[10], vertices[11]
    };
    drawTriangle(triA, sizeof(triA), color);
    drawTriangle(triB, sizeof(triB), color);
}

const char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return strdup("Could not open file");
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fclose(file);
        return strdup("Not enough memory to read file");
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fclose(file);
        free(buffer);
        return strdup("Could not read file");
    }

    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

char** readText(const char* text) {
    if (!text) return NULL;

    int lines = 1;
    for (const char* p = text; *p; p++) {
        if (*p == '\n') lines++;
    }

    char** result = malloc((lines + 1) * sizeof(char*));
    if (!result) return NULL;

    const char* start = text;
    int lineIndex = 0;

    for (const char* p = text; ; p++) {
        if (*p == '\n' || *p == '\0') {
            int len = p - start;

            if (len > 0 && start[len-1] == '\r') len--;

            char* line = malloc(len + 1);
            memcpy(line, start, len);
            line[len] = '\0';
            result[lineIndex++] = line;

            if (*p == '\0') break;
            start = p + 1;
        }
    }

    result[lineIndex] = NULL;
    return result;
}

void writeText(const char* path, char** lines) {
    FILE* f = fopen(path, "w");
    if (!f) return;

    for (int i = 0; lines[i]; i++) {
        fputs(lines[i], f);
        if (lines[i + 1]) fputc('\n', f);
        free(lines[i]);
    }

    free(lines);
    fclose(f);
}

int renderButton(const char* label, float x, float y, float width, float height, int screenWidth, int screenHeight, int mouseX, int mouseY, int mouseClicked, float r, float g, float b, float a) {
    if (!fontLoaded) return 0;

    int clicked = 0;
    int inside = (mouseX >= x && mouseX <= x + width && mouseY >= y && mouseY <= y + height);

    if (mouseClicked && !prevMouseDown && inside) clicked = 1;

    if (inside) {
        float bgColor[4] = { mouseClicked ? 1.0f : 0.8f, 0.0f, 0.0f, 1.0f };
        float vertices[] = {
            pxToNDC_X((int)x),              pxToNDC_Y((int)y),               0.0f,
            pxToNDC_X((int)(x + width)),    pxToNDC_Y((int)y),               0.0f,
            pxToNDC_X((int)(x + width)),    pxToNDC_Y((int)(y + height)),    0.0f,
            pxToNDC_X((int)x),              pxToNDC_Y((int)(y + height)),    0.0f
        };
        drawRectangle(vertices, sizeof(vertices), bgColor);
    }

    renderText(fontTexture, cdata, label, x + 10, y + height - 10, screenWidth, screenHeight, 1.0f, r, g, b, a);
    return clicked;
}

void updateMouseState(int mouseClicked) {
    prevMouseDown = mouseClicked;
}

int isDirectory(const char *path) {
    DWORD attrib = GetFileAttributes(path);
    if (attrib == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return (attrib & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

char* preprocessText(const char* text) {
    const char* green  = "[color=#00FF00]";
    const char* grey   = "[color=#424242]";
    const char* red    = "[color=#FF0000]";
    const char* purple = "[color=#800080]";

    const char* usual = (mode == 2 || mode == 3) ? "[color=#FFFFFF]" : "[color=#000000]";

    const char* redWords[]    = {"and", "or", "if", "else", "true", "false", "null", NULL};
    const char* purpleWords[] = {"class", "for", "def", "return", "super", "this", "int", "while", NULL};

    int inQuotes = 0;
    int inComment = 0;

    size_t len = strlen(text);

    size_t maxTag = strlen(green);
    if (strlen(usual) > maxTag)  maxTag = strlen(usual);
    if (strlen(grey) > maxTag)   maxTag = strlen(grey);
    if (strlen(red) > maxTag)    maxTag = strlen(red);
    if (strlen(purple) > maxTag) maxTag = strlen(purple);

    size_t maxLen = len * (maxTag + 8);
    char* newText = malloc(maxLen);
    if (!newText) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len;) {
        char c = text[i];

        if (!inComment && i + 1 < len && c == '/' && text[i+1] == '/') {
            strcpy(&newText[j], green);
            j += strlen(green);
            newText[j++] = '/';
            newText[j++] = '/';
            inComment = 1;
            i += 2;
        } else if (inComment) {
            if (c == '\n') {
                strcpy(&newText[j], usual);
                j += strlen(usual);
                newText[j++] = '\n';
                inComment = 0;
                i++;
            } else {
                newText[j++] = c;
                i++;
            }
        } else if (c == '"') {
            if (!inQuotes) {
                strcpy(&newText[j], green);
                j += strlen(green);
                newText[j++] = '"';
                inQuotes = 1;
            } else {
                newText[j++] = '"';
                strcpy(&newText[j], usual);
                j += strlen(usual);
                inQuotes = 0;
            }
            i++;
        } else if (c == '(' || c == ')' || c == '{' || c == '}' || c == ';' || c == '+' || c == '-' || c == '=' || c == '*') {
            strcpy(&newText[j], grey);
            j += strlen(grey);
            newText[j++] = c;
            strcpy(&newText[j], usual);
            j += strlen(usual);
            i++;
        } else if (isalpha(c)) {
            char word[128];
            size_t k = 0;
            while (i < len && (isalnum(text[i]) || text[i] == '_')) {
                if (k < sizeof(word) - 1)
                    word[k++] = text[i];
                i++;
            }
            word[k] = '\0';

            int isRed = 0, isPurple = 0;
            for (int r = 0; redWords[r]; r++) {
                if (strcmp(word, redWords[r]) == 0) { isRed = 1; break; }
            }
            for (int p = 0; purpleWords[p]; p++) {
                if (strcmp(word, purpleWords[p]) == 0) { isPurple = 1; break; }
            }

            if (isRed) {
                strcpy(&newText[j], red);
                j += strlen(red);
                strcpy(&newText[j], word);
                j += strlen(word);
                strcpy(&newText[j], usual);
                j += strlen(usual);
            } else if (isPurple) {
                strcpy(&newText[j], purple);
                j += strlen(purple);
                strcpy(&newText[j], word);
                j += strlen(word);
                strcpy(&newText[j], usual);
                j += strlen(usual);
            } else {
    			strcpy(&newText[j], usual);
    			j += strlen(usual);
    			strcpy(&newText[j], word);
    			j += strlen(word);
			}
        } else {
            newText[j++] = c;
            i++;
        }
    }

    if (inComment) {
        strcpy(&newText[j], usual);
        j += strlen(usual);
    }

    newText[j] = '\0';
    return newText;
}

void saveFile(const char* path, char** lines) {
    if (!path) printf("saveFile: path is NULL\n");
    if (!lines) printf("saveFile: lines is NULL\n");
    FILE* f = fopen(path, "w");
    if (!f) return;

    for (int i = 0; lines[i]; i++) {
        fprintf(f, "%s\n", lines[i]);
    }

    fclose(f);
}

void saveFileAs(char** lines) {
    if (!lines) return;

    char filePath[MAX_PATH] = {0};

    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);

    ofn.lpstrFilter = "N++ source file (*.npp)\0*.c\0" "All Files (*.*)\0*.*\0";

    ofn.lpstrFile = filePath;
    ofn.nMaxFile  = MAX_PATH;
    ofn.lpstrTitle = "Save As";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    ofn.nFilterIndex = 1;

    if (!GetSaveFileNameA(&ofn)) {
        return;
    }

    if (!strrchr(filePath, '.')) {
        const char* ext = "txt";

        switch (ofn.nFilterIndex) {
            case 1: ext = "npp"; break;
        }

        strcat(filePath, ".");
        strcat(filePath, ext);
    }

    FILE* f = fopen(filePath, "w");
    if (!f) return;

    for (int i = 0; lines[i]; i++) {
        fprintf(f, "%s\n", lines[i]);
    }

    fclose(f);
}

char* newFile() {
    char filePath[MAX_PATH] = {0};

    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);

    ofn.lpstrFilter = "N++ source file (*.npp)\0*.c\0" "All Files (*.*)\0*.*\0";

    ofn.lpstrFile = filePath;
    ofn.nMaxFile  = MAX_PATH;
    ofn.lpstrTitle = "Save As";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    ofn.nFilterIndex = 1;

    if (!GetSaveFileNameA(&ofn)) {
        return NULL;
    }

    if (!strrchr(filePath, '.')) {
        const char* ext = "npp";

        switch (ofn.nFilterIndex) {
            case 1: ext = "npp";   break;
        }

        strcat(filePath, ".");
        strcat(filePath, ext);
    }

    FILE* f = fopen(filePath, "w");
    if (!f) return NULL;

    fprintf(f, "A fresh file!\nWhat will you write?\n");
    fclose(f);

    char* result = malloc(strlen(filePath) + 1);
    if (!result) return NULL;
    strcpy(result, filePath);
    return result;
}

void openFolder() {
    char chosen[MAX_PATH] = {0};
    int got = 0;

    BROWSEINFO bi = {0};
    LPITEMIDLIST pidl = NULL;
    char path[MAX_PATH];

    bi.lpszTitle = "Select a folder:";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    pidl = SHBrowseForFolder(&bi);
    if (pidl) {
        if (SHGetPathFromIDList(pidl, path)) {
            strncpy(chosen, path, MAX_PATH);
            chosen[MAX_PATH-1] = '\0';
            got = 1;
        }
        CoTaskMemFree(pidl);
    }

    if (got) {
        explorerSetHomeAndCurrent(chosen);
    }
}

float getTextWidth(stbtt_bakedchar* cdata, const char* text, float scale) {
    float width = 0.0f;

    for (const char* p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;

        if (c < 32 || c >= 128) {
            c = 'e';
        }

        width += cdata[c - 32].xadvance * scale;
    }

    return width;
}

float getTextWidthRange(stbtt_bakedchar* cdata, const char* text, int count, float scale) {
    float width = 0.0f;

    for (int i = 0; i < count && text[i]; i++) {
        stbtt_bakedchar* b = &cdata[(int)text[i] - 32];
        width += b->xadvance * scale;
    }

    return width;
}

static void parse_hex_to_rgb(const char* s, float* r, float* g, float* b, float* a) {
    *r = *g = *b = 1.0f;
    *a = 1.0f;
    if (!s) return;
    if (s[0] == '#') s++;
    if (strlen(s) < 6) return;
    unsigned int v = 0;
    if (sscanf(s, "%6x", &v) == 1) {
        unsigned int rv = (v >> 16) & 0xFF;
        unsigned int gv = (v >> 8) & 0xFF;
        unsigned int bv = (v >> 0) & 0xFF;
        *r = rv / 255.0f;
        *g = gv / 255.0f;
        *b = bv / 255.0f;
        *a = 1.0f;
    }
}

float renderColoredText(GLuint fontTex, const stbtt_bakedchar* cdata, const char* text, float x, float y, int screenW, int screenH, float scale) {
    if (!text) return x;

    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    const char* p = text;
    const char* chunkStart = p;

    while (*p) {
        if (*p == '[') {
            if (strncmp(p, "[color=", 7) == 0) {
                const char* colorStart = p + 7;
                const char* end = strchr(colorStart, ']');
                if (!end) { p++; continue; }

                if (p > chunkStart) {
                    size_t len = (size_t)(p - chunkStart);
                    char* chunk = (char*)malloc(len + 1);
                    if (!chunk) return x;
                    memcpy(chunk, chunkStart, len);
                    chunk[len] = '\0';
                    renderText(fontTex, cdata, chunk, x, y, screenW, screenH, scale, r, g, b, a);
                    x += getTextWidth((stbtt_bakedchar*)cdata, chunk, scale);
                    free(chunk);
                }

                size_t colorLen = (size_t)(end - colorStart);
                char colorBuf[32];
                if (colorLen >= sizeof(colorBuf)) colorLen = sizeof(colorBuf) - 1;
                memcpy(colorBuf, colorStart, colorLen);
                colorBuf[colorLen] = '\0';

                if (strcmp(colorBuf, "#") == 0) {
                    r = g = b = 1.0f;
                    a = 1.0f;
                } else {
                    parse_hex_to_rgb(colorBuf, &r, &g, &b, &a);
                }

                p = end + 1;
                chunkStart = p;
                continue;
            } else if (p[1] == '#') {
                const char* colorStart = p + 1;
                const char* end = strchr(colorStart, ']');
                if (!end) { p++; continue; }

                if (p > chunkStart) {
                    size_t len = (size_t)(p - chunkStart);
                    char* chunk = (char*)malloc(len + 1);
                    if (!chunk) return x;
                    memcpy(chunk, chunkStart, len);
                    chunk[len] = '\0';
                    renderText(fontTex, cdata, chunk, x, y, screenW, screenH, scale, r, g, b, a);
                    x += getTextWidth((stbtt_bakedchar*)cdata, chunk, scale);
                    free(chunk);
                }

                size_t colorLen = (size_t)(end - colorStart);
                char colorBuf[32];
                if (colorLen >= sizeof(colorBuf)) colorLen = sizeof(colorBuf) - 1;
                memcpy(colorBuf, colorStart, colorLen);
                colorBuf[colorLen] = '\0';

                if (strcmp(colorBuf, "#") == 0) {
                    r = g = b = 1.0f; a = 1.0f;
                } else {
                    parse_hex_to_rgb(colorBuf, &r, &g, &b, &a);
                }

                p = end + 1;
                chunkStart = p;
                continue;
            }
        }
        p++;
    }

    if (p > chunkStart) {
        size_t len = (size_t)(p - chunkStart);
        char* chunk = (char*)malloc(len + 1);
        if (!chunk) return x;
        memcpy(chunk, chunkStart, len);
        chunk[len] = '\0';
        renderText(fontTex, cdata, chunk, x, y, screenW, screenH, scale, r, g, b, a);
        x += getTextWidth((stbtt_bakedchar*)cdata, chunk, scale);
        free(chunk);
    }

    return x;
}

void normalizeSelection(const TextSelection* sel, int* sl, int* sc, int* el, int* ec) {
    *sl = sel->startLine;
    *sc = sel->startCol;
    *el = sel->endLine;
    *ec = sel->endCol;

    if (*sl > *el || (*sl == *el && *sc > *ec)) {
        int tl = *sl, tc = *sc;
        *sl = *el; *sc = *ec;
        *el = tl;  *ec = tc;
    }
}

static inline void selectionSet(TextSelection* s, int sl, int sc, int el, int ec) {
    s->active    = 1;
    s->startLine = sl;
    s->startCol  = sc;
    s->endLine   = el;
    s->endCol    = ec;
}

static inline int selectionIsSingleLine(const TextSelection* s) {
    return s->active && s->startLine == s->endLine;
}

char* clipboardGetText(void) {
    if (!OpenClipboard(NULL)) return NULL;

    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) {
        CloseClipboard();
        return NULL;
    }

    char* src = (char*)GlobalLock(hData);
    if (!src) {
        CloseClipboard();
        return NULL;
    }

    char* out = _strdup(src);

    GlobalUnlock(hData);
    CloseClipboard();

    return out;
}

void clipboardSetText(const char* text) {
    if (!text) return;
    if (!OpenClipboard(NULL)) return;

    EmptyClipboard();

    size_t len = strlen(text) + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hMem) {
        CloseClipboard();
        return;
    }

    char* dst = (char*)GlobalLock(hMem);
    memcpy(dst, text, len);
    GlobalUnlock(hMem);

    SetClipboardData(CF_TEXT, hMem);
    CloseClipboard();
}

void drawSelectionRect(float x1, float y1, float x2, float y2, float color[4]) {
    if (x2 < x1) {
        float tmp = x1;
        x1 = x2;
        x2 = tmp;
    }

    if (y2 < y1) {
        float tmp = y1;
        y1 = y2;
        y2 = tmp;
    }

    float verts[] = {
        pxToNDC_X((int)x1), pxToNDC_Y((int)y1), 0.0f,
        pxToNDC_X((int)x2), pxToNDC_Y((int)y1), 0.0f,
        pxToNDC_X((int)x2), pxToNDC_Y((int)y2), 0.0f,
        pxToNDC_X((int)x1), pxToNDC_Y((int)y2), 0.0f
    };

    drawRectangle(verts, sizeof(verts), color);
}

int caretIndexFromMouse(const char* text, float mouseX) {
    if (!text || !*text) return 0;

    float x = 0.0f;
    int index = 0;

    for (const char* p = text; *p; p++) {
        int c = (unsigned char)*p;

        if (c < 32 || c > 126) continue;

        stbtt_bakedchar* b = &cdata[c - 32];
        float advance = b->xadvance;

        if (mouseX < x + advance * 0.5f) return index;

        x += advance;
        index++;
    }

    return index;
}

void resetGLState() {
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}