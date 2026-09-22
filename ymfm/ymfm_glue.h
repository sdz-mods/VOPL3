/* C interface to ymfm's YMF262 (OPL3) for the VOPL3 renderer - see ymfm_glue.cpp */
#ifndef YMFM_GLUE_H
#define YMFM_GLUE_H
#ifdef __cplusplus
extern "C" {
#endif

void ymfm_reset(unsigned rate);                     /* output rate, e.g. 48000 */
void ymfm_write(unsigned reg, unsigned char val);   /* reg 0x000-0x1FF */
void ymfm_generate(short *out, unsigned frames);    /* stereo interleaved */

#ifdef __cplusplus
}
#endif
#endif
