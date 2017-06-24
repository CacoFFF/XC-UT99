/*=============================================================================
	Atomics.h:
	Simple 32-bit atomic methods for multithreading.
	Based on Unreal Engine 4 atomics.
=============================================================================*/

struct FGenericPlatformAtomics
{
	static FORCEINLINE bool CanUseCompareExchange128()
	{
		return false;
	}

protected:
	/**
	 * Checks if a pointer is aligned and can be used with atomic functions.
	 *
	 * @param Ptr - The pointer to check.
	 *
	 * @return true if the pointer is aligned, false otherwise.
	 */
	static inline bool IsAligned( const volatile void* Ptr, const DWORD Alignment = sizeof(void*) )
	{
		return !( (INT)Ptr & (Alignment - 1));
	}
};

//64 and 128 bit atomics removed

#ifdef __LINUX_X86__

//GCC 2.95 doesn't have these atomic functions, so i must define them
inline INT __sync_fetch_and_add(volatile INT* p, INT incr)
{
	INT result;
	__asm__ __volatile__ ("lock; xadd %0, %1" :
			"=r"(result), "=m"(*p):
			"0"(incr), "m"(*p) :
			"memory");
	return result;
}

inline INT __sync_fetch_and_sub( volatile INT* p, INT decr)
{
    INT result;
    __asm__ __volatile__ ("lock; xadd %0, %1"
            :"=r"(result), "=m"(*p)
            :"0"(-decr), "m"(*p)
            :"memory");
    return result;
}

inline INT __sync_lock_test_and_set( volatile INT* p, INT exch)
{
    __asm__ __volatile__ ("lock; xchgl %0, %1"
                 : "=r"(exch), "=m"(*p)
                 : "0"(exch), "m"(*p)
                 : "memory");
    return exch;
}

inline INT __sync_val_compare_and_swap(volatile INT* p, INT old, INT _new)
{
    INT prev;
    __asm__ __volatile__ ("lock;"
                 "cmpxchgl %1, %2;"
                 : "=a"(prev)
                 : "q"(_new), "m"(*p), "a"(old)
                 : "memory");
    return prev;
}

struct XC_CORE_API FLinuxPlatformAtomics : public FGenericPlatformAtomics
{
	static FORCEINLINE INT InterlockedIncrement( volatile INT* Value )
	{
		return __sync_fetch_and_add(Value, 1) + 1;
	}
	static FORCEINLINE INT InterlockedDecrement( volatile INT* Value )
	{
		return __sync_fetch_and_sub(Value, 1) - 1;
	}
	static FORCEINLINE INT InterlockedAdd( volatile INT* Value, INT Amount )
	{
		return __sync_fetch_and_add(Value, Amount);
	}
	static FORCEINLINE INT InterlockedExchange( volatile INT* Value, INT Exchange )
	{
		return __sync_lock_test_and_set(Value,Exchange);	
	}
	static FORCEINLINE void* InterlockedExchangePtr( volatile void** Dest, void* Exchange )
	{
		return (void*)__sync_lock_test_and_set( (volatile INT*)Dest, (INT)Exchange);
	}
	static FORCEINLINE INT InterlockedCompareExchange( volatile INT* Dest, INT Exchange, INT Comparand )
	{
		return __sync_val_compare_and_swap(Dest, Comparand, Exchange);
	}
};
typedef FLinuxPlatformAtomics FPlatformAtomics;
#endif


#ifdef _MSC_VER
struct XC_CORE_API FWindowsPlatformAtomics : public FGenericPlatformAtomics
{
	static FORCEINLINE INT InterlockedIncrement( volatile INT* Value )
	{
		return (INT)::InterlockedIncrement((LPLONG)Value);
	}
	static FORCEINLINE INT InterlockedDecrement( volatile INT* Value )
	{
		return (INT)::InterlockedDecrement((LPLONG)Value);
	}
	static FORCEINLINE INT InterlockedAdd( volatile INT* Value, INT Amount )
	{
		return (INT)::InterlockedExchangeAdd((LPLONG)Value, (LONG)Amount);
	}
	static FORCEINLINE INT InterlockedExchange( volatile INT* Value, INT Exchange )
	{
		return (INT)::InterlockedExchange((LPLONG)Value, (LONG)Exchange);
	}
	static FORCEINLINE void* InterlockedExchangePtr( volatile void** Dest, void* Exchange )
	{
		return (void*)::InterlockedExchange((PLONG)(Dest), (LONG)(Exchange));
	}

	static FORCEINLINE INT InterlockedCompareExchange( volatile INT* Dest, INT Exchange, INT Comparand )
	{
#if _MSC_VER < 1250
		return (INT)::InterlockedCompareExchange((PVOID*)Dest, (PVOID)Exchange, (PVOID)Comparand); //VC++6
#else
		return (INT)::InterlockedCompareExchange((LPLONG)Dest, (LONG)Exchange, (LONG)Comparand);
#endif
	}

protected:
};
typedef FWindowsPlatformAtomics FPlatformAtomics;
#endif