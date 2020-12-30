#include <stdio.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#include <thread>

#include "SDL2/SDL.h"

#define SCALE 16
#define NOGUI

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

template<typename T>
T* get_addr(void* module, const char* proc)
{
    #ifdef _WIN32
    return (T*)GetProcAddress(module, proc);
    #else
    return (T*)dlsym(module, proc);
    #endif
}

extern "C" void init()
{
    #ifdef _WIN32
    auto sdl = LoadLibraryA("SDL2.dll");
    #else
    auto sdl = dlopen("libSDL2.dylib", RTLD_GLOBAL);
    #endif
    
    _SDL_Init = get_addr<decltype(SDL_Init)>(sdl, "SDL_Init");
    _SDL_CreateWindow = get_addr<decltype(SDL_CreateWindow)>(sdl, "SDL_CreateWindow");
    _SDL_CreateRenderer = get_addr<decltype(SDL_CreateRenderer)>(sdl, "SDL_CreateRenderer");
    _SDL_SetRenderDrawColor = get_addr<decltype(SDL_SetRenderDrawColor)>(sdl, "SDL_SetRenderDrawColor");
    _SDL_RenderClear = get_addr<decltype(SDL_RenderClear)>(sdl, "SDL_RenderClear");
    _SDL_RenderPresent = get_addr<decltype(SDL_RenderPresent)>(sdl, "SDL_RenderPresent");
    _SDL_RenderDrawPoint = get_addr<decltype(SDL_RenderDrawPoint)>(sdl, "SDL_RenderDrawPoint");
    _SDL_RenderSetScale = get_addr<decltype(SDL_RenderSetScale)>(sdl, "SDL_RenderSetScale");
    _SDL_PollEvent = get_addr<decltype(SDL_PollEvent)>(sdl, "SDL_PollEvent");
    _SDL_GetTicks = get_addr<decltype(SDL_GetTicks)>(sdl, "SDL_GetTicks");
    _SDL_Delay = get_addr<decltype(SDL_Delay)>(sdl, "SDL_Delay");

    #ifndef NOGUI
    // thread crashes on macos
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
    #endif
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
    #ifndef NOGUI
    while (!renderer || !window) {}
    #else
    #if defined _WIN32
    system("cls");
    #elif defined (__LINUX__) || defined(__gnu_linux__) || defined(__linux__)
    system("clear");
    #elif defined (__APPLE__)
    system("clear");
    #endif
    #endif 

    for (int i = 0; i < 64 * 32; ++i)
    {
        int x = i % 64;
        int y = (i / 64) % 32;
        int color = screen[i] * 255;

        #ifndef NOGUI
        _SDL_SetRenderDrawColor(renderer, color, color, color, 255);
        _SDL_RenderDrawPoint(renderer, x, y);
        #else
        // todo: there seems to be a bug:
        // fishie rom without printfs will show empty screen
        // render thread doesnt catch up with the drw calls?

        if (i % 64 == 0) printf("\n");
        
        if (screen[i] == 1) printf("x");
        else printf(" ");
        #endif  
    }

    #ifndef NOGUI
    _SDL_RenderPresent(renderer);
    #endif

    // wait for 60fps / vsync
    //_SDL_Delay(1000);
}