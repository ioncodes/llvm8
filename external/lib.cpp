#include <stdio.h>
#include <Windows.h>
#include <thread>

#include "SDL2/SDL.h"

#define SCALE 16

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;

decltype(SDL_Init)* _SDL_Init = nullptr;
decltype(SDL_CreateWindow)* _SDL_CreateWindow = nullptr;
decltype(SDL_CreateRenderer)* _SDL_CreateRenderer = nullptr;
decltype(SDL_SetRenderDrawColor)* _SDL_SetRenderDrawColor = nullptr;
decltype(SDL_RenderClear)* _SDL_RenderClear = nullptr;
decltype(SDL_RenderPresent)* _SDL_RenderPresent = nullptr;
decltype(SDL_RenderDrawPoint)* _SDL_RenderDrawPoint = nullptr;
decltype(SDL_RenderSetScale)* _SDL_RenderSetScale = nullptr;
decltype(SDL_PollEvent)* _SDL_PollEvent = nullptr;
decltype(SDL_GetTicks)* _SDL_GetTicks = nullptr;
decltype(SDL_Delay)* _SDL_Delay = nullptr;

extern "C" void init()
{
    auto sdl = LoadLibraryA("SDL2.dll");
    _SDL_Init = (decltype(SDL_Init)*)GetProcAddress(sdl, "SDL_Init");
    _SDL_CreateWindow = (decltype(SDL_CreateWindow)*)GetProcAddress(sdl, "SDL_CreateWindow");
    _SDL_CreateRenderer = (decltype(SDL_CreateRenderer)*)GetProcAddress(sdl, "SDL_CreateRenderer");
    _SDL_SetRenderDrawColor = (decltype(SDL_SetRenderDrawColor)*)GetProcAddress(sdl, "SDL_SetRenderDrawColor");
    _SDL_RenderClear = (decltype(SDL_RenderClear)*)GetProcAddress(sdl, "SDL_RenderClear");
    _SDL_RenderPresent = (decltype(SDL_RenderPresent)*)GetProcAddress(sdl, "SDL_RenderPresent");
    _SDL_RenderDrawPoint = (decltype(SDL_RenderDrawPoint)*)GetProcAddress(sdl, "SDL_RenderDrawPoint");
    _SDL_RenderSetScale = (decltype(SDL_RenderSetScale)*)GetProcAddress(sdl, "SDL_RenderSetScale");
    _SDL_PollEvent = (decltype(SDL_PollEvent)*)GetProcAddress(sdl, "SDL_PollEvent");
    _SDL_GetTicks = (decltype(SDL_GetTicks)*)GetProcAddress(sdl, "SDL_GetTicks");
    _SDL_Delay = (decltype(SDL_Delay)*)GetProcAddress(sdl, "SDL_Delay");

    std::thread([]()
    {
        _SDL_Init(SDL_INIT_VIDEO);

        window = _SDL_CreateWindow(
            "llvm8",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            64 * SCALE,
            32 * SCALE,
            SDL_WINDOW_OPENGL
        );
        renderer = _SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        
        _SDL_RenderSetScale(renderer, SCALE, SCALE);

        while (true)
        {
            SDL_Event event;
            while (_SDL_PollEvent(&event)) { }
        }
    }).detach();
}

extern "C" void start_delay_timer(char& dt)
{
    std::thread([&dt]()
    { 
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (dt > 0) dt--;
        }
    }).detach();
}

/*
 * race condition:
 * first draw call may be called before renderer is initialized?
 * or maybe its refreshing too fast? SDL_Delay?
 */

extern "C" void draw(char* screen)
{
    while (!renderer || !window) {}

    for (int i = 0; i < 64 * 32; ++i)
    {
        int x = i % 64;
        int y = (i / 64) % 32;
        int color = screen[i] * 255;
        _SDL_SetRenderDrawColor(renderer, color, color, color, 255);
        _SDL_RenderDrawPoint(renderer, x, y);

        // todo: there seems to be a bug:
        // fishie rom without printfs will show empty screen
        // render thread doesnt catch up with the drw calls?

        /*if (i % 64 == 0) printf("\n");
        
        if (screen[i] == 1) printf("x");
        else printf(" ");*/
    }

    _SDL_RenderPresent(renderer);

    // wait for 60fps / vsync
    //_SDL_Delay(1000);
}