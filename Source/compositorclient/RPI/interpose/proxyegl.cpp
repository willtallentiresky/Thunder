/*
    First attempt. Possibly wrong and / or incomplete, and may contain 'bad' code and / or coding practice.

    Interpostion library libEGL.so. Assume lazy symbol resolution by default.
    Link against the real EGL library to get the ELF 'NEEDED' recorded. Assume that library is named libRealEGL.so.
    Possibly, 'tell' the linker to ignore unresolved symbols.

    RDK licence and copyright (may) apply.
*/

#include "common.h"

#include <string>

// A little less code bloat
#define _2CSTR(str) std::string (str).c_str ()

#define PROXYEGL_PRIVATE COMMON_PRIVATE
#define PROXYEGL_PUBLIC COMMON_PUBLIC

#include <limits>
#include <vector>
#include <cstring>
#include <unordered_map>

#ifdef __cplusplus
extern "C" {
#endif

// This order matters!
#include <gbm.h>
#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef EGL_PLATFORM_GBM_MESA
#define EGL_PLATFORM_GBM_KHR
#endif

#include <signal.h>

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif
#include <setjmp.h>
#include <sys/select.h>
#undef _POSIX_SOURCE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#undef _GNU_SOURCE

#ifdef _MESADEBUG
#include <unistd.h>
#include <sys/syscall.h>
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PROXYEGL_PRIVATE COMMON_PRIVATE
#define PROXYEGL_PUBLIC COMMON_PUBLIC

// TODO: match prototypes etc properly, eg, EGLAPI, EGLAPIENTRY

// EGL 1.4 / 1.5 extension support
PROXYEGL_PUBLIC __eglMustCastToProperFunctionPointerType eglGetProcAddress (const char*);
PROXYEGL_PUBLIC EGLDisplay eglGetPlatformDisplayEXT (EGLenum, void*, const EGLAttrib*);
PROXYEGL_PUBLIC EGLSurface eglCreatePlatformWindowSurfaceEXT (EGLDisplay, EGLConfig, void*, const EGLAttrib*);

// EGL 1.4 support
PROXYEGL_PUBLIC EGLDisplay eglGetDisplay (EGLNativeDisplayType);
PROXYEGL_PUBLIC EGLBoolean eglTerminate (EGLDisplay);

PROXYEGL_PUBLIC EGLBoolean eglChooseConfig (EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);

PROXYEGL_PUBLIC EGLSurface eglCreateWindowSurface (EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
PROXYEGL_PUBLIC EGLBoolean eglDestroySurface (EGLDisplay, EGLSurface);

PROXYEGL_PUBLIC EGLBoolean eglSwapBuffers (EGLDisplay, EGLSurface);

#ifdef _MESADEBUG
PROXYEGL_PUBLIC EGLContext eglCreateContext (EGLDisplay, EGLConfig, EGLContext, const EGLint*);
PROXYEGL_PUBLIC EGLBoolean eglDestroyContext (EGLDisplay, EGLContext);

PROXYEGL_PUBLIC EGLSurface eglCreatePbufferSurface (EGLDisplay, EGLConfig, const EGLint*);

PROXYEGL_PUBLIC EGLBoolean eglMakeCurrent (EGLDisplay, EGLSurface, EGLSurface, EGLContext);
#endif

// EGL 1.5 support
PROXYEGL_PUBLIC EGLDisplay eglGetPlatformDisplay (EGLenum, void*, const EGLAttrib*);
PROXYEGL_PUBLIC EGLSurface eglCreatePlatformWindowSurface (EGLDisplay, EGLConfig, void*, const EGLAttrib*);

#ifdef __cplusplus
}
#endif

namespace {
// Suppress compiler preprocessor 'visibiity ignored' in anonymous namespace
#define _PROXYEGL_PRIVATE /*PROXYEGL_PRIVATE*/
#define _PROXYEGL_PUBLIC PROXYEGL_PUBLIC

class Platform : public Singleton <Platform> {

    // This friend has access to all memebers!
    friend Singleton <Platform>;

    public :

        _PROXYEGL_PRIVATE bool isGBMdevice (const EGLNativeDisplayType& display) const;
        _PROXYEGL_PRIVATE bool isGBMsurface (const EGLNativeWindowType& window) const;

        _PROXYEGL_PRIVATE void FilterConfigs (const EGLDisplay& display, std::vector <EGLConfig>& configs) const;

        _PROXYEGL_PRIVATE bool Add (const EGLDisplay&, const EGLNativeDisplayType&);
        _PROXYEGL_PRIVATE bool Add (const EGLSurface&, const EGLNativeWindowType&);

        // EGLDisplay is ignored if all equals true
        // EGLDisplay handles remain valid after their creation (until the application ends)
//        _PROXYEGL_PRIVATE bool Remove (const EGLDisplay&, bool all = false);
        // EGLSurface is ignored if all equals true
        _PROXYEGL_PRIVATE bool Remove (const EGLSurface&, bool all = false);

        _PROXYEGL_PRIVATE bool ScanOut (const EGLSurface&) const;

        _PROXYEGL_PRIVATE void Lock (void) const;
        _PROXYEGL_PRIVATE void Unlock (void) const;

        // Expected runtime dependencies and their names
        // Also see helpers
        _PROXYEGL_PRIVATE static constexpr const char* libGBMname() {
            return "libgbm.so";
        }

        _PROXYEGL_PRIVATE static constexpr const char* libDRMname () {
            return "libdrm.so";
        }

    protected :

        //  Nothing

    private :

        using drm_callback_data_t = struct { int fd; uint32_t fb; struct gbm_bo* bo; bool waiting; };

        enum class JMP_STATUS : sig_atomic_t {UNDEFINED, SETUP, PROCESSED};

        mutable volatile JMP_STATUS _safe;
        mutable sigjmp_buf _stack_env;

        pthread_once_t _jmp_mutex_initialized;
        mutable pthread_mutex_t _jmp_mutex;

#ifdef _HASH
        // Let std::hash <const T> be (well-)defined
        static_assert (std::is_pointer <EGLDisplay>::value != false && sizeof (EGLDisplay) <= sizeof (void*));
        static_assert (std::is_pointer <EGLSurface>::value != false && sizeof (EGLSurface) <= sizeof (void*));

        std::unordered_map <const void* /*EGLDisplay*/, const EGLNativeDisplayType> _map_dpy;
        std::unordered_map <const void* /*EGLSurface*/, const EGLNativeWindowType> _map_surf;
#else
        std::unordered_map <EGLDisplay, EGLNativeDisplayType> _map_dpy;
        std::unordered_map <EGLSurface, EGLNativeWindowType> _map_surf;
#endif

        Platform () : _safe (Platform::JMP_STATUS::UNDEFINED), _jmp_mutex_initialized (PTHREAD_ONCE_INIT) {
            /*void*/ _map_dpy.clear ();
            /*void*/ _map_surf.clear ();
        }

        virtual ~Platform () = default;

        template <typename Func>
        _PROXYEGL_PRIVATE bool hasGBMproperty(Func func) const;

        _PROXYEGL_PRIVATE struct gbm_bo* ScanOut (const struct gbm_bo* bo, uint8_t buffers = 2) const;

        // Seconds
        _PROXYEGL_PRIVATE static constexpr time_t FrameDuration () {
            return 1;
        }

//        _PROXYEGL_PRIVATE void Lock (void) const;
//        _PROXYEGL_PRIVATE void Unlock (void) const;

        _PROXYEGL_PRIVATE bool InitLock (void);

        _PROXYEGL_PRIVATE static constexpr uint32_t NonEmptySizeInitialValue (void) {
            return 1;
        }

        // Helpers, make the GBM API well-defined within this unit
        // All these are ill-defined for EGL_DEFAULT_DISPLAY
        _PROXYEGL_PRIVATE struct gbm_bo* gbm_surface_lock_front_buffer (struct gbm_surface* surface) const;
        _PROXYEGL_PRIVATE void gbm_surface_release_buffer (struct gbm_surface* surface, struct gbm_bo* bo) const;
        _PROXYEGL_PRIVATE int gbm_surface_has_free_buffers (struct gbm_surface* surface) const;
};

// TODO: safeguard simultaneous access / recursive access
template <typename Func>
bool Platform::hasGBMproperty (Func func) const {
    bool ret = false;

    struct sigaction _action[2]; //  new action, old action
    sigset_t _mask[2]; // new mask, old mask

    constexpr uint8_t NEW = 0;
    constexpr uint8_t OLD = 1;

    if (sigemptyset (&_mask[NEW]) != 0 || sigaddset (&_mask[NEW], SIGSEGV) != 0) {
        // Error
    }
    else {
        // Undefined behavior if SIGFPE, SIGILL, SIGSEGV, or SIGBUS are blocked and not generated by kill, pthreadd_kill, raise or sigqueue on the same thread; generated by other processes is ok
        // At least unblock any inadvertently blocked SIGSEGV
        if (pthread_sigmask (SIG_UNBLOCK /* ignored */,&_mask[NEW], &_mask[OLD]) == 0) {

            // Strictly speaking c++ linkage and not C linkage
            // Asynchronous, but never called more than once (although pending SIGSEGV are possible, but they are usually redirected to one of the avaiable threads), still in scope until the old handler is restored
            auto handler = +[] (int signo) /*-> void */ {
                switch (Platform::Instance ()._safe) {
                    case Platform::JMP_STATUS::UNDEFINED :
                        {
                            // Handler triggered before the stack enviroment was set up

                            if (signo == SIGSEGV) {
                                // Forward to the default handler
                                struct sigaction action;

                                /*void**/ memset (&action, 0, sizeof (action));

                                action.sa_flags = 0;
                                action.sa_handler = SIG_DFL;

                                // No longer block the SIGSEGV 
                                if (sigemptyset (&(action.sa_mask)) != 0) {
//                                    // Error
                                }

                                if (sigaction (SIGSEGV, &action, nullptr) != 0) {
                                    // Error
                                }

                                // Invoke the signal
                                LOG (_2CSTR ("Invoking the default SIGSEGV handler"));
                                if (pthread_kill (pthread_self (), SIGSEGV) != 0) {
//                                    // Error
                                }
                            }
                            break;
                        }
                    case Platform::JMP_STATUS::SETUP :
                        {
                            Platform::Instance ()._safe = Platform::JMP_STATUS::PROCESSED;

                            siglongjmp (Platform::Instance ()._stack_env , 1 /* anything not 0 */);

                            break;
                        }
                    case Platform::JMP_STATUS::PROCESSED :
                    default                              :
                        {  // Error, this should not happen
                            LOG (_2CSTR ("Unexpected state in the SIGSEGV handler"));
                        }
                }
            };

            /*void**/ memset (&_action[NEW], 0, sizeof (_action[NEW]));
            _action[NEW].sa_flags = 0;
//            _action[NEW].sa_handler = &gbm_handler;
            _action[NEW].sa_handler = handler;
            // Only if SA_NODEFER is set
//            _action[NEW].sa_mask = _mask[NEW];

            // It's time to block any of the caller threads before the first has completed the test

            // This makes it well defined
            static pthread_once_t _initialized = PTHREAD_ONCE_INIT;
            static pthread_mutex_t _mutex;

            // Strictly speaking c++ linkage and not C linkage
            auto init_once = +[] ()/* -> void*/ {
                pthread_mutexattr_t _attr;

                // Set the defaults used by the implementation
                if (pthread_mutexattr_init (&_attr) != 0 || \
                    pthread_mutexattr_settype (&_attr, PTHREAD_MUTEX_ERRORCHECK /*PTHREAD_MUTEX_NORMAL*/) != 0 || \
                    pthread_mutex_init (&_mutex, &_attr)) {

                    // Error
                    assert (false);
                }
            };

            // Captureless (positive, eg add '+' in front) lambda's can be cast to regular function pointers
            // void (*init_routine)(void);
            // All threads block until pthread_once returns
            if (pthread_once (&_initialized, init_once) != 0) {
                LOG (_2CSTR ("Unable to initialize required mutex"));
            }

            // Initialize mutex once and (re-)install new handler
            if (pthread_mutex_lock (&_mutex) != 0 || sigaction (SIGSEGV, &_action[NEW], &_action[OLD]) != 0) {
                // Error, we cannot probe
            }
            else {
                if (sigsetjmp (_stack_env, 1 /* save signal mask */) != 0) {
                    // Returned from siglongjmp in signal handler and rewinded the stack etc
                    // Return value equals value set in siglongjmp
                    LOG (_2CSTR ("Returned from the signal handler"));
                }
                else {
                    // Prepare to probe
                    // True return of sigsetjmp equals 0

#ifndef _TEST
                    // Handler and stack environment are set up
                    _safe = JMP_STATUS::SETUP;

                    // Probe gbm property
                    func();
#else
                    // Deliberately cause a segfault
                    LOG (_2CSTR ("Deliberately invoking SIGSEGV"));
                    if (pthread_kill (pthread_self (), SIGSEGV) != 0) {
                    }
#endif
                }

                // Restore by at least disabling the 'local' signal handler
                if (sigaction (SIGSEGV, &_action[OLD], nullptr) != 0) {
                    // Error
                }
            }

            // Wait until none of the pending signals is a SIGSEGV
            sigset_t pending;

            int value = 0;
            do {
                if (value == -1 || sigpending (&pending) != 0) {
                    // Unable to verify
                    int err = errno;
                    LOG (_2CSTR ("Unable to determine pending SIGSEGV signals with "), err);
                    break;
                }
                else {
                    value = sigismember (&pending, SIGSEGV);
                }
            } while (value == 1 || value == -1); // SIGSEGV pending or error

            // Assume display is a struct gbmdevice* If the handler was not invoked
             ret = !(_safe == Platform::JMP_STATUS::PROCESSED);

            _safe = Platform::JMP_STATUS::UNDEFINED;

#ifdef _TEST
            // Deliberately cause a segfault
            LOG (_2CSTR ("Deliberately invoking SIGSEGV"));
            if (pthread_kill (pthread_self (), SIGSEGV) != 0) {
                // Error
            }
#endif

            if (pthread_mutex_unlock (&_mutex) != 0) {
                assert (false);
            }
        }
    }

    return ret;
}

// Hack based on gbmint.h
bool Platform::isGBMdevice (const EGLNativeDisplayType& display) const {
    // Probe gbm device
    auto func = [&display] () -> bool {
        bool ret = false;

        using gbm_device_t = struct { struct gbm_device* (*dummy) (int); };

        // Expect some slicing
        const gbm_device_t* _device = reinterpret_cast <const gbm_device_t*> (display);

        if (_device != nullptr && _device->dummy != nullptr) {
            // Some symbols may be undefined if libgbm is not explicitly loaded
            ret = loaded (libGBMname ()) != false && _device->dummy == &gbm_create_device;
// TODO: use dladdr to test if both are within the same library and the nearest symbols equal
        }

        return ret;
    };

    bool ret = false;

    // The true type is fixed by eglplatform and a platform flag, eg, __GBM__
    if (display != EGL_DEFAULT_DISPLAY) {
        // Probe gbm device
        ret = hasGBMproperty (func);
    }

    LOG (_2CSTR ("Display is "), ret != false ? _2CSTR ("") : _2CSTR ("NOT "), _2CSTR ("a GBM device"));

    return ret;
}

// Hack based on gbmint.h
bool Platform::isGBMsurface (const EGLNativeWindowType& window) const {
    // Probe gbm surface
    auto func = [&window] () -> bool {
        bool ret = false;

        using gbm_surface_t = struct { struct gbm_device* gbm; };

        // Expect some slicing
        const gbm_surface_t* _surface = reinterpret_cast <const gbm_surface_t*> (window);

        if (_surface != nullptr && _surface->gbm != nullptr) {
            using gbm_device_t = struct { struct gbm_device* (*dummy) (int); };

            const gbm_device_t* _device = reinterpret_cast <const gbm_device_t*> (_surface->gbm);

            if (_device != nullptr && _device->dummy != nullptr) {
                // Some symbols may undefined if libgbm is not explicitly loaded
                // RTLD_NOLOAD does not exist in POSIX
                ret = loaded (libGBMname ()) != false && _device->dummy == &gbm_create_device;
// TODO: use dladdr to test if both are within the same library and the nearest symbols equal
            }
        }

        return ret;
    };

    bool ret = false;

    // Probe gbm surface
    ret = hasGBMproperty (func);

    LOG (_2CSTR ("Surface is "), ret != false ? _2CSTR ("") : _2CSTR ("NOT "), _2CSTR ("a GBM surface"));

    return ret;
}

void Platform::FilterConfigs (const EGLDisplay& display, std::vector <EGLConfig>& configs) const {
    Lock ();

    auto _it_dpy = _map_dpy.find (display);

    // Filter only for GBM displays being tracked
    if (_it_dpy != _map_dpy.end () && configs.empty() != true) {
        auto it = configs.begin();

        while (it != configs.end()) {
            EGLint value = 0;

            if (eglGetConfigAttrib (display, *it, EGL_NATIVE_VISUAL_ID, &value) != EGL_FALSE) {
                // Both formats should be considered equivalent / interchangeable
                if (value != DRM_FORMAT_ARGB8888 && value != DRM_FORMAT_XRGB8888) {
                    it = configs.erase (it);
                    continue;
                }
            }

            it++;
        }
    }

    Unlock ();
}

bool Platform::Add (const EGLDisplay& egl, const EGLNativeDisplayType& native) {
    Lock ();

    bool ret = false;

    //  An EGL display remains valid until an application ends, here until the library unloads

    auto _it_dpy = _map_dpy.find (egl);

    if (_it_dpy != _map_dpy.end ()) {
        // EGLDisplay is constructed using a previous EGLNativeDisplayType
        ret = _it_dpy->second == native;
    }
    else {
#ifdef _HASH
        auto result = _map_dpy.insert (std::pair <EGLDisplay, EGLNativeDisplayType> (egl, native));
#else
        auto result = _map_dpy.insert (std::pair <EGLDisplay, EGLNativeDisplayType> ( const_cast <EGLDisplay> (egl), const_cast <EGLNativeDisplayType> (native)));
#endif

        // On failure, eg, key exists, false is returned
        ret = result.second;
    }

    assert (ret != false);

    if (ret != true) {
        LOG (_2CSTR ("Unable to add EGLDisplay "), egl, _2CSTR (" and native display "), native, _2CSTR (" to the associative map"));
    }

    Unlock ();

    return ret;
}

bool Platform::Add (const EGLSurface& egl, const EGLNativeWindowType& native) {
    Lock ();

    auto result = _map_surf.insert (std::pair <EGLSurface, EGLNativeWindowType> (egl, native));

    // On failure, eg, key exists, false is returned
    bool ret = result.second;

    assert (ret != false);

    if (ret != true) {
        LOG (_2CSTR ("Unable to add EGLSurface "), egl, _2CSTR (" and native window "), native, _2CSTR (" to the associative map"));
    }

    Unlock ();

    return ret;
}

// EGLSurface is ignored if all equals true
bool Platform::Remove (const EGLSurface& egl, bool all) {
    Lock ();

    bool ret = false;

    if (all != true) {
        ret = _map_surf.erase (egl) ==  1;

        // iterator has become singular
    }
    else {
        /*void*/ _map_surf.clear ();

        // iterator has become singular

        ret = _map_surf.size () < NonEmptySizeInitialValue ();
    }

    assert (ret != false);

    if (ret != true) {
        LOG (_2CSTR ("Unable to remove "), all != false ? _2CSTR ("all EGLSurfaces ") : _2CSTR ("EGLSurface "), all != false ? _2CSTR ("") : egl, _2CSTR ("from the associatve map"));
    }

    Unlock ();

    return ret;
}

bool Platform::ScanOut (const EGLSurface& surface) const {
    Lock ();

    bool ret = false;

    auto it = _map_surf.find (surface);

    if (it != _map_surf.end ()) {
        // Check here and not in every helper
        EGLDisplay _dpy = eglGetCurrentDisplay ();

        auto _it_dpy = _map_dpy.find (_dpy);

        EGLNativeDisplayType _native = EGL_DEFAULT_DISPLAY;

        if (_it_dpy != _map_dpy.end ()) {
            _native = _it_dpy->second;
        }

        if (_native != EGL_DEFAULT_DISPLAY && _dpy != EGL_NO_DISPLAY) {
            struct gbm_surface* _gbm_surf = reinterpret_cast <struct gbm_surface*> (it->second);

            struct gbm_bo* _gbm_bo = nullptr;

            if (_it_dpy != _map_dpy.end ()) {
                _native = _it_dpy->second;
            }

            if (_gbm_surf != nullptr) {
                _gbm_bo = gbm_surface_lock_front_buffer (_gbm_surf);
            }

            if (_gbm_bo != nullptr) {
                // Signal that a lock has been taken
                ret = true;
                EGLint _value;

                if (eglQuerySurface (_dpy, surface, EGL_RENDER_BUFFER, &_value) != EGL_FALSE) {
// TODO: validate surface belongs to device
                    struct gbm_bo* _available = ScanOut (_gbm_bo, _value != EGL_BACK_BUFFER ? 1 : 2);

                    if (_available != nullptr) {
                        // Probably not required after removing the FB, but just be safe
                        /*void*/ gbm_surface_release_buffer (_gbm_surf, _available);
                    }
                }
                else {
                    LOG (_2CSTR ("Unable to complete scan out"));
                }
            }

            if (gbm_surface_has_free_buffers (_gbm_surf) <= 0) {
                LOG (_2CSTR ("Insufficient free buffers left"));
            }
        }
    }
    else {
        LOG (_2CSTR ("Untracked EGLSurface"));
        assert (false);
    }

    Unlock ();

    return ret;
}

struct gbm_bo* Platform::ScanOut (const struct gbm_bo* bo, uint8_t buffers) const {
    // Avoid many additional const_cast's for the GBM API
    struct gbm_bo* _bo = const_cast <struct gbm_bo*> (bo);

    // Determine current CRTC; currently only considers just a single crtc-encoder-connector path
    auto func = [] (uint32_t fd, uint32_t& crtc, uint32_t& connectors) -> uint32_t {
        uint32_t ret = 0;

        if (ret == 0) {
            drmModeResPtr _res = drmModeGetResources (fd);

            if (_res != nullptr) {

                for (int i = 0; i < _res->count_connectors; i++) {
                    // Do not probe
                    drmModeConnectorPtr _con = drmModeGetConnectorCurrent (fd, _res->connectors[i]);

                    // Only consider HDMI
                    if (_con != nullptr) {
                        if ( (_con->connector_type == DRM_MODE_CONNECTOR_HDMIA  || \
                              _con->connector_type == DRM_MODE_CONNECTOR_HDMIB) && \
                              DRM_MODE_CONNECTED == _con->connection) {

                            // Encoder currently connected to
                            drmModeEncoderPtr _enc = drmModeGetEncoder (fd, _con->encoder_id);

                            if (_enc != nullptr) {
                                crtc = _enc->crtc_id;

                                connectors = _con->connector_id;

                                ret++;

                                drmModeFreeEncoder (_enc);
                            }
                        }

                        drmModeFreeConnector (_con);
                    }

                    if (ret > 0) {
                        // Probably never exceeds 1
                        break;
                    }
                }

                drmModeFreeResources (_res);
            }
        }

        return ret;
    };

    struct gbm_bo* ret = nullptr;

    struct gbm_device* _gbm_device = gbm_bo_get_device (_bo);

    // This can be an expensive test, thus cache the result
    static bool _loaded = loaded (libGBMname ()) && loaded (libDRMname ());

    if (_gbm_device != nullptr && _loaded != false) {

        int _fd = gbm_device_get_fd (_gbm_device);

        if (_fd >= 0 && drmAvailable () != 0 && drmIsMaster (_fd) != 0) {
            uint32_t _format = gbm_bo_get_format (_bo);
            uint32_t _bpp = gbm_bo_get_bpp (_bo);
            uint32_t _stride = gbm_bo_get_stride (_bo);
            uint32_t _height = gbm_bo_get_height (_bo);
            uint32_t _width = gbm_bo_get_width (_bo);
            uint32_t _handle = gbm_bo_get_handle (_bo).u32;

            if (_bpp == 32 && gbm_device_is_format_supported (_gbm_device, _format, GBM_BO_USE_SCANOUT) != 0) {
                // gbm_bo_format is any of 
                // GBM_BO_FORMAT_XRGB8888 : RGB in 32 bit
                // GBM_BO_FORMAT_ARGB8888 : ARGB in 32 bit
                // and should be considered something internal

// TODO: compare with BPP and ColorDepth

                // Formats can be set using GBM_BO_FORMAT_XRGB8888, GBM_BO_FORMAT_ARGB8888, but also with DRM_FORMAT_XRGB8888 and DRM_FORMAT_ARGB8888
                // The returned format is always one of the latter two, which is indicative for the native visual id. Do not support other format than available with gbm.
                assert (_format == DRM_FORMAT_XRGB8888 || _format == DRM_FORMAT_ARGB8888);

                uint32_t _fb = 0;

                // drm_fourcc.c illustrates that DRM_FORMAT_XRGB8888 has depth 24, and DRM_FORMAT_ARGB8888 has depth 32
                if (drmModeAddFB (_fd, _width, _height, _format != DRM_FORMAT_ARGB8888 ? _bpp - 8 : _bpp, _bpp, _stride, _handle, &_fb) == 0) {

                    auto enqueue = [&buffers] (int fd, int fb, struct gbm_bo* bo) -> struct gbm_bo* {
                        // Single, double or multibuffering

                        if (buffers != 2) {
                            LOG (_2CSTR ("Only double buffering is supported"));
                            assert (false);
                        }

                        static int _fb [1] = { 0 };
// TODO: 'dangerous', on a 'restart' in EGL context, eg, apllication 'never' ends
                        static struct gbm_bo* _bo [1] = { nullptr };

                        struct gbm_bo* ret = nullptr;

                        if (fd < 0 || _fb[0] == 0 || _bo[0] == nullptr || drmModeRmFB (fd, _fb [0]) != 0) {
                            // Always true for the initial frame
                            LOG (_2CSTR ("Unable to remove 'old' frame buffer"));
                        }
                        else {
                            // Effectively implement double buffering
                            ret = _bo [0];
                        }

                        // Unconditionally update
                        _fb [0] = fb;
                        _bo [0] = bo;

                        return ret;
                    };

                    // Expensive operation thus best to cache the result assuming it will not change
                    static uint32_t _crtc = 0;
                    static uint32_t _connectors = 0;
                    static uint32_t _count = func (_fd, _crtc, _connectors);

                    Platform::drm_callback_data_t _callback_data = {_fd, _fb, _bo, true};

                    int _err = drmModePageFlip (_fd, _crtc, _fb, DRM_MODE_PAGE_FLIP_EVENT, &_callback_data);

                    switch (0 - _err) {
                        case 0      :   {   // No error
                                            // Strictly speaking c++ linkage and not C linkage
                                            // Asynchronous, but never called more than once, waiting in scope
                                            auto handler = +[] (int fd, unsigned int frame, unsigned int sec, unsigned int usec, void* data) {
                                                if (data != nullptr) {
                                                    Platform::drm_callback_data_t* _data = reinterpret_cast <Platform::drm_callback_data_t*> (data);

                                                    assert (fd == _data->fd);

                                                    // Encourages the loop to break
                                                    _data->waiting = false;
                                                }
                                                else {
                                                    LOG (_2CSTR ("Invalid callback data"));
                                                }
                                            };

                                            // Use the magic constant here because the struct is versioned!
                                            drmEventContext _context = { .version = 2, . vblank_handler = nullptr, .page_flip_handler = handler };

                                            fd_set _fds;

                                            struct timespec _timeout = { .tv_sec = Platform::FrameDuration (), .tv_nsec = 0 };

                                            while (_callback_data.waiting != false) {
                                                FD_ZERO (&_fds);
                                                FD_SET( _fd, &_fds);

                                                // Race free
                                                _err  = pselect(_fd + 1, &_fds, nullptr, nullptr, &_timeout, nullptr);

                                                if (_err < 0) {
                                                    // Error; break the loop
                                                    break;
                                                }
                                                else {
                                                    if (_err == 0) {
                                                        // Timeout; retry
// TODO: add an additional condition to break the loop to limit the number of retries, but then deal with the asynchronous nature of the callback
                                                    }
                                                    else { // ret > 0
                                                        if (FD_ISSET (_fd, &_fds) != 0) {
                                                            // Node is readable
                                                            if (drmHandleEvent (_fd, &_context) != 0) {
                                                                // Error; break the loop
                                                                break;
                                                            }

                                                            // Flip probably occured already otherwise it loops again
                                                            ret = enqueue (_fd, _fb, _bo);
                                                        }
                                                    }
                                                }
                                            }

                                            break;
                                        }
                        // Many causes, but the most obvious is a busy resource or a missing drmModeSetCrtc
                        case EINVAL : {     // Probably a missing drmModeSetCrtc or an invalid _crtc
                                            drmModeCrtcPtr _ptr = drmModeGetCrtc (_fd, _crtc);

                                            if (_ptr != nullptr) {
                                                // Assume the dimensions of the buffer fit within this mode
                                                if (drmModeSetCrtc (_fd, _crtc, _fb, _ptr->x, _ptr->y, &_connectors, _count, &_ptr->mode) != 0) {
                                                    // Error
                                                    // There is nothing to be done te recover
                                                }
                                                else {
                                                    ret = enqueue (_fd, _fb, _bo);
                                                }

                                                drmModeFreeCrtc (_ptr);
                                            }

                                            break;
                                      }
                        case EBUSY  :
                        default :   {
                                        // There is nothing to be done about it
                                    }
                    }
                }
            }

// TODO : Keep track of the previous framebuffer etc to delete it later

        }
        else {
            LOG (_2CSTR ("Unable to complete the scan out due to insufficient privileges"));
        }
    }

    return ret;
}

void Platform::Lock (void) const {
    if (Instance ().InitLock () != false && pthread_mutex_lock (&_jmp_mutex) != 0) {
        assert (false);
    }
}

void Platform::Unlock (void) const {
    if (Platform::Instance ().InitLock () != false && pthread_mutex_unlock (&_jmp_mutex) != 0) {
        // Error
        assert (false);
    }
}

bool Platform::InitLock (void) {
    // Non-capturing positive lambda's can be cast to regular function pointers with the same signature
    // Strictly speaking c++ linkage and not C linkage
    auto init_once = +[] ()/* -> void*/ {
        pthread_mutexattr_t _attr;

        // Set the defaults used by the implementation
        if (pthread_mutexattr_init (&_attr) != 0 || \
            pthread_mutexattr_settype (&_attr, PTHREAD_MUTEX_ERRORCHECK /*PTHREAD_MUTEX_NORMAL*/) != 0 || \
            pthread_mutex_init (&Platform::Instance ()._jmp_mutex, &_attr) ) {

            // Error
            assert (false);
        }
    };

    bool ret = false;

    if (pthread_once (&_jmp_mutex_initialized, init_once) != 0) {
        LOG (_2CSTR ("Unable to initialize required mutex"));
        assert (false);
    }
    else {
        ret = true;
    }

    return ret;
}

// Helpers

struct gbm_bo* Platform::gbm_surface_lock_front_buffer (struct gbm_surface *surface) const {
    struct gbm_bo* ret = nullptr;

    // This can be an expensive test thus cache the result
    static bool _loaded = loaded (libGBMname ());

    // Some symbols may be undefined if libgbm is not explicitly loaded
    if (_loaded != false) {
        ret = ::gbm_surface_lock_front_buffer (surface);
    }
    else {
        // Error
        assert (false);
    }

    return ret;
}

void Platform::gbm_surface_release_buffer (struct gbm_surface* surface, struct gbm_bo* bo) const {
    // This can be an expensive test thus cache the result
    static bool _loaded = loaded (libGBMname ());

    // Some symbols may be undefined if libgbm is not explicitly loaded
    if (_loaded != false) {
        /* void */ ::gbm_surface_release_buffer (surface, bo); 
    }
    else {
        // Error
        assert (false);
    }
}

int Platform::gbm_surface_has_free_buffers (struct gbm_surface* surface) const {
    int ret = 0;

    // This can be an expensive test thus cache the result
    static bool _loaded = loaded (libGBMname ());

    // Some symbols may be undefined if libgbm is not explicitly loaded
    if (_loaded != false) {
        ret = ::gbm_surface_has_free_buffers (surface);
    }
    else {
        // Error
        assert (false);
    }

    return ret;
}

#undef _PROXYEGL_PRIVATE
#undef _PROXYEGL_PUBLIC
} // Anonymous namespace

// EGL 1.4 / 1.5 extension support
__eglMustCastToProperFunctionPointerType eglGetProcAddress (const char* procname) {
/**/     Platform::Instance ().Lock ();

//    static thread_local void* (*_eglGetProcAddress) (const char*) = nullptr;
    static void* (*_eglGetProcAddress) (const char*) = nullptr;

//    static thread_local bool resolved = false;
    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglGetProcAddress", reinterpret_cast <uintptr_t&> (_eglGetProcAddress));
    }

/**/     Platform::Instance ().Unlock ();

    __eglMustCastToProperFunctionPointerType ret = nullptr;

    if (resolved != false) {

        // Intercept to be able to intercept the underlying functions
        if (procname != nullptr) {
            if (std::string (procname).compare ("eglGetPlatformDisplayEXT") == 0) {
                LOG (_2CSTR ("Intercepting eglGetProcAddress and replacing it with a local function"));

                ret = reinterpret_cast <__eglMustCastToProperFunctionPointerType> ( &eglGetPlatformDisplayEXT );
            }

            if (std::string (procname).compare ("eglCreatePlatformWindowSurfaceEXT") == 0) {
                LOG (_2CSTR ("Intercepting eglGetProcAddress and replacing it with a local function"));

                ret = reinterpret_cast <__eglMustCastToProperFunctionPointerType> ( &eglCreatePlatformWindowSurfaceEXT );
            }
        }

        if (ret == nullptr) {
            LOG (_2CSTR ("Calling Real eglGetProcAddress"));

            ret = reinterpret_cast <__eglMustCastToProperFunctionPointerType> ( _eglGetProcAddress (procname) );
        }
    }
    else {
        LOG (_2CSTR ("Real eglGetProcAddress not found"));
        assert (false);
    }

    return ret;
}

