/*
    First attempt. Possibly wrong and / or incomplete, and may contain 'bad' code and / or coding practice.

    Interpostion library named libgbm.so. Assume lazy symbol resolution by default.
    Link against the real gbm library to get the ELF 'NEEDED' recorded. Assume that library is named librealgbm.so.
    Possibly, 'tell' the linker to ignore unresolved symbols.

    RDK licence and copyright (may) apply.
*/

#include "common.h"

#include <string>

// A little less code bloat
#define _2CSTR(str) std::string (str).c_str ()

#define PROXYGBM_PRIVATE COMMON_PRIVATE
#define PROXYGBM_PUBLIC COMMON_PUBLIC

#define PROXYGBM_UNUSED __attribute__ ((unused))

#include <unordered_map>
#include <unordered_set>

#ifdef __cplusplus
extern "C" {
#endif
#include <gbm.h>
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

PROXYGBM_PUBLIC struct gbm_bo* gbm_surface_lock_front_buffer (struct gbm_surface* surface);
PROXYGBM_PUBLIC void gbm_surface_release_buffer (struct gbm_surface* surface, struct gbm_bo* bo);
PROXYGBM_PUBLIC int gbm_surface_has_free_buffers (struct gbm_surface* surface);

PROXYGBM_PUBLIC struct gbm_surface* gbm_surface_create (struct gbm_device* gbm, uint32_t width, uint32_t height, uint32_t format, uint32_t flags);
PROXYGBM_PUBLIC struct gbm_surface* gbm_surface_create_with_modifiers (struct gbm_device* gbm, uint32_t width, uint32_t height, uint32_t format, const uint64_t* modifiers, const unsigned int count);

PROXYGBM_PUBLIC void gbm_surface_destroy (struct gbm_surface* surface);
PROXYGBM_PUBLIC void gbm_device_destroy (struct gbm_device* device);

PROXYGBM_PUBLIC struct gbm_device* gbm_create_device (int fd);

#ifdef __cplusplus
}
#endif

namespace {
// Suppress compiler preprocessor 'visibiity ignored' in anonymous namespace
#define _PROXYGBM_PRIVATE /*PROXYGBM_PRIVATE*/
#define _PROXYGBM_PUBLIC PROXYGBM_PUBLIC
#define _PROXYGBM_UNUSED PROXYGBM_UNUSED

class Platform : public Singleton <Platform> {
    // This (These) friend(s) has (have) access to all memebers!

    friend Singleton <Platform>;

    public :

        _PROXYGBM_PRIVATE bool Add (const gbm_surface* surface, const struct gbm_bo* bo);
        _PROXYGBM_PRIVATE bool Remove (const gbm_surface* surface, const struct gbm_bo* bo = nullptr);

        _PROXYGBM_PRIVATE bool Add (const gbm_device* device, const struct gbm_surface* surface);
        _PROXYGBM_PRIVATE bool Remove (const gbm_device* device, const struct gbm_surface* surface = nullptr);

        // All tracked buffers
        _PROXYGBM_PRIVATE const std::unordered_map <const struct gbm_bo*, uint32_t> Buffers (const struct gbm_surface* surface) const;

        struct gbm_bo* PredictedBuffer (std::unordered_map <const struct gbm_bo*, uint32_t> buffers) const;

        _PROXYGBM_PRIVATE bool Exist (const struct gbm_device*) const;
        _PROXYGBM_PRIVATE bool Exist (const struct gbm_surface*) const;

        _PROXYGBM_PRIVATE void Lock (void) const;
        _PROXYGBM_PRIVATE void Unlock (void) const;

        _PROXYGBM_PRIVATE uint16_t Sequence (uint32_t value) const {
            return static_cast <uint16_t> (value >> 16);
        }

        _PROXYGBM_PRIVATE uint16_t Count (uint32_t value) const {
            return static_cast <uint16_t> (value & static_cast <uint32_t> (CountBitmask ()));
        }

