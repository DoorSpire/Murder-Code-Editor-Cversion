#ifndef EDITOR_H
#define EDITOR_H

void rebuildRenderLines();
void editorScroll(int delta);
void editorScrollHorizontal(int delta);
int drawEditor(int screenWidth, int screenHeight, float color[4], int mouseX, int mouseY, int mouseClicked, int keyPressed);

#endif