EGLDisplay eglGetPlatformDisplayEXT (EGLenum platform, void* native_display, const EGLAttrib* attrib_list) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLDisplay (*_eglGetPlatformDisplayEXT) (EGLenum, void*, const EGLAttrib*) = nullptr;
/**/    static EGLDisplay (*_eglGetPlatformDisplayEXT) (EGLenum, void*, const EGLAttrib*) = nullptr;

//    static thread_local bool resolved = false;
/**/    static bool resolved = false;

    if (resolved != true) {
        // Deliberatly another funtion because extentions are resolved internally
        void* (*_eglGetProcAddress) (const char*) = nullptr;

        if (lookup ("eglGetProcAddress", reinterpret_cast <uintptr_t&> (_eglGetProcAddress)) != false) {
            _eglGetPlatformDisplayEXT = reinterpret_cast <EGLDisplay (*) (EGLenum, void*, const EGLAttrib*)> ( _eglGetProcAddress("eglGetPlatformDisplayEXT") );
        }

        resolved = _eglGetPlatformDisplayEXT != nullptr;
    }

/**/    Platform::Instance ().Unlock ();

    EGLDisplay ret = EGL_NO_DISPLAY;

    if (resolved != false) {
        LOG (_2CSTR ("Calling Real eglGetPlatformDisplayEXT"));

        ret = _eglGetPlatformDisplayEXT (platform, native_display, attrib_list);

        if (ret != EGL_NO_DISPLAY && platform == EGL_PLATFORM_GBM_KHR) {
//TODO: Extra validation Platform::isGBMDevice

            LOG (_2CSTR ("Detected GBM platform, hence, native_display is a struct gbm_device*"));

            if (Platform::Instance ().Add (ret, reinterpret_cast <EGLNativeDisplayType> (native_display)) != false) {
                // Hand over the result
            }
            else {
                // Probably already added but not / never removed
                assert (false);
            }
        }
    }
    else {
        LOG (_2CSTR ("Real eglGetPlatformDisplayEXT not found"));
        assert (false);
    }

    return ret;
}