    protected :

        //  Nothing

    private :

        pthread_once_t _mutex_initialized;
        mutable pthread_mutex_t _mutex;

        std::unordered_map < const struct gbm_surface*, std::unordered_map <const struct gbm_bo*, uint32_t> > _map_surf;
        std::unordered_map < const struct gbm_device*, std::unordered_set <const struct gbm_surface*> > _map_dev;

        // Previous frame

        Platform () {
            /*void*/ _map_surf.clear ();
            /*void*/ _map_dev.clear ();
        }

        virtual ~Platform () = default;

        // Implementation possibly have no more than SequenceMaxValue internal buffers for use with 'lock' and 'release'
        // Per surface!

        _PROXYGBM_PRIVATE static constexpr uint16_t SequenceInitialValue (void) {
            return 0;
        }

        _PROXYGBM_PRIVATE static constexpr uint16_t SequenceMaxValue (void) {
            return 4;
        }

        _PROXYGBM_PRIVATE static constexpr uint16_t CountInitialValue (void) {
            return 1;
        }

        _PROXYGBM_PRIVATE static constexpr uint16_t CountMaxValue () {
            return 4;
        }

        // Extract part of the struct gbm_bo* paired value, here reference count
        _PROXYGBM_PRIVATE static constexpr uint16_t CountBitmask () {
            return 0x00FF;
        }

        // Extract part of the struct gbm_bo* paired value, here sequence number
        _PROXYGBM_PRIVATE static constexpr uint16_t SequenceBitmask () {
            return 0xFF00;
        }

        _PROXYGBM_PRIVATE uint32_t Convert (uint16_t count, uint16_t sequence) {
            return count + (sequence << 16);
        }

        _PROXYGBM_PRIVATE bool Age (std::unordered_map <const struct gbm_bo*, uint32_t>& buffers);
        _PROXYGBM_PRIVATE bool Rejuvenate (std::unordered_map <const struct gbm_bo*, uint32_t>& buffers);

// TODO: probably misused at places
        _PROXYGBM_PRIVATE static constexpr uint32_t NonEmptySizeInitialValue (void) {
            return 1;
        }

       _PROXYGBM_PRIVATE bool InitLock (void);
};

void Platform::Lock (void) const {
    if (Instance ().InitLock () != false && pthread_mutex_lock (&_mutex) != 0) {
        // Error
        assert (false);
    }
}

void Platform::Unlock (void) const {
    if (Platform::Instance ().InitLock () != false && pthread_mutex_unlock (&_mutex) !=0) {
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
            pthread_mutexattr_settype (&_attr, PTHREAD_MUTEX_RECURSIVE/*PTHREAD_MUTEX_ERRORCHECK*/) != 0 || \
            pthread_mutex_init (&Platform::Instance ()._mutex, &_attr) ) {

            // Error
            assert (false);
        }
    };

    bool ret = false;

    if (pthread_once (&_mutex_initialized, init_once) != 0) {
        LOG (_2CSTR ("Unable to initialize required mutex"));
        assert (false);
    }
    else {
        ret = true;
    }

    return ret;
}

const std::unordered_map <const struct gbm_bo*, uint32_t> Platform::Buffers (const struct gbm_surface* surface) const {
    Lock ();

    std::unordered_map <const struct gbm_bo*, uint32_t> result;

    if (surface != nullptr) {
        auto _it = _map_surf.find (surface);

        if (_it != _map_surf.end ()) {
            result = _it->second;
        }
    }

    Unlock ();

    return result;
}

struct gbm_bo* Platform::PredictedBuffer (std::unordered_map <const struct gbm_bo*, uint32_t> buffers) const {
    Lock ();

    struct gbm_bo* ret = nullptr;

    decltype (CountInitialValue ()) _count = CountInitialValue ();
    decltype (SequenceInitialValue ()) _sequence = SequenceInitialValue ();

