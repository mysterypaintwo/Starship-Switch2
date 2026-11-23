#include "port/switch/Switch2Debug.h"
#include <libultraship.h>

#include <Fast3D/interpreter.h>
#include "Engine.h"
#include "extractor/GameExtractor.h"
#include <cstdlib>

extern "C" {
#include <sf64mesg.h>
    void Main_SetVIMode(void);
    void Main_Initialize(void);
    void Main_ThreadEntry(void* arg);
    void Lib_FillScreen(u8 setFill);
    void Graphics_ThreadUpdate();
    void AudioThread_CreateTask();
}

extern "C"
void Graphics_PushFrame(Gfx* data) {
    GameEngine::ProcessGfxCommands(data);
}

extern "C" void Timer_Update();

void push_frame() {
    Switch2Prof_Tick();
    Graphics_ThreadUpdate();
    GameEngine::StartAudioFrame();
    GameEngine::Instance->StartFrame();
    Timer_Update();
    // thread5_iteration();
    GameEngine::EndAudioFrame();
#ifdef __SWITCH__
    GameEngine::ProcessVoiceInstall();
#endif
}

#ifdef _WIN32
int SDL_main(int argc, char **argv) {
#else
#if defined(__cplusplus) && defined(PLATFORM_IOS)
extern "C"
#endif
int main(int argc, char *argv[]) {
#endif
    GameEngine::Create();
    Main_SetVIMode();
    Lib_FillScreen(1);
    Switch2Debug_Log("B boot:game-init");
    Main_Initialize();
    Main_ThreadEntry(NULL);
    Switch2Debug_Log("B boot:shaders");
    GameEngine::PrepareShaders();
    Switch2Debug_Log("B boot:first-frame");
    while (WindowIsRunning()) {
        push_frame();
    }
    GameEngine::Instance->Destroy();
#ifdef __SWITCH__
    // Everything is closed. Skip destroying the extractor's globals.
    if (GameExtractor::HasRun()) {
        _Exit(0);
    }
#endif
    return 0;
}