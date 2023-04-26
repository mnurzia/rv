#ifndef RT_H
#define RT_H

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u32;
typedef signed int s32;

#define RBASE ((u32 *)(0xC0000000))
#define R_VBASE (RBASE + 0)
#define R_VMODE (RBASE + 1)

void rt_sw(u32 *addr, u32 v);
u32 rt_lw(u32 *addr);

u32 rt_cpu(void);

void rt_trap(void);
void rt_spin(void);

#endif
