/* Minimal stand-in for DOSBox's dosbox.h - only what dbopl.cpp / dbopl.h use,
 * so those two files can be compiled unmodified outside DOSBox. 32-bit target. */
#ifndef VOPL3_DOSBOX_SHIM_H
#define VOPL3_DOSBOX_SHIM_H

typedef unsigned char  Bit8u;
typedef signed char    Bit8s;
typedef unsigned short Bit16u;
typedef signed short   Bit16s;
typedef unsigned int   Bit32u;
typedef signed int     Bit32s;
typedef unsigned int   Bitu;
typedef signed int     Bits;

#define INLINE          inline
#define GCC_UNLIKELY(x) (x)
#define DB_FASTCALL

static inline void LOG_MSG(const char *, ...) {}

#endif