    for (auto _it = buffers.begin (), _end = buffers.end (); _it != _end; _it++) {
        // The list should contain sane entries
        assert (_it->first != nullptr && Count (_it->second) >= CountInitialValue ());

        // Oldest, with lowest count is most probably the one required by a successive call to gbm_surface_lock_front_buffer
        if (Sequence (_it->second) >= _sequence) {
            if (Count (_it->second) <= _count) {
                _count = Count (_it->second);
                ret = const_cast <struct gbm_bo*> (_it->first);
                _sequence = Sequence (_it->second);
            }
        }
    }

    Unlock ();

    return ret;
}

bool Platform::Age (std::unordered_map <const struct gbm_bo*, uint32_t>& buffers) {
    bool ret = buffers.size () >= NonEmptySizeInitialValue ();

    for (auto _it_buf = buffers.begin (), _end = buffers.end (); _it_buf != _end; _it_buf++) {
        decltype (_it_buf->second) _count = Count (_it_buf->second);
        decltype (_it_buf->second) _sequence = Sequence (_it_buf->second);

        assert (_sequence < SequenceMaxValue ());

        _sequence++;

        _it_buf->second = Convert (_count, _sequence);
    }

    return ret;
}

bool Platform::Rejuvenate (std::unordered_map <const struct gbm_bo*, uint32_t>& buffers) {
    bool ret = buffers.size () >= NonEmptySizeInitialValue ();

    for (auto _it_buf = buffers.begin (), _end = buffers.end (); _it_buf != _end; _it_buf++) {
        decltype (_it_buf->second) _count = Count (_it_buf->second);
        decltype (_it_buf->second) _sequence = Sequence (_it_buf->second);

        assert (_sequence > SequenceInitialValue ());

        _sequence--;

        _it_buf->second = Convert (_count, _sequence);
    }

    return ret;
}

bool Platform::Add (const struct gbm_surface* surface, const struct gbm_bo* bo) {
    Lock ();

    auto _it_surf = _map_surf.find (surface);

    std::unordered_map <const struct gbm_bo*, uint32_t> _buffers;

    if (_it_surf != _map_surf.end ()) {
        // Surface buffers entry exists

        _buffers = _it_surf->second;
    }
    else {
        // No known surface
        if (surface != nullptr) {
            auto __it_surf = _map_surf.insert (std::pair < const struct gbm_surface*, std::unordered_map <const struct gbm_bo*, uint32_t> > (surface, _buffers));

            // The iterator may have become singular

            if (__it_surf.second != false) {
                assert (__it_surf.first != _map_surf.end ());

                _it_surf = __it_surf.first;
            }
        }
        else {
            assert (false);
        }
    }

    auto _it_buf = _buffers.find (bo);

    if (_it_buf != _buffers.end ()) {
        // bo exist
        assert (Count (_it_buf->second) >= CountInitialValue ());

        decltype (_it_buf->second) _count = Count(_it_buf->second);
        decltype (_it_buf->second) _sequence = Sequence(_it_buf->second);

        assert (_count < CountMaxValue ());

        _count++;

        _it_buf->second = Convert (_count, _sequence);
    }
    else {
        //  bo does not exist or bo equals nullptr

        if (bo != nullptr) {
            auto __it_buf = _buffers.insert (std::pair <const struct gbm_bo*, uint32_t> (bo, Convert (CountInitialValue (), SequenceInitialValue ())));

            // The iterator may have become singular

            if ( __it_buf.second != false) {
                assert (__it_buf.first != _buffers.end ());

                _it_buf = __it_buf.first;

                if (Age (_buffers) != true) {
                    assert (false);
                }
            }
        }
        else {
            // Empty set
        }
    }

    bool ret = false;

    if (_it_surf != _map_surf.end ()) {

        // Successfully added or empty set
        if ((bo != nullptr && _it_buf != _buffers.end ()) || (bo == nullptr && _buffers.empty () != false)) {
            _it_surf->second = _buffers;

            ret  = true;
        }

    }
    else {
        // Surface is a nullptr or could not be added, in debug mode, only, could not be added
        assert (false);
    }

    Unlock ();

    return ret;
}