EGLSurface eglCreatePlatformWindowSurfaceEXT (EGLDisplay dpy, EGLConfig config, void* native_window, const EGLAttrib* attrib_list) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLSurface (*_eglCreatePlatformWindowSurfaceEXT) (EGLDisplay, EGLConfig, void*, const EGLAttrib*) = nullptr;
/**/    static EGLSurface (*_eglCreatePlatformWindowSurfaceEXT) (EGLDisplay, EGLConfig, void*, const EGLAttrib*) = nullptr;

//    static thread_local bool resolved = false;
/**/    static bool resolved = false;

    if (resolved != true) {
        // Deliberatly another funtion because extentions are resolved internally
        void* (*_eglGetProcAddress) (const char*) = nullptr;

        if (lookup ("eglGetProcAddress", reinterpret_cast <uintptr_t&> (_eglGetProcAddress)) != false) {
            _eglCreatePlatformWindowSurfaceEXT = reinterpret_cast <EGLSurface (*) (EGLDisplay, EGLConfig, void*, const EGLAttrib*)> ( _eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT") );
        }

        resolved = _eglCreatePlatformWindowSurfaceEXT != nullptr;
    }

/**/    Platform::Instance ().Unlock ();

    EGLSurface ret = EGL_NO_SURFACE;

    if (resolved != false) {
        LOG (_2CSTR ("Calling Real eglCreatePlatformWindowSurfaceEXT"));

        ret = _eglCreatePlatformWindowSurfaceEXT (dpy, config, native_window, attrib_list);

        if (ret != EGL_NO_SURFACE && Platform::Instance ().isGBMsurface ( reinterpret_cast <EGLNativeWindowType> (native_window) ) != false) {
            if (Platform::Instance ().Add (ret, reinterpret_cast <EGLNativeWindowType> (native_window)) != false) {
                // Hand over the result
            }
            else {
                // Probably already added but not / never removed
                assert (false);
            }
        }
    }
    else {
        LOG (_2CSTR ("Real egliCreatePlatformWindowSurfaceEXT not found"));
        assert (false);
    }

    return ret;
}

