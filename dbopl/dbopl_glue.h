/* C interface to DOSBox's DBOPL (see dbopl_glue.cpp) */
#ifndef DBOPL_GLUE_H
#define DBOPL_GLUE_H
#ifdef __cplusplus
extern "C" {
#endif
void dbopl_reset(unsigned rate);
void dbopl_write(unsigned reg, unsigned char val);
void dbopl_generate(short *out, unsigned frames);   /* interleaved stereo int16 */
#ifdef __cplusplus
}
#endif
#endif