bool Platform::Remove (const struct gbm_surface* surface, const struct gbm_bo* bo) {
    Lock ();

    bool ret = false;

    auto _it_surf = _map_surf.find (surface);

    std::unordered_map <const struct gbm_bo*, uint32_t> _buffers;

    if (_it_surf != _map_surf.end ()) {
        // Surface buffers entry exists

        _buffers = _it_surf->second;
    }
    else {
        // No known surface
        if (surface != nullptr) {
            assert (false);
        }
        else {
            // Remove all surfaces and consequently all devices
            /*void*/ _map_dev.clear ();
            /*void*/ _map_surf.clear ();

            // iterator has become singular
            _it_surf = _map_surf.end ();

            assert (_map_dev.size () < NonEmptySizeInitialValue ());
            assert (_map_surf.size () < NonEmptySizeInitialValue ());
        }
    }

    auto _it_buf = _buffers.find (bo);

    if (_it_buf != _buffers.end ()) {
        assert (bo != nullptr);

        // bo exist
        assert (Count (_it_buf->second) >= CountInitialValue ());

        switch (Count (_it_buf->second)) {
            case 0  :   // Error, it should not happen, try to make the _map sane
                        assert (false);
            case 1  :  // Count reduces to 0

                        switch (_buffers.erase (bo)) {
                            case 1  :   // Expected, iterator has become singular
                                        ret = Rejuvenate (_buffers);
                                        break;
                            case 0  :;
                            default :   // Unexpected, iterator has become singular
                                        _it_buf = _buffers.end ();
                                        assert (false);
                        }

                        break;
            default :   // > 1
                        decltype (_it_buf->second) _count = Count (_it_buf->second);
                        decltype (_it_buf->second) _sequence = SequenceInitialValue(); //Sequence (_it_buf->second);

                        assert (_count > CountInitialValue ());

                        _count--;

                        _it_buf->second = Convert (_count, _sequence);

                        ret = true;
        }
    }
    else {
        // Unknown bo or bo equals nullptr, nullptr is not an element in the map

        if (bo == nullptr) {
            // All associated bos have to be removed

            /* void */ _buffers.clear ();

            // iterator has become singular

            ret = _buffers.size() < NonEmptySizeInitialValue ();

            assert (ret != false);
        }
        else {
            // Error, untracked bo
#ifdef _NO_RESTART_APPLICATION
            assert (false);
#endif
        }
    }

    if (_it_surf != _map_surf.end ()) {
        if (ret != false /*|| bo == nullptr*/) {
            // An empty set is still valid
            _it_surf->second = _buffers;
        }
    }
    else {
        // Unknown surface or cleared map, in debug mode, only, a cleared map
        ret = surface == nullptr && _map_surf.size () < NonEmptySizeInitialValue ();

        assert (ret != false);
    }

    Unlock ();

    return ret;
}

