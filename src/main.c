#include <stdio.h>
#include <stdlib.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "hotbar.h"
#include "cmd.h"
#include "editor.h"
#include "explorer.h"
#include "draw.h"
#include "editor.h"
#include "settings.h"

static int g_lastKeyPressed = 0;
static int g_keyDown = 0;
static int g_keyConsumed = 0;

int shiftHeld = 0;
int ctrlHeld = 0;
int screenWidth;
int screenHeight;
int mode;

char* lastOpened;
char* homePath;

extern char* currentFilePath;
extern char** rawLines;

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    screenWidth = width;
    screenHeight = height;
    glViewport(0, 0, width, height);
    cmdRebuildRenderLines(cdata, (float)(screenWidth - (int)(screenWidth * EXPLORER_RATIO) - 20), 1.0f);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    int ctrlPressed = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

	if (g_focus == FOCUS_EXPLORER) {
    	explorerScrollWheel((float)yoffset);
    	return;
	}

    if (g_focus == FOCUS_CMD) {
        cmdScrollWheel((float)yoffset);
        return;
    }

    if (ctrlPressed) {
        editorScrollHorizontal((int)-yoffset);
    } else {
        editorScroll((int)-yoffset);
    }
}

void charCallback(GLFWwindow* window, unsigned int codepoint) {
    if (g_focus == FOCUS_CMD) {
        cmdCharInput(codepoint);
        return;
    }
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if ((action == GLFW_PRESS || action == GLFW_REPEAT) && (mods & GLFW_MOD_CONTROL)) {
        if (key == GLFW_KEY_S) {
            if (shiftHeld == 1) {
                saveFileAs(rawLines);
            } else {
                saveFile(currentFilePath, rawLines);
            }
        } else if (key == GLFW_KEY_O) {
            openFolder();
            loadSettings();
        } else if (key == GLFW_KEY_N) {
            newFile();
        }

        if (key == GLFW_KEY_GRAVE_ACCENT) {
    		if (g_focus == FOCUS_EDITOR) g_focus = FOCUS_EXPLORER;
    		else if (g_focus == FOCUS_EXPLORER) g_focus = FOCUS_CMD;
    		else g_focus = FOCUS_EDITOR;
    		return;
		}
    }

    if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL) {
        ctrlHeld = (action != GLFW_RELEASE);
    }

    if (g_focus == FOCUS_CMD) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            cmdKeyDown(key);
        }
        return;
    }

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) {
            shiftHeld = 1;
            return;
        }

        g_keyDown = key;
        g_keyConsumed = 0;
    }

    if (action == GLFW_RELEASE) {
        if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) {
            shiftHeld = 0;
            return;
        }

        if (g_keyDown == key) {
            g_keyDown = 0;
            g_keyConsumed = 0;
        }
    }
}

int main() {
    if (!glfwInit()) {
        printf("Failed to initialize GLFW");
        return -1;
    }

    loadSettings();

    double mouseX, mouseY;
    int mouseClicked = 0;

    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "MCode", NULL, NULL);
    if (!window) {
        printf("Failed to open GLFW window");
        return -1;
    }

    glfwMaximizeWindow(window);
    glfwMakeContextCurrent(window);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCharCallback(window, charCallback);
    
    cmdStart(settings[2]);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        printf("Failed to initialize GLAD");
        return -1;
    }

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    screenWidth = fbWidth;
    screenHeight = fbHeight;

    glViewport(0, 0, fbWidth, fbHeight);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    float modes[4][3][4] = { // The alpha setting is set to 1 bc it should be invis, but it got flipped somehow, so 1 is solid and 0 is invis
        // Editor color               // Segment color          // Borderline color
        {{0.85f, 0.85f, 0.85f, 1.0f}, {0.9f, 0.9f, 0.9f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}}, // Light mode
        {{1.0f,  1.0f,  1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}, // Light contrast mode
        {{0.15f, 0.15f, 0.15f, 1.0f}, {0.1f, 0.1f, 0.1f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}}, // Dark mode
        {{0.0f,  0.0f,  0.0f,  1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}  // Dark contrast mode
    };

    // Main loop
    while(!glfwWindowShouldClose(window)) {
        mode = atoi(settings[0]);

        // Color set
        glClearColor(modes[mode][0][0], modes[mode][0][1], modes[mode][0][2], modes[mode][0][3]);
        glClear(GL_COLOR_BUFFER_BIT);

        // Main code
        glfwGetCursorPos(window, &mouseX, &mouseY);
        mouseClicked = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

		resetGLState();

        if (mode == 0 || mode == 1) {
            drawHotbar(screenWidth, screenHeight, modes[mode][1], modes[mode][2], (int)mouseX, (int)mouseY, mouseClicked, 0.0f);
			resetGLState();
            drawExplorer(screenWidth, screenHeight, modes[mode][1], modes[mode][2], settings[1], settings[2], 0.0f, (int)mouseX, (int)mouseY, mouseClicked);
			resetGLState();
        } else if (mode == 2 || mode == 3) {
            drawHotbar(screenWidth, screenHeight, modes[mode][1], modes[mode][2], (int)mouseX, (int)mouseY, mouseClicked, 1.0f);
			resetGLState();
            drawExplorer(screenWidth, screenHeight, modes[mode][1], modes[mode][2], settings[1], settings[2], 1.0f, (int)mouseX, (int)mouseY, mouseClicked);
			resetGLState();
        }

        int editorKey = 0;
        int cmdKey = 0;

        if (g_keyDown && !g_keyConsumed) {
            if (g_focus == FOCUS_EDITOR) {
                editorKey = g_keyDown;
            } else {
                cmdKey = g_keyDown;
            }
            g_keyConsumed = 1;
        }


        drawEditor(screenWidth, screenHeight, modes[mode][0], (int)mouseX, (int)mouseY, mouseClicked, editorKey);
		resetGLState();
        drawCMD(screenWidth, screenHeight, modes[mode][1], modes[mode][2], cmdKey);
		resetGLState();
        updateMouseState(mouseClicked);

        GLenum err;
        while ((err = glGetError()) != GL_NO_ERROR) {
            printf("OpenGL error: %d\n", err);
        }

        // Reset stuff ig
        glViewport(0, 0, screenWidth, screenHeight);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

	cmdShutdown();
    freeSettings();
    free(fileChosen);
    fileChosen = NULL;
    return 0;
}