// EGL 1.4 support

EGLDisplay eglGetDisplay (EGLNativeDisplayType display_id) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLDisplay (*_eglGetDisplay) (EGLNativeDisplayType) = nullptr;
/**/    static EGLDisplay (*_eglGetDisplay) (EGLNativeDisplayType) = nullptr;

//    static thread_local bool resolved = false;
    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglGetDisplay", reinterpret_cast <uintptr_t&> (_eglGetDisplay));
    }

/**/    Platform::Instance ().Unlock ();

    EGLDisplay ret = EGL_NO_DISPLAY;

    if (resolved != false) {
#ifndef _MESADEBUG
        LOG (_2CSTR ("Calling Real eglGetDisplay"));

        ret = _eglGetDisplay (display_id);

        if (ret != EGL_NO_DISPLAY && Platform::Instance ().isGBMdevice (display_id) != false) {
            if (Platform::Instance ().Add (ret, display_id) != false) {
                // Hand over the result
            }
            else {
                // Probably already added but not / never removed
                assert (false);
            }
        }
#else
        if (Platform::Instance ().isGBMdevice (display_id) != false) {
            // Force eglGetPlatformDisplay or eglGetPlatformDisplayEXT to avoid the MESA bug
            // Make it explicit that the platform is GBM

            // Default to the implementation defaults; required attributes are implicitly specified
            const EGLAttrib _attribs[] = { EGL_NONE };

            ret = eglGetPlatformDisplayEXT (EGL_PLATFORM_GBM_KHR, display_id, &_attribs[0]);

            if (ret == EGL_NO_DISPLAY) {
                ret = eglGetPlatformDisplay (EGL_PLATFORM_GBM_MESA, display_id, &_attribs[0]);
            }
        }

        // Fallback(s)

        if (ret == EGL_NO_DISPLAY) {
            ret = _eglGetDisplay (display_id);

            if (ret != EGL_NO_DISPLAY) {
                if (Platform::Instance ().Add (ret, display_id) != false) {
                    // Hand over the result
                }
                else {
                    // Probably already added but not / never removed
                    assert (false);
                }
            }
        }
#endif
    }
    else {
        LOG (_2CSTR ("Real eglGetDisplay not found"));
        assert (false);
    }

    return ret;
}

