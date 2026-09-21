/* Minimal stand-in for DOSBox's adlib.h: dbopl.h declares a Handler that plugs
 * the chip into DOSBox's mixer. VOPL3 drives DBOPL::Chip directly and never
 * uses that Handler, but it must still compile. */
#ifndef VOPL3_ADLIB_SHIM_H
#define VOPL3_ADLIB_SHIM_H

#include "dosbox.h"

class MixerChannel {
public:
    void AddSamples_m32(Bitu, const Bit32s *) {}
    void AddSamples_s32(Bitu, const Bit32s *) {}
};

namespace Adlib {
class Handler {
public:
    virtual Bit32u WriteAddr(Bit32u port, Bit8u val) = 0;
    virtual void   WriteReg(Bit32u addr, Bit8u val) = 0;
    virtual void   Generate(MixerChannel *chan, Bitu samples) = 0;
    virtual void   Init(Bitu rate) = 0;
    virtual ~Handler() {}
};
}

#endif
