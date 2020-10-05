/*
    First attempt. Possibly wrong and / or incomplete, and may contain 'bad' code and / or coding practice.

    Intended to be become an object file linked into a shared library.

    RDK licence and copyright (may) apply.
*/

#include "common.h"

#include <iostream>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#undef _GNU_SOURCE

#ifdef __cplusplus
}
#endif

// An interposition function should be located outside this library
COMMON_PRIVATE const std::string& libraryName () {
    static std::string _libname;

    if (_libname.empty () == true) {
        Dl_info info;

        if (dladdr (reinterpret_cast <void*> (&libraryName), &info) != 0) {
            if (info.dli_fname != nullptr) {
                LOG ("Library name ", info.dli_fname);
                _libname = info.dli_fname;
            }
        }
    }

     return _libname;
}

// Lookup the symbols in the scope associated with this library
COMMON_PRIVATE bool lookup (const std::string symbol, uintptr_t& _address, bool default_scope) {
    bool ret = false;

    if (symbol.empty () != true)
    {
        // Its result might be statically allocated, hence, do not free it.
        /*char**/ dlerror();

        // Interposition requires to look outside this library
        _address = reinterpret_cast <uintptr_t> ( dlsym (default_scope ? RTLD_DEFAULT : RTLD_NEXT, symbol.data ()) );

        if (dlerror () == nullptr) {
            Dl_info info;

            if (dladdr ( reinterpret_cast <void*> (_address), &info ) != 0) {
                if (info.dli_fname != nullptr) {
                    LOG ("Library name ", info.dli_fname);

                    const std::string& _libraryName = libraryName ();

                    ret = _libraryName.empty () != true && _libraryName.compare (info.dli_fname) != 0;
                }
            }
        }
    }

    return ret;
}

COMMON_PRIVATE bool loaded(const std::string lib) {
    bool ret = false;

    if (lib.empty () != true) {
        // Its result might be statically allocated, hence, do not free it.
        /*char**/ dlerror();

        // RTLD_NOLOAD does not exist in POSIX
        void* _handle = dlopen (lib.data (), RTLD_LAZY | RTLD_LOCAL);

        if (_handle != nullptr) {
            ret = true;
            /* int */ dlclose (_handle);
        }
        else {
            LOG ("Unable to test for loaded library ", lib.data (), " with error : ", dlerror ());
        }
    }

    return ret;
}
