// Copyright (c) 2011 Sirikata Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include <sirikata/core/util/Platform.hpp>
#include <sirikata/sdl/SDL.hpp>
#include <SDL.h>

namespace Sirikata {
namespace SDL {

namespace {

uint32 SDLSubsystem(Subsystem::Type sub) {
    switch (sub) {
      case Subsystem::Timer: return SDL_INIT_TIMER; break;
      case Subsystem::Audio: return SDL_INIT_AUDIO; break;
      case Subsystem::Video: return SDL_INIT_VIDEO; break;
      case Subsystem::CD: return SDL_INIT_CDROM; break;
      case Subsystem::Joystick: return SDL_INIT_JOYSTICK; break;
      default: return 0xFFFFFFFF; break;
    }
}

// We use an instance of this struct to make sure we setup and cleanup
// everything properly when this library is unloaded, as well as to track some
// state about requests. We track all of this in the same struct to ensure
// initialization/destruction order doesn't matter.
struct SafeInitCleanup {
    SafeInitCleanup()
     : initialized(false)
    {
        memset(subsystem_refcounts, 0, Subsystem::NumSubsystems);
    }

    ~SafeInitCleanup() {
        if (initialized) {
            // We want to do
            //  SDL_Quit();
            // but we can't currently because forked processes (Berkelium) get
            // hung up trying to cleanup, I guess because something is
            // mismatched when you fork too late (after allocating graphics
            // resources).  This isn't really that horrible as SDL is usually
            // tied mostly to the lifetime of the entire process anyway.
            initialized = false;
        }
    }

    int initialize(Subsystem::Type sub) {
        // Trying to catch a crash: http://crashes.sirikata.com/status/2097,
        // issue #463.
        // Intentionally not using assert since we want to track this in binary
        // releases.
        if (this == NULL) {
            SILOG(sdl, fatal, "Got NULL this pointer in SDL::SafeInitCleanup::initialize");
            // This should trigger a crash, which would just happen later anyway.
            this->initialized = false;
        }

        assert(sub < Subsystem::NumSubsystems);
        if (subsystem_refcounts[sub] == 0) {
            if (!initialized) {
                if (SDL_Init(SDL_INIT_NOPARACHUTE) < 0)
                    return -1;
                initialized = true;
            }
            int retval = SDL_InitSubSystem(SDLSubsystem(sub));
            // Only increase refcount upon successful initialization
            if (retval == 0)
                subsystem_refcounts[sub]++;
            return retval;
        }
        else {
            // Already initialized, always successful
            subsystem_refcounts[sub]++;
            return 0;
        }
    }

    void quit(Subsystem::Type sub) {
        assert(sub < Subsystem::NumSubsystems);
        assert(subsystem_refcounts[sub] > 0);

        subsystem_refcounts[sub]--;
        if (subsystem_refcounts[sub] == 0)
            SDL_QuitSubSystem(SDLSubsystem(sub));
    }

    // Whether SDL is currently initialized
    bool initialized;
    // Refcounts on each subsystem
    int32 subsystem_refcounts[Subsystem::NumSubsystems];
};
SafeInitCleanup gSafeInitCleanup;

} // namespace

int SIRIKATA_SDL_FUNCTION_EXPORT InitializeSubsystem(Subsystem::Type sub) {
    return gSafeInitCleanup.initialize(sub);
}

void SIRIKATA_SDL_FUNCTION_EXPORT QuitSubsystem(Subsystem::Type sub) {
    gSafeInitCleanup.quit(sub);
}

} // namespace SDL
} // namespace Sirikata
