/* ymfm_owcompat.h - VOPL3: lets Open Watcom C++ (no C++11) build ymfm's OPL3.
 * Included first by ymfm.h (part of ymfm-openwatcom.patch). The rest of the
 * port is in that patch; see README.md. */
#ifndef YMFM_OWCOMPAT_H
#define YMFM_OWCOMPAT_H

#include <stdint.h>     /* Watcom's <cstdint> puts the types in std:: only */
#include <stdio.h>      /* snprintf, used by ymfm's key-on logging */

#ifdef __WATCOMC__
#define constexpr const /* used for constants and simple functions only */
#define override
#define nullptr 0
#endif

#endif