EGLBoolean eglTerminate (EGLDisplay dpy) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLBoolean (*_eglTerminate) (EGLDisplay) = nullptr;
/**/    static EGLBoolean (*_eglTerminate) (EGLDisplay) = nullptr;

//    static thread_local bool resolved = false;
/**/    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglTerminate", reinterpret_cast <uintptr_t&> (_eglTerminate));
    }

/**/    Platform::Instance ().Unlock ();

    EGLBoolean ret = EGL_FALSE;

    if (resolved != false) {
        LOG (_2CSTR ("Calling Real eglTerminate"));

        ret = _eglTerminate (dpy);

        if (ret != EGL_FALSE) {
            // All resources are marked for deletion; EGLDisplay handles remain valid. Other handles are invalidated and once used may result in errors
            // eglReleaseThread and eglMakeCurrent can be called to complete deletion of resources

            // Remove all EGLSurface from the list
            if (Platform::Instance ().Remove (EGL_NO_SURFACE, true) != true) {
                assert (false);
            }
        }
    }
    else {
        LOG (_2CSTR ("Real eglTerminate not found"));
        assert (false);
    }

    return ret;

}

EGLBoolean eglChooseConfig (EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs, EGLint config_size, EGLint* num_config) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLBoolean (*_eglChooseConfig) (EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) = nullptr;
/**/    static EGLBoolean (*_eglChooseConfig) (EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) = nullptr;

