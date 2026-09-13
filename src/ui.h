#pragma once
const int UI_WIDTH = 1280, UI_HEIGHT = 760;
void UiInit(float dpiScale);
void UiFrame(double now);
void UiShutdown();
void UiSelectTab(int tab);
