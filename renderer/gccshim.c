/* gccshim.c - the C/C++ runtime functions the GCC-compiled emulator cores
 * call.
 *
 * The cores are compiled by GCC and linked into the Watcom-built renderer
 * (see build.ps1), without GCC's own runtime: that one targets newer Windows
 * than 98. GCC's code calls these with the C (cdecl) convention, under the
 * C name with a leading underscore (C++ ones under their GCC-mangled names);
 * Watcom's library uses its register convention and other names. So each
 * shim here is built by Watcom under GCC's name and convention, and forwards
 * to Watcom's library. */
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* cdecl: arguments on the stack, removed by the caller; result in EAX, or
 * in ST(0) for a double; EAX/ECX/EDX may be changed (GCC's caller-saved
 * registers) */
#pragma aux gcc_int  "*" __parm __caller [] __value [__eax]   __modify [__eax __ecx __edx]
#pragma aux gcc_fp   "*" __parm __caller [] __value [__8087]  __modify [__eax __ecx __edx __8087]

#pragma aux (gcc_int) gcc_memset  "_memset"
#pragma aux (gcc_int) gcc_memcpy  "_memcpy"
#pragma aux (gcc_int) gcc_memmove "_memmove"
#pragma aux (gcc_int) gcc_strlen  "_strlen"
void  *gcc_memset(void *d, int c, size_t n)          { return memset(d, c, n); }
void  *gcc_memcpy(void *d, const void *s, size_t n)  { return memcpy(d, s, n); }
void  *gcc_memmove(void *d, const void *s, size_t n) { return memmove(d, s, n); }
size_t gcc_strlen(const char *s)                     { return strlen(s); }

#pragma aux (gcc_fp) gcc_sin "_sin"
#pragma aux (gcc_fp) gcc_pow "_pow"
double gcc_sin(double x)           { return sin(x); }
double gcc_pow(double x, double y) { return pow(x, y); }

/* C++ operator new / delete (GCC's mangled names, 32-bit size_t) */
#pragma aux (gcc_int) gcc_new         "__Znwj"      /* new(size_t)          */
#pragma aux (gcc_int) gcc_new_array   "__Znaj"      /* new[](size_t)        */
#pragma aux (gcc_int) gcc_delete      "__ZdlPv"     /* delete(void *)       */
#pragma aux (gcc_int) gcc_delete_sz   "__ZdlPvj"    /* delete(void *, size) */
#pragma aux (gcc_int) gcc_delete_arr  "__ZdaPv"     /* delete[](void *)     */
void *gcc_new(size_t n)                 { return malloc(n ? n : 1); }
void *gcc_new_array(size_t n)           { return malloc(n ? n : 1); }
void  gcc_delete(void *p)               { free(p); }
void  gcc_delete_sz(void *p, size_t n)  { (void)n; free(p); }
void  gcc_delete_arr(void *p)           { free(p); }

/* ___chkstk_ms: GCC calls it before setting up a stack frame of 4 KB or
 * more, with the frame size in EAX, and expects every register preserved.
 * It touches the stack once per 4 KB page, top down, so each page's guard
 * page is hit in order and Windows commits the stack as it grows (skipping
 * a page would fault). Same algorithm as libgcc's. */
void __declspec(naked) gcc_chkstk_ms(void);
#pragma aux gcc_chkstk_ms "___chkstk_ms"
void __declspec(naked) gcc_chkstk_ms(void)
{
    _asm {
        push ecx
        push eax
        lea  ecx, [esp + 12]        ; the caller's stack pointer
        cmp  eax, 1000h
        jb   last
    next:
        sub  ecx, 1000h
        or   dword ptr [ecx], 0
        sub  eax, 1000h
        cmp  eax, 1000h
        ja   next
    last:
        sub  ecx, eax
        or   dword ptr [ecx], 0
        pop  eax
        pop  ecx
        ret
    }
}