//    static thread_local bool resolved = false;
/**/    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglChooseConfig", reinterpret_cast <uintptr_t&> (_eglChooseConfig));
    }

/**/    Platform::Instance ().Unlock ();

    EGLBoolean ret = EGL_FALSE;

    if (resolved != false) {

        LOG (_2CSTR ("Calling Real eglChooseConfig"));

        ret = eglGetConfigs (dpy, nullptr, config_size, num_config);

        if (ret != EGL_FALSE && *num_config > 0) {

            EGLConfig _configs [*num_config];

            ret = _eglChooseConfig (dpy, attrib_list, &_configs [0], *num_config, num_config);

            // Do not filter for (platform unrelated) pbuffers
            bool _available = false;

            if (attrib_list != nullptr) {
                for (size_t i = 0; attrib_list [i] != EGL_NONE && _available != true; i++) {
                    if (i > 1 && attrib_list [i-1] == EGL_SURFACE_TYPE) {
                        _available = (attrib_list [i] & EGL_PBUFFER_BIT) == EGL_PBUFFER_BIT;
                    }
                }
            }

            // Temporary placeholder
            std::vector <EGLConfig> _vector;

            if (_available != true) {

                if (ret != EGL_FALSE) {

                    _vector.insert (_vector.end (), &_configs[0], &_configs[*num_config]);

                    // Filter the configs
                    Platform::Instance ().FilterConfigs (dpy, _vector);

                    *num_config = _vector.size ();
                }

            }
            else {
                LOG (_2CSTR ("Off screen pbuffer support requested. Frame buffer configuration NOT filtered."));
            }


            if (ret != EGL_FALSE && configs != nullptr) {
                *num_config = config_size > *num_config ? *num_config : config_size;

                /* void* */ memcpy (configs, _vector.data (), *num_config * sizeof (EGLConfig));
            }
        }
    }
    else {
        LOG (_2CSTR ("Real eglChooseConfig not found"));
        assert (false);
    }

    return ret;
}

EGLSurface eglCreateWindowSurface (EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint* attrib_list) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLSurface (*_eglCreateWindowSurface) (EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) = nullptr;
/**/    static EGLSurface (*_eglCreateWindowSurface) (EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) = nullptr;

//    static thread_local bool resolved = false;
    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglCreateWindowSurface", reinterpret_cast <uintptr_t&> (_eglCreateWindowSurface));
    }

