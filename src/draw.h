#ifndef DRAW_H
#define DRAW_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stddef.h>
#include <FreeType/stb_truetype.h>

#define BITMAP_W 512
#define BITMAP_H 512
#define EXPLORER_RATIO 0.3f
#define MAX_TEXT_CHARS 8192
#define MAX_VERTEX_BUFFER_SIZE (MAX_TEXT_CHARS * 6 * 9)
#define FLOORF(x) ((float)((int)(x)))
#define MAX_LINE_LEN 4096

static float persistentVertexBuffer[MAX_VERTEX_BUFFER_SIZE];
static int caretLine = 0;
static int caretCol = 0;
static int caretMoved = 0;
static int lineCount = 0;
static int lineCapacity = 0;

extern stbtt_bakedchar cdata[96];
extern unsigned char fontBitmap[BITMAP_W * BITMAP_H];
extern GLuint fontTexture;
extern int fontLoaded;
extern int shiftHeld;
extern char* fileChosen;

typedef struct {
    const char* label;
    float x;
    float y;
    float width;
    float height;
    int clicked;
} Button;

typedef enum {
    FOCUS_EDITOR,
    FOCUS_CMD,
	FOCUS_EXPLORER
} InputFocus;

typedef struct {
    int active;
    int startLine;
    int startCol;
    int endLine;
    int endCol;
} TextSelection;

static InputFocus g_focus = FOCUS_EDITOR;

float pxToNDC_X(int x);
float pxToNDC_Y(int y);

void initFont(int screenHeight);
int renderText(GLuint fontTex, const stbtt_bakedchar* cdata, const char* text, float x, float y, int screenWidth, int screenHeight, float scale, float r, float g, float b, float a);

void drawTriangle(float vertices[], size_t size, float color[4]);
void drawRectangle(float vertices[], size_t size, float color[4]);

char** readText(const char* text);
const char* readFile(const char* path);
void writeText(const char* path, char** lines);
int isDirectory(const char *path);

int renderButton(const char* label, float x, float y, float width, float height, int screenWidth, int screenHeight, int mouseX, int mouseY, int mouseClicked, float r, float g, float b, float a);
void updateMouseState(int mouseClicked);

char* preprocessText(const char* text);

void saveFile(const char* path, char** lines);
void saveFileAs(char** lines);
void openFolder();
char* newFile();

float getTextWidth(stbtt_bakedchar* cdata, const char* text, float scale);
float getTextWidthRange(stbtt_bakedchar* cdata, const char* text, int count, float scale);
static void parse_hex_to_rgb(const char* s, float* r, float* g, float* b, float* a);
float renderColoredText(GLuint fontTex, const stbtt_bakedchar* cdata, const char* text, float x, float y, int screenW, int screenH, float scale);

static inline void selectionSet(TextSelection* s, int sl, int sc, int el, int ec);
void normalizeSelection(const TextSelection* sel, int* sl, int* sc, int* el, int* ec);
char* clipboardGetText();
void clipboardSetText(const char* text);
void drawSelectionRect(float x1, float y1, float x2, float y2, float color[4]);
int caretIndexFromMouse(const char* text, float mouseX);

void resetGLState();

#endif