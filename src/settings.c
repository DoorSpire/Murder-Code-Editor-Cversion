#include <stdlib.h>

#include "settings.h"
#include "draw.h"

char **settings = NULL;

void loadSettings() {
    if (settings) {
        for (int i = 0; settings[i]; i++)
            free(settings[i]);
        free(settings);
        settings = NULL;
    }

    const char *text = readFile("src/settings/settings.txt");
    if (!text) return;

    settings = readText(text);
    free((void*)text);
}

void freeSettings() {
    if (!settings) return;
    for (int i = 0; settings[i]; i++) free(settings[i]);
    free(settings);
    settings = NULL;
}