bool Platform::Add (const gbm_device* device, const struct gbm_surface* surface) {
    Lock ();

    auto _it_dev = _map_dev.find (device);

    std::unordered_set <const struct gbm_surface*> _surfaces;

    if (_it_dev != _map_dev.end ()) {
        // Device surfaces entry exist

        _surfaces = _it_dev->second;
    }
    else {
        // No known device
        if (device != nullptr) {
            auto __it_dev = _map_dev.insert (std::pair < const struct gbm_device*, std::unordered_set <const struct gbm_surface*> > (device, _surfaces));

            // The iterator may have become singular

            if (__it_dev.second != false) {
                assert (__it_dev.first != _map_dev.end ());

                _it_dev = __it_dev.first;
            }
        }
        else {
            assert (false);
        }
    }

    auto _it_surf = _surfaces.find (surface);

    if (_it_surf != _surfaces.end ()) {
        // Surface exist
        assert (false);

        _it_surf = _surfaces.end ();
    }
    else {
        //  Surface does not exist or surface equals nullptr

        if (surface != nullptr) {
            auto __it_surf = _surfaces.insert (surface);

            // The iterator may have become singular

            if ( __it_surf.second != false) {
                assert (__it_surf.first != _surfaces.end ());

                _it_surf = __it_surf.first;
            }
        }
        else {
            // Empty set
        }
    }

    bool ret = false;

    if (_it_dev != _map_dev.end ()) {

        // Successfully added or empty set
        if ((surface != nullptr && _it_surf != _surfaces.end ()) || (surface == nullptr && _surfaces.empty () != false)) {

            ret = true;

            if (surface != nullptr) {
                ret = Add (surface, nullptr);
            }

            if (ret != false) {
                _it_dev->second = _surfaces;
            }

            assert (ret != false);
        }
    }
    else {
        // Device is a nullptr or could not be added, in debug mode, only, could not be added
        assert (false);
    }

    Unlock ();

    return ret;
}

bool Platform::Remove (const gbm_device* device, const struct gbm_surface* surface) {
    Lock ();

    bool ret = false;

    auto _it_dev = _map_dev.find (device);

    std::unordered_set <const struct gbm_surface*> _surfaces;

    if (_it_dev != _map_dev.end ()) {
        // Device surfaces entry exist

        _surfaces = _it_dev->second;
    }
    else {
        // Failure or no known device, surfaces are not tracked for unknown devices
        if (device != nullptr) {
            assert (false);
        }
        else {
            // Remove all devices and consequently all surfaces
            /*void*/ _map_dev.clear ();
            /*void*/ _map_surf.clear ();

            // iterator has become singular
            _it_dev = _map_dev.end ();

            assert (_map_dev.size () < NonEmptySizeInitialValue ());
            assert (_map_surf.size () < NonEmptySizeInitialValue ());
        }
    }

    auto _it_surf = _surfaces.find (surface);

    if (_it_surf != _surfaces.end ()) {
        assert (surface != nullptr);

        switch (_surfaces.erase (surface)) {
            case 1  :   // Expected
                        // Iterator has become singular
                        ret = Remove (surface);

                        assert (ret);

                        break;
            case 0  :; 
            default :   // Unexpected
                        // Iterator has become singular
                        _it_surf = _surfaces.end ();
                        assert (false);
        }
    }
    else {
        // Unknown or surface equals nullptr

        if (surface == nullptr) {
            // All associated surfaces have to be removed

            /* void */ _surfaces.clear ();

            // iterator has become singular

            ret = _surfaces.size () < NonEmptySizeInitialValue ();

            assert (ret != false);
        }
        else {
            // Error, untracked surface
            assert (false);
        }
    }

    if (_it_dev != _map_dev.end ()) {
        if (ret != false /*|| surface == nullptr*/) {
            // Successfully removed and/or empty set
            _it_dev->second = _surfaces;

            ret = true;
        }
    }
    else {
        // Unknown device or cleared map, in debug mode, only, a cleared map
        ret = device == nullptr && _map_dev.size () < NonEmptySizeInitialValue ();

        assert (ret);
    }

    Unlock ();

    return ret;
}

bool Platform::Exist (const struct gbm_surface* surface) const {
    bool ret = false;

    Lock ();

    if (surface != nullptr) {
        const auto _it_surf = _map_surf.find (surface);

        // Empty sets should be considered as non-existent
        ret = _it_surf != _map_surf.end () && _it_surf->second.size () >= NonEmptySizeInitialValue ();
    }
    else {
        // Unintended use
        assert (false);
    }

    Unlock ();

    return ret;
}