/**/    Platform::Instance ().Unlock ();

    EGLSurface ret = EGL_NO_SURFACE;

    if (resolved != false) {
#ifndef _MESADEBUG
        LOG (_2CSTR ("Calling Real eglCreateWindowSurface"));

        ret = _eglCreateWindowSurface(dpy, config, win, attrib_list);

        if (ret != EGL_NO_SURFACE && Platform::Instance ().isGBMsurface (win) != false) {
            if (Platform::Instance ().Add (ret, win) != false) {
                // Hand over the result
            }
            else {
                // Probably already added but not / never removed
                assert (false);
            }
        }
#else
        if (Platform::Instance ().isGBMsurface (win) != false) {
            // Force eglCreatePlatformWindowSurface or eglCreatePlatformWindowSurfaceEXT
            ret = eglCreatePlatformWindowSurfaceEXT (dpy, config, win, attrib_list);

            if (ret == EGL_NO_SURFACE) {
                ret = eglCreatePlatformWindowSurface (dpy, config, win, attrib_list);
            }

            // Fallback(s)

            if (ret == EGL_NO_SURFACE) {
                ret = _eglCreateWindowSurface(dpy, config, win, attrib_list);

                if (ret != EGL_NO_SURFACE) {
                    if (Platform::Instance ().Add (ret, win) != false) {
                        // Hand over the result
                    }
                    else {
                        // Probably already added but not / never removed
                        assert (false);
                    }
                }
            }
        }
#endif
    }
    else {
        LOG (_2CSTR ("Real eglCreateWindowSurface not found"));
        assert (false);
    }

    return ret;
}

EGLBoolean eglDestroySurface (EGLDisplay dpy, EGLSurface surface) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLBoolean (*_eglDestroySurface) (EGLDisplay, EGLSurface) = nullptr;
/**/    static EGLBoolean (*_eglDestroySurface) (EGLDisplay, EGLSurface) = nullptr;

//    static thread_local bool resolved = false;
    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglDestroySurface", reinterpret_cast <uintptr_t&> (_eglDestroySurface));
    }

/**/    Platform::Instance ().Unlock ();

    EGLBoolean ret = EGL_FALSE;

    if (_eglDestroySurface != nullptr) {
        LOG (_2CSTR ("Calling Real eglDestroySurface"));

        ret = _eglDestroySurface(dpy, surface);

        if (ret != EGL_FALSE) {
            if (Platform::Instance ().Remove (surface) != true) {
                assert (false);
            }
        }
    }
    else {
        LOG (_2CSTR ("Real eglDestroySurface not found"));
        assert (false);
    }

    return ret;
}

EGLBoolean eglSwapBuffers (EGLDisplay dpy, EGLSurface surface) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLBoolean (*_eglSwapBuffers) (EGLDisplay, EGLSurface) = nullptr;
/**/    static EGLBoolean (*_eglSwapBuffers) (EGLDisplay, EGLSurface) = nullptr;

//    static thread_local bool resolved = false;
    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglSwapBuffers", reinterpret_cast <uintptr_t&> (_eglSwapBuffers));
    }

/**/    Platform::Instance ().Unlock ();

    EGLBoolean ret = EGL_FALSE;

    if (resolved != false) {
#ifdef _MESADEBUG
        if ( eglGetCurrentContext ()         != EGL_NO_CONTEXT && \
             eglGetCurrentDisplay ()         == dpy            && \
             eglGetCurrentSurface (EGL_READ) == surface        && \
             eglGetCurrentSurface (EGL_DRAW) == surface) {

            LOG (_2CSTR ("Valid current context / display / surface"));

            EGLint _value;
            if (EGL_FALSE != eglQueryContext (dpy, eglGetCurrentContext(), EGL_RENDER_BUFFER, &_value)) {
                switch (_value) {
                    case EGL_BACK_BUFFER    : LOG (_2CSTR ("Back buffer being used"));   break; // Possibly double buffering
                    case EGL_SINGLE_BUFFER  : LOG (_2CSTR ("Single buffer being used")); break; // No double, triple, ... buffering
                    case EGL_NONE           :
                    default                 : LOG (_2CSTR ("Context not bound to surface"));
                }
            }

            if (EGL_FALSE != eglQuerySurface (dpy, surface, EGL_WIDTH, &_value)) {
                LOG (_2CSTR ("Surface witdh "), _value);
            }

            if (EGL_FALSE != eglQuerySurface (dpy, surface, EGL_HEIGHT, &_value)) {
                LOG (_2CSTR ("Surface height "), _value);
            }

            if (EGL_FALSE != eglQuerySurface (dpy, surface, EGL_SWAP_BEHAVIOR, &_value)) {
                constexpr char _prefix [] = "Surface color buffer behavior is ";
                constexpr char _postfix [] = ", but ancillary buffer behavior is always UNKNOWN";
                switch (_value) {
                    case EGL_BUFFER_DESTROYED   : LOG (_prefix, _2CSTR ("DESTROY") , _postfix);  break;
                    case EGL_BUFFER_PRESERVED   : LOG (_prefix, _2CSTR ("PRESERVE"), _postfix); break;
                    default                     : LOG (_prefix, _2CSTR ("UNKNOWN") , _postfix);
                }
            }

            LOG (_2CSTR ("Calling Real eglSwapBuffers for EGLSurface "), surface, _2CSTR (" on thread "), syscall (SYS_gettid));
#else
            LOG (_2CSTR ("Calling Real eglSwapBuffers"));
#endif

            // MESA expects a call to the gbm_surface_lock_front_buffer prior any subseqent eglSwapBuffers to avoid an internal error
            ret = _eglSwapBuffers (dpy, surface);

            if (ret != EGL_FALSE && Platform::Instance ().ScanOut (surface) != false) {
                // Nothing
            }
            else {
                LOG (_2CSTR ("Not performing a platform enabled scan out"));
            }
#ifdef _MESADEBUG
        }
        else {
            // Has an eglMakeCurrent been called?
            LOG (_2CSTR ("No Valid context / display / surface detected"));
        }
#endif
    }
    else {
        LOG (_2CSTR ("Real eglSwapBuffers not found"));
        assert (false);
    }

    return ret;
}

#ifdef _MESADEBUG
EGLContext eglCreateContext (EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint* attrib_list) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLContext (*_eglCreateContext) (EGLDisplay, EGLConfig, EGLContext, const EGLint*) = nullptr;
/**/    static EGLContext (*_eglCreateContext) (EGLDisplay, EGLConfig, EGLContext, const EGLint*) = nullptr;

//    static thread_local bool resolved = false;
/**/    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglCreateContext", reinterpret_cast <uintptr_t&> (_eglCreateContext));
    }

