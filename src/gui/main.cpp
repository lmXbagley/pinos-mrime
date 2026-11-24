// gui/main.cpp — точка входа настольного CAD MINOSPRIME (SDL2 + OpenGL 2.1).
#include "app.hpp"
#include "draw.hpp"
#include "panels.hpp"
#include "scene.hpp"
#include "widgets.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <cstdio>
#include <string>

static bool setFullscreen(SDL_Window* win, bool on) {
  if (!on) {
    SDL_SetWindowFullscreen(win, 0);
    SDL_SetWindowBordered(win, SDL_TRUE);
    return (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) == 0;
  }
  SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
  bool ok = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
  if (!ok) SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN);
  ok = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
  if (!ok) {
    SDL_DisplayMode dm;
    int di = SDL_GetWindowDisplayIndex(win);
    if (di >= 0 && SDL_GetDesktopDisplayMode(di, &dm) == 0) {
      SDL_SetWindowBordered(win, SDL_FALSE);
      SDL_SetWindowPosition(win, 0, 0);
      SDL_SetWindowSize(win, dm.w, dm.h);
    }
  }
  return ok;
}

int main(int argc, char** argv) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_Window* win = SDL_CreateWindow("MINOSPRIME — CAD органоидов",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     1600, 900,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN |
                                         SDL_WINDOW_ALLOW_HIGHDPI);
  bool startFs = false;
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--fullscreen" || std::string(argv[i]) == "-f") startFs = true;
  if (!win) {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_GLContext gl = SDL_GL_CreateContext(win);
  if (!gl) {
    fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }
  SDL_GL_SetSwapInterval(1);
  SDL_StartTextInput();

  mpui::initDraw();

  mpui::App app;
  const char* fileArg = nullptr;
  for (int i = 1; i < argc; ++i)
    if (argv[i][0] != '-') { fileArg = argv[i]; break; }
  app.startup(fileArg);

  if (startFs) app.wantFs = true;



  Uint64 perfFreq = SDL_GetPerformanceFrequency();
  Uint64 lastT = SDL_GetPerformanceCounter();

  while (!app.quit) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      switch (ev.type) {
        case SDL_QUIT:
          app.quit = true;
          break;
        case SDL_MOUSEMOTION:
          app.ui.mvx += (float)(ev.motion.xrel);
          app.ui.mvy += (float)(ev.motion.yrel);
          app.ui.mx = (float)ev.motion.x;
          app.ui.my = (float)ev.motion.y;
          break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
          if (ev.button.button == SDL_BUTTON_LEFT) {
            app.ui.lDown = ev.type == SDL_MOUSEBUTTONDOWN;
            if (ev.type == SDL_MOUSEBUTTONDOWN) {
              app.ui.lPressed = true;
              app.ui.mx = ev.button.x;
              app.ui.my = ev.button.y;
            } else {
              app.ui.lReleased = true;
            }
          } else if (ev.button.button == SDL_BUTTON_RIGHT) {
            app.ui.rPressed = ev.type == SDL_MOUSEBUTTONDOWN;
            app.ui.rReleased = ev.type == SDL_MOUSEBUTTONUP;
            if (ev.type == SDL_MOUSEBUTTONDOWN) {
              app.ui.mx = ev.button.x;
              app.ui.my = ev.button.y;
            }
          }
          break;
        case SDL_MOUSEWHEEL:
          app.ui.wheel += ev.wheel.y;
          break;
        case SDL_KEYDOWN: {
          if (ev.key.repeat || ev.key.keysym.sym == SDLK_UNKNOWN) break;
          if (ev.key.keysym.sym == SDLK_F11) {
            bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
            if (setFullscreen(win, !fs))
              app.statusMsg = !fs ? "фуллскрин: вкл (F11/FS)" : "фуллскрин: выкл";
            else
              fprintf(stderr, "SDL_SetWindowFullscreen: %s\n", SDL_GetError());
            break;
          }
          app.ui.keys.push_back(SDL_GetKeyName(ev.key.keysym.sym));
          break;
        }
        case SDL_TEXTINPUT:
          app.ui.entered += ev.text.text;
          break;
        default:
          break;
      }
    }

    int dw = 1, dh = 1, w = 1, h = 1;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    SDL_GetWindowSize(win, &w, &h);

    Uint64 nowT = SDL_GetPerformanceCounter();
    double dt = (double)(nowT - lastT) / (double)perfFreq;
    if (dt < 0.0001) dt = 0.0001;
    if (dt > 0.1) dt = 0.1;
    lastT = nowT;

    SDL_Keymod mod = SDL_GetModState();
    app.ui.ctrl = (mod & KMOD_CTRL) != 0;
    app.ui.shift = (mod & KMOD_SHIFT) != 0;
    app.ui.alt = (mod & KMOD_ALT) != 0;
    app.ui.beginFrame(w, h);

    app.update(dt);

    if (app.wantFs) {
      bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
      if (setFullscreen(win, !fs))
        app.statusMsg = !fs ? "фуллскрин: вкл (F11/FS)" : "фуллскрин: выкл";
      else
        fprintf(stderr, "SDL_SetWindowFullscreen: %s\n", SDL_GetError());
      app.wantFs = false;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glViewport(0, 0, dw, dh);

    // 3D-вьюпорт (6.x)
    mpui::Mat4 P = app.cam.proj(w, h);
    mpui::Mat4 V = app.cam.view();
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(P.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(V.m);
    mpui::sceneDraw3D(app);

    // 2D-слой (HUD/панели/оверлеи)
    mpui::begin2D(w, h);
    mpui::sceneOverlay2D(app);
    app.drawUI();
    app.ui.endFrame();
    mpui::end2D();

    SDL_GL_SwapWindow(win);
  }

  if (app.api) {
    std::string err;
    (void)err;
    app.api->stop();
    app.api.reset();
  }
  SDL_StopTextInput();
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
