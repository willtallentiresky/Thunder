/*
    First attempt. Possibly wrong and / or incomplete, and may contain 'bad' code and / or coding practice.

    RDK licence and copyright (may) apply.
*/

#pragma once

#ifdef NDEBUG
    #error This library requires assert to be available
#else
    #include <cassert>
#endif

#include <string>

#define COMMON_PRIVATE __attribute__ (( visibility ("hidden") ))
#define COMMON_PUBLIC __attribute__ (( visibility ("default") ))

#include <iostream>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#if !defined(LOG_PREFIX)
    #define LOG(...) _LOG(__VA_ARGS__)
#else
    #define _UNKNOWN_ 1
    #define _RELEASE_ 2
    #define _DEBUG_ 3

    // At least define a LOG
    #define LOG(...)

    #if (LOG_PREFIX==_RELEASE_)
        #undef LOG
        #define LOG(...) _LOG(__VA_ARGS__)
    #endif

    #if (LOG_PREFIX==_DEBUG_)
        #undef LOG
        #define LOG(...) _LOG(__FILE__, __LINE__, __VA_ARGS__)
    #endif

    #undef _UNKNOWN_
    #undef _RELEASE_
    #undef _DEBUG_
#endif

// Force internal linkage
namespace {

// Termination condition
void _LOG () {
    // Avoid the static initialization order fiasco by ensuring the (default) standard stream objects are constructed before their first use
    static std::ios_base::Init _base;

    // Unbuffered!
    std::cout << std::endl;
}

template <typename Head, typename... Tail>
void _LOG (const Head& head, const Tail&... tail) {
    // Unbuffered!
    std::cout << head;

    _LOG (tail...);
}

} // Anonymous namespace

template <typename T>
class Singleton {

    public :

        Singleton (const Singleton&) = delete;
        Singleton (const Singleton&&) = delete;

        Singleton& operator= (const Singleton&) = delete;
        Singleton& operator= (Singleton&&) = delete;

        // Each shared object should have its own instance
        COMMON_PRIVATE static T& Instance ()
        {
            static T _instance;
            return _instance;
        }

    protected :

        COMMON_PRIVATE Singleton () = default;
        COMMON_PRIVATE virtual ~Singleton () = default;

    private :

        // Nothing
};

COMMON_PRIVATE bool lookup (const std::string symbol, uintptr_t& _address, bool default_scope = false);
COMMON_PRIVATE bool loaded(const std::string lib);