bool Platform::Exist (const struct gbm_device* device) const {
    bool ret = false;

    Lock ();

    if (device != nullptr) {
        auto _it_dev = _map_dev.find (device);

        // Empty sets should be considered as non-existent
        ret = _it_dev != _map_dev.end () && _it_dev->second.size () >= NonEmptySizeInitialValue ();
    }
    else {
        // Unintended use
        assert (false);
    }

    Unlock ();

    return ret;
}

#undef _PROXYGBM_PRIVATE
#undef _PROXYGBM_PUBLIC
#undef _PROXYGBM_UNUSED
} // Anonymous namespace

struct gbm_bo* gbm_surface_lock_front_buffer (struct gbm_surface* surface) {
/**/    Platform::Instance ().Lock ();

//    static thread_local struct gbm_bo* (*_gbm_surface_lock_front_buffer) (struct gbm_surface*) = nullptr;
/**/    static struct gbm_bo* (*_gbm_surface_lock_front_buffer) (struct gbm_surface*) = nullptr;

    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("gbm_surface_lock_front_buffer", reinterpret_cast <uintptr_t&> (_gbm_surface_lock_front_buffer));
    }

/**/    Platform::Instance ().Unlock ();

    Platform::Instance ().Lock ();

    struct gbm_bo* bo = nullptr;

    if (resolved != false) {

        bool _flag = false;

        if (surface != nullptr) {
            _flag = gbm_surface_has_free_buffers (surface) > 0;

            LOG (_2CSTR ("Calling Real gbm_surface_lock_front_buffer"));

            // This might trigger an internal error
            bo = _gbm_surface_lock_front_buffer (surface);
        }
        else {
            // Error
            assert (false);
        }

        if (bo == nullptr) {
            // gbm_surface_lock_front_buffer (also) fails if eglSwapBuffers has not been called (yet)

            if (_flag != false) {
                // Free buffers so assume a second successive call has just happened without a prior call to eglSwapBuffers

                auto _buffers = Platform::Instance ().Buffers (surface);

                if (_buffers.empty () != true) {

                    // Decision time, without any (prior) knowledge select one of the recorded bo's

                    bo = Platform::Instance ().PredictedBuffer (_buffers);

                    // Existing bo's can be updated by increasing the count
                    if (Platform::Instance (). Add (surface, bo) != true) {
                        LOG (_2CSTR ("Unable to update existing bo"));
                    }
                }
                else {
                    // No recorded bo's, and failure, hand over the result
                }
            }
            else {
                // Error indicative for no free buffers, just hand over the result
            }
        }
        else {
            // A successful gbm_surface_lock_front_buffer

            if (Platform::Instance (). Add (surface, bo) != true) {
                // Unable to track or other error

                /* void */ gbm_surface_release_buffer (surface, bo);

                bo = nullptr;

                assert (false);
            }
            else {
                // Just hand over the created bo
            }
        }
    }
    else {
        LOG (_2CSTR ("Real gbm_surface_lock_front_buffer not found"));
        assert (false);
    }

    Platform::Instance ().Unlock ();

    return bo;
}

void gbm_surface_release_buffer (struct gbm_surface* surface, struct gbm_bo* bo) {
/**/    Platform::Instance () .Lock ();

//    static thread_local void (*_gbm_surface_release_buffer) (struct gbm_surface*, struct gbm_bo*) = nullptr;
/**/    static void (*_gbm_surface_release_buffer) (struct gbm_surface*, struct gbm_bo*) = nullptr;

    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("gbm_surface_release_buffer", reinterpret_cast <uintptr_t&> (_gbm_surface_release_buffer));
    }

/**/     Platform::Instance () .Unlock ();

    if (resolved != false) {
        Platform::Instance () .Lock ();

        if (Platform::Instance (). Remove (surface, bo) != true) {
#ifdef _NO_RESTART_APPLICATION
            // Error, probably, unknown surface, no known previous locked buffers or other error
            assert (false);
#endif
        }
        else {
            // Expected
        }

        // Avoid any segfault
        if (bo != nullptr) {
            LOG (_2CSTR ("Calling Real gbm_surface_release_buffer"));

            // Always release even an untracked surface
            /* void */ _gbm_surface_release_buffer (surface, bo);
        }
        else {
            // Error
            assert (false);
        }

        Platform::Instance ().Unlock ();
    }
    else {
        LOG (_2CSTR ("Real gbm_surface_release_buffer not found"));
        assert (false);
    }
}