/**/    Platform::Instance ().Unlock ();

    EGLContext ret = EGL_NO_CONTEXT;

    if (resolved != false) {
        LOG (_2CSTR ("Calling Real eglCreateContext"));

        EGLint _value;
        if (EGL_TRUE != eglGetConfigAttrib (dpy, config, EGL_CONFIG_ID, &_value)) {
            // Error
            LOG (_2CSTR ("Unable to determine EGL_CONFIG_ID"));
        }
        else {
            LOG (_2CSTR ("EGL config : "), config, _2CSTR (" : EGL_CONFIG_ID : "), _value);
        }

        if (EGL_TRUE != eglGetConfigAttrib (dpy, config, EGL_BUFFER_SIZE, &_value)) {
            LOG (_2CSTR ("Unable to determine buffer size"));
        }
        else {
            LOG (_2CSTR ("EGL config : "), config, _2CSTR (" : EGL_BUFFER_SIZE : "), _value);
        }

        if (EGL_TRUE != eglGetConfigAttrib (dpy, config, EGL_ALPHA_SIZE, &_value)) {
            LOG (_2CSTR ("Unable to determine alpha size"));
        }
        else {
            LOG (_2CSTR ("EGL config : "), config, _2CSTR (" : EGL_ALPHA_SIZE : "), _value);
        }

        if (EGL_TRUE != eglGetConfigAttrib (dpy, config, EGL_RED_SIZE, &_value)) {
            LOG (_2CSTR ("Unable to determine red size"));
        }
        else {
            LOG (_2CSTR ("EGL config : "), config, _2CSTR (" : EGL_RED_SIZE : "), _value);
        }

        if (EGL_TRUE != eglGetConfigAttrib (dpy, config, EGL_BLUE_SIZE, &_value)) {
            LOG (_2CSTR ("Unable to determine blue size"));
        }
        else {
            LOG (_2CSTR ("EGL config : "), config, _2CSTR (" : EGL_BLUE_SIZE : "), _value);
        }

        if (EGL_TRUE != eglGetConfigAttrib (dpy, config, EGL_GREEN_SIZE, &_value)) {
            LOG (_2CSTR ("Unable to determine green size"));
        }
        else {
            LOG (_2CSTR ("EGL config : "), config, _2CSTR (" : EGL_GREEN_SIZE : "), _value);
        }

        ret = _eglCreateContext (dpy, config, share_context, attrib_list);
    }
    else {
        LOG (_2CSTR ("Real eglCreateContext not found"));
        assert (false);
    }

    return ret;
}

EGLBoolean eglDestroyContext (EGLDisplay dpy, EGLContext ctx) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLBoolean (*_eglDestroyContext) (EGLDisplay, EGLContext) = nullptr;
/**/    static EGLBoolean (*_eglDestroyContext) (EGLDisplay, EGLContext) = nullptr;

//    static thread_local bool resolved = false;
/**/    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglDestroyContext", reinterpret_cast <uintptr_t&> (_eglDestroyContext));
    }

/**/    Platform::Instance ().Unlock ();

    EGLBoolean ret = EGL_FALSE;

    if (resolved != false) {

        // The maps cannot be cleared just yet. EGL (resources) are only marked for deletion.
        // Shared context can still use them

        LOG (_2CSTR ("Calling Real eglDestroyContext"));

        ret = _eglDestroyContext (dpy, ctx);
    }
    else {
        LOG (_2CSTR ("Real eglDestroyContext not found"));
        assert (false);
    }

    return ret;
}

EGLSurface eglCreatePbufferSurface (EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLSurface (*_eglCreatePbufferSurface) (EGLDisplay, EGLConfig, const EGLint*) = nullptr;
/**/    static EGLSurface (*_eglCreatePbufferSurface) (EGLDisplay, EGLConfig, const EGLint*) = nullptr;

//    static thread_local bool resolved = false;
/**/    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglCreatePbufferSurface", reinterpret_cast <uintptr_t&> (_eglCreatePbufferSurface));
    }

/**/    Platform::Instance ().Unlock ();

    EGLSurface ret = EGL_NO_SURFACE;

    if (resolved != false) {
        LOG (_2CSTR ("Calling Real eglCreatePbufferSurface"));

        ret = _eglCreatePbufferSurface(dpy, config, attrib_list);
    }
    else {
        LOG (_2CSTR ("Real eglCreatePbufferSurface not found"));
        assert (false);
    }

    return ret;
}

EGLBoolean eglMakeCurrent (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLBoolean (*_eglMakeCurrent) (EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
/**/    static EGLBoolean (*_eglMakeCurrent) (EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;

//    static thread_local bool resolved = false;
/**/    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglMakeCurrent", reinterpret_cast <uintptr_t&> (_eglMakeCurrent));
    }

/**/    Platform::Instance ().Unlock ();

    EGLBoolean ret = EGL_FALSE;

    if (resolved != false) {
        LOG (_2CSTR ("Calling Real eglMakeCurrent for surface (draw/read) "), draw, _2CSTR (" / "), read, _2CSTR (" on thread "), syscall (SYS_gettid));

         ret = _eglMakeCurrent (dpy, draw, read, ctx);
    }
    else {
        LOG (_2CSTR ("Real eglMakeCurrent not found"));
        assert (false);
    }

    return ret;
}
#endif

// EGL 1.5 support

EGLDisplay eglGetPlatformDisplay (EGLenum platform, void* native_display, const EGLAttrib* attrib_list) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLDisplay (*_eglGetPlatformDisplay) (EGLenum, void*, const EGLAttrib*) = nullptr;
/**/    static EGLDisplay (*_eglGetPlatformDisplay) (EGLenum, void*, const EGLAttrib*) = nullptr;

//    static thread_local bool resolved = false;
/**/    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglGetPlatformDisplay", reinterpret_cast <uintptr_t&> (_eglGetPlatformDisplay));
    }

/**/    Platform::Instance ().Unlock ();

    EGLDisplay ret = EGL_NO_DISPLAY;

    if (resolved != false) {

        LOG (_2CSTR ("Calling Real eglGetPlatformDisplay"));

        ret = _eglGetPlatformDisplay (platform, native_display, attrib_list);

        static_assert (EGL_PLATFORM_GBM_KHR == EGL_PLATFORM_GBM_MESA);

        if (ret != EGL_NO_SURFACE && platform == EGL_PLATFORM_GBM_KHR) {

            // Create a mapping; the caller is responsible for being truthful
            LOG (_2CSTR ("Detected GBM platform, hence, native_display is a struct gbm_device*"));

            if (Platform:: Instance ().Add (ret, reinterpret_cast <EGLNativeDisplayType> (native_display)) != false) {
                // Hand over the result
            }
            else {
                // Probably already added but not / never removed
                assert (false);
            }
        }

        ret = _eglGetPlatformDisplay (platform, native_display, attrib_list);
    }
    else {
        LOG (_2CSTR ("Real eglGetPlatformDisplay not found"));
        assert (false);
    }

    return ret;
}

EGLSurface eglCreatePlatformWindowSurface (EGLDisplay dpy, EGLConfig config, void* native_window, const EGLAttrib* attrib_list) {
/**/    Platform::Instance ().Lock ();

//    static thread_local EGLSurface (*_eglCreatePlatformWindowSurface) (EGLDisplay, EGLConfig, void*, const EGLAttrib*) = nullptr;
/**/    static EGLSurface (*_eglCreatePlatformWindowSurface) (EGLDisplay, EGLConfig, void*, const EGLAttrib*) = nullptr;

//    static thread_local bool resolved = false;
/**/    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("eglCreatePlatformCreateWindowSurface", reinterpret_cast <uintptr_t&> (_eglCreatePlatformWindowSurface));
    }

/**/    Platform::Instance ().Unlock ();

    EGLSurface ret = EGL_NO_SURFACE;

    if (resolved != false) {

        LOG (_2CSTR ("Calling Real eglCreatePlatformWindowSurface"));

        ret = _eglCreatePlatformWindowSurface (dpy, config, native_window, attrib_list);

        if (ret != EGL_NO_SURFACE && Platform::Instance ().isGBMsurface ( reinterpret_cast <EGLNativeWindowType> (native_window) ) != false) {

            LOG (_2CSTR ("Detected GBM platform, hence, native_window is a struct gbm_surface*"));

            if (Platform::Instance ().Add (ret, reinterpret_cast <EGLNativeWindowType> (native_window)) != false) {
                // Hand over the result
            }
            else {
                // Probably already added but not / never removed
                assert (false);
            }
        }
    }
    else {
        LOG (_2CSTR ("Real eglCreatePlatformWindowSurface not found"));
#ifndef _MESADEBUG
        assert (false);
#endif
    }

    return ret;
}
