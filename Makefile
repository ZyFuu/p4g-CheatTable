IMGUI = imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-unused-function -Isrc -Iimgui

preview: build/preview
build/preview: src/main.cpp src/ui.cpp src/game.cpp src/mem_fake.cpp src/*.h $(IMGUI) imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp
	@mkdir -p build
	g++ $(CXXFLAGS) src/main.cpp src/ui.cpp src/game.cpp src/mem_fake.cpp $(IMGUI) imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp -o $@ -lglfw -lGL -ldl

win: build/P4GTrainer.exe
build/trainer.res: src/trainer.rc src/trainer.manifest
	@mkdir -p build
	x86_64-w64-mingw32-windres -I src src/trainer.rc -O coff -o $@
build/P4GTrainer.exe: src/main.cpp src/ui.cpp src/game.cpp src/mem_win.cpp src/*.h $(IMGUI) imgui/backends/imgui_impl_win32.cpp imgui/backends/imgui_impl_dx11.cpp build/trainer.res
	@mkdir -p build
	x86_64-w64-mingw32-g++ $(CXXFLAGS) -municode -mwindows -static src/main.cpp src/ui.cpp src/game.cpp src/mem_win.cpp $(IMGUI) imgui/backends/imgui_impl_win32.cpp imgui/backends/imgui_impl_dx11.cpp build/trainer.res -o $@ -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lgdi32 -limm32 -lxinput1_4
	x86_64-w64-mingw32-strip $@