int gbm_surface_has_free_buffers(struct gbm_surface* surface) {
/**/    Platform::Instance ().Lock ();

//    static thread_local int (*_gbm_surface_has_free_buffers) (struct gbm_surface*) = nullptr;
/**/    static int (*_gbm_surface_has_free_buffers) (struct gbm_surface*) = nullptr;

    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("gbm_surface_has_free_buffers", reinterpret_cast <uintptr_t&> (_gbm_surface_has_free_buffers));
    }

/**/    Platform::Instance ().Unlock ();

    Platform::Instance ().Lock ();

    // GBM uses only values 0 and 1; the latter indicates free buffers are available
    int ret = 0;

    if (resolved != false) {
        LOG (_2CSTR ("Calling Real gbm_surface_has_buffers"));

        ret = _gbm_surface_has_free_buffers (surface);
    }
    else {
        LOG (_2CSTR ("Real gbm_surface_has_free_buffers not found"));
        assert (false);
    }

    Platform::Instance ().Unlock ();

    return ret;
}

struct gbm_surface* gbm_surface_create (struct gbm_device* gbm, uint32_t width, uint32_t height, uint32_t format, uint32_t flags) {
/**/    Platform::Instance ().Lock ();

//    static thread_local struct gbm_surface* (*_gbm_surface_create) (struct gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t) = nullptr;
/**/    static struct gbm_surface* (*_gbm_surface_create) (struct gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t) = nullptr;

    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("gbm_surface_create", reinterpret_cast <uintptr_t&> (_gbm_surface_create));
    }

/**/    Platform::Instance ().Unlock ();

    Platform::Instance ().Lock ();

    struct gbm_surface* ret = nullptr;

    if (resolved != false) {
        LOG (_2CSTR ("Calling Real gbm_surface_create"));

        ret = _gbm_surface_create (gbm, width, height, format, flags);

        // The surface should not yet exist
        if (Platform::Instance ().Exist (ret) != false && Platform::Instance ().Remove (ret) != false) {
            assert (false);
        }

        if (Platform::Instance ().Add (gbm, ret) != true) {
            /* void */ gbm_surface_destroy (ret);

            ret = nullptr;

            assert (false);
        }
        else {
            // Just hand over the created surface (pointer)
        }
    }
    else {
        LOG (_2CSTR ("Real gbm_surface_create not found"));
        assert (false);
    }

    Platform::Instance ().Unlock ();

    return ret;
}

struct gbm_surface* gbm_surface_create_with_modifiers (struct gbm_device* gbm, uint32_t width, uint32_t height, uint32_t format, const uint64_t* modifiers, const unsigned int count) {
/**/    Platform::Instance ().Lock ();

//    static thread_local struct gbm_surface* (*_gbm_surface_create_with_modifiers) (struct gbm_device*, uint32_t, uint32_t, uint32_t, const uint64_t*, const unsigned int) = nullptr;
/**/    static struct gbm_surface* (*_gbm_surface_create_with_modifiers) (struct gbm_device*, uint32_t, uint32_t, uint32_t, const uint64_t*, const unsigned int) = nullptr;

    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("gbm_surface_create_with_modifiers", reinterpret_cast <uintptr_t&> (_gbm_surface_create_with_modifiers));
    }

