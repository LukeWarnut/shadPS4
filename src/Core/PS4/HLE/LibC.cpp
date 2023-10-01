#include "LibC.h"
#include "Libs.h"
#include "../Loader/Elf.h"
#include <debug.h>
#include <pthread.h>

namespace HLE::Libs::LibC {

	static u32 g_need_sceLibc = 1;

	static PS4_SYSV_ABI void init_env()  // every game/demo should probably 
	{
		//dummy no need atm
	}

    static pthread_mutex_t __guard_mutex;
    static pthread_once_t __once_control = PTHREAD_ONCE_INIT;

    static void recursiveMutex() 
    {
        pthread_mutexattr_t recursiveMutexAttr;
        pthread_mutexattr_init(&recursiveMutexAttr);
        pthread_mutexattr_settype(&recursiveMutexAttr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&__guard_mutex, &recursiveMutexAttr);
    }

    static pthread_mutex_t* mutex_quard() {
        pthread_once(&__once_control, &recursiveMutex);
        return &__guard_mutex;
    }

	int PS4_SYSV_ABI __cxa_guard_acquire(u64* guard_object) {
        if ((*((uint8_t*)guard_object) != 0))  // low 8 bits checks if its already init
        {
            return 0;
        }
        int result = ::pthread_mutex_lock(mutex_quard());
        if (result != 0) {
            BREAKPOINT();
        }

        // Check if another thread has completed initializer run
        if ((*((uint8_t*)guard_object) != 0)) {  // check again if other thread init it
            int result = ::pthread_mutex_unlock(mutex_quard());
            if (result != 0) {
                BREAKPOINT();
            }
            return 0;
        }

        if (((uint8_t*)guard_object)[1] != 0) { // the second lowest byte marks if it's being used by a thread
            BREAKPOINT();
        }

        // mark this guard object as being in use
        ((uint8_t*)guard_object)[1] = 1;

        return 1;
    }

	void PS4_SYSV_ABI __cxa_guard_release(u64* guard_object) 
	{ 
		*((uint8_t*)guard_object) = 1;//mark it as done

        // release global mutex
        int result = ::pthread_mutex_unlock(mutex_quard());
        if (result != 0) {
            BREAKPOINT();
        }
	}

	int PS4_SYSV_ABI memcmp(const void* s1, const void* s2, size_t n) {
        return ::memcmp(s1, s2, n);
    }

    void* PS4_SYSV_ABI memcpy(void* dest, const void* src, size_t n) {
        return ::memcpy(dest, src, n);
    }

	static PS4_SYSV_ABI void catchReturnFromMain(int status) { BREAKPOINT();
	}
    static PS4_SYSV_ABI void exit(int code) { BREAKPOINT();
	}
    static PS4_SYSV_ABI int atexit(void (*func)())
	{ 
		int rt = ::atexit(func);
		if (rt != 0)
		{
            BREAKPOINT();
		}
        return rt;
	}
    static PS4_SYSV_ABI void _Assert() { BREAKPOINT(); }

    static PS4_SYSV_ABI int printf() { /*
        BREAKPOINT();*/
        //PRINT_DUMMY_FUNCTION_NAME();
        return 0;//not correct
    }

	void LibC_Register(SymbolsResolver* sym)
	{
        LIB_FUNCTION("bzQExy189ZI", "libc", 1, "libc", 1, 1, init_env);
        LIB_FUNCTION("3GPpjQdAMTw", "libc", 1, "libc", 1, 1, __cxa_guard_acquire);
        LIB_FUNCTION("9rAeANT2tyE", "libc", 1, "libc", 1, 1, __cxa_guard_release);
        LIB_FUNCTION("DfivPArhucg", "libc", 1, "libc", 1, 1, memcmp);
        LIB_FUNCTION("Q3VBxCXhUHs", "libc", 1, "libc", 1, 1, memcpy);
        LIB_FUNCTION("XKRegsFpEpk", "libc", 1, "libc", 1, 1, catchReturnFromMain);
        LIB_FUNCTION("uMei1W9uyNo", "libc", 1, "libc", 1, 1, exit);
        LIB_FUNCTION("8G2LB+A3rzg", "libc", 1, "libc", 1, 1, atexit);
        LIB_FUNCTION("-QgqOT5u2Vk", "libc", 1, "libc", 1, 1, _Assert);
        LIB_FUNCTION("hcuQgD53UxM", "libc", 1, "libc", 1, 1, printf);

		LIB_OBJ("P330P3dFF68", "libc", 1, "libc", 1, 1, &HLE::Libs::LibC::g_need_sceLibc);
    }
    
};