/**/    Platform::Instance ().Unlock ();

    Platform::Instance ().Lock ();

    // GBM uses only values 0 and 1; the latter indicates free buffers are available
    struct gbm_surface* ret = nullptr;

    if (resolved != false) {
        LOG (_2CSTR ("Calling Real gbm_surface_create_with_modifiers"));

        ret = _gbm_surface_create_with_modifiers (gbm, width, height, format, modifiers, count);

        // The surface should not yet exist
        if (Platform::Instance ().Exist (ret) != false && Platform::Instance ().Remove (ret)) {
            assert (false);
        }

        if (Platform::Instance ().Add (gbm, ret) != true) {
            /* void */ gbm_surface_destroy (ret);

            ret = nullptr;

            assert (false);
        }
        else {
            // Just hand over the created gbm_surface
        }
    }
    else {
        LOG (_2CSTR ("Real gbm_surface_create_with_modifiers"));
        assert (false);
    }

    Platform::Instance ().Unlock ();

    return ret;
}

void gbm_surface_destroy (struct gbm_surface* surface) {
/**/    Platform::Instance ().Lock ();

//    static thread_local void (*_gbm_surface_destroy) (struct gbm_surface*) = nullptr;
/**/    static void (*_gbm_surface_destroy) (struct gbm_surface*) = nullptr;

    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("gbm_surface_destroy", reinterpret_cast <uintptr_t&> (_gbm_surface_destroy));
    }

/**/    Platform::Instance ().Unlock ();

    if (resolved != false) {
        Platform::Instance ().Lock ();

        // Remove all references
        if (Platform::Instance ().Remove (surface, nullptr) != false) {
            LOG (_2CSTR ("Calling Real gbm_surface_destroy"));
            /*void*/ _gbm_surface_destroy (surface);
        }
        else {
            // Error
            assert (false);
        }

        Platform::Instance ().Unlock ();
    }
    else {
        LOG (_2CSTR ("Real gbm_surface_destroy not found"));
        assert (false);
    }
}

void gbm_device_destroy (struct gbm_device* device) {
/**/     Platform::Instance ().Lock ();

//    static thread_local void (*_gbm_device_destroy) (struct gbm_device*) = nullptr;
/**/    static void (*_gbm_device_destroy) (struct gbm_device*) = nullptr;

    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("gbm_device_destroy", reinterpret_cast <uintptr_t&> (_gbm_device_destroy));
    }

/**/     Platform::Instance ().Unlock ();

    if (resolved != false) {
        Platform::Instance ().Lock ();

        // Remove all references
        if (Platform::Instance ().Remove (device, nullptr) != false) {
            LOG (_2CSTR ("Calling Real gbm_device_destroy"));
            /* void */ _gbm_device_destroy (device);
        }
        else {
            // Error
            assert (false);
        }

        Platform::Instance ().Unlock ();
    }
    else {
        LOG (_2CSTR ("Real gbm_device_destroy not found"));
        assert (false);
    }
}

struct gbm_device* gbm_create_device (int fd) {
/**/    Platform::Instance ().Lock ();

//    static thread_local struct gbm_device* (*_gbm_create_device) (int) = nullptr;
/**/    static struct gbm_device* (*_gbm_create_device) (int) = nullptr;

    static bool resolved = false;

    if (resolved != true) {
        resolved = lookup ("gbm_create_device", reinterpret_cast <uintptr_t&> (_gbm_create_device));
    }

/**/    Platform::Instance ().Unlock ();

    Platform::Instance ().Lock ();

    struct gbm_device* ret = nullptr;

    if (resolved != false) {
        LOG (_2CSTR ("Calling Real gbm_create_device"));

        ret = _gbm_create_device (fd);

        // The device should not yet exist
        if (Platform::Instance ().Exist (ret) != false && Platform::Instance ().Remove (ret)) {
            assert (false);
        }

        if (Platform::Instance ().Add (ret, nullptr) != true) {
            /* void */ gbm_device_destroy (ret);

            ret = nullptr;

            assert (false);
        }
        else {
            // Just hand over the created device (pointer)
        }
    }
    else {
        LOG (_2CSTR ("Real gbm_create_device not found"));
        assert (false);
    }

    Platform::Instance ().Unlock ();

    return ret;
}
