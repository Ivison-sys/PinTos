#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

// FUNÇÃO PARA TODAS AS OPERAÇÕES QUE ENVOLVE PONTO FIXO 17.14

typedef int fixed_point_t;

#define F (1 << 14)

/* Converter inteiro → fixed-point */
static inline fixed_point_t IntToFp(int n) {
    return n * F;
}

// FP to int truncado
static inline int FpToIntZero(fixed_point_t x) {
    return x / F;
}

// FP to int (Arredodamento)
static inline int FpToIntArredodamento(fixed_point_t x) {
    if (x >= 0)
        return (x + F / 2) / F;
    else
        return (x - F / 2) / F;
}

// FP  + FP
static inline fixed_point_t FpAdd(fixed_point_t x, fixed_point_t y) {
    return x + y;
}

// FP - FP 
static inline fixed_point_t FpSub(fixed_point_t x, fixed_point_t y) {
    return x - y;
}

// FP + FP
static inline fixed_point_t FpAddInt(fixed_point_t x, int n) {
    return x + n * F;
}

// FP - FP
static inline fixed_point_t FpSubInt(fixed_point_t x, int n) {
    return x - n * F;
}

// FP * FP
static inline fixed_point_t FpMul(fixed_point_t x, fixed_point_t y) {
    return (fixed_point_t)((int64_t)x * y / F);
}

// FP × INT 
static inline fixed_point_t FpMulInt(fixed_point_t x, int n) {
    return x * n;
}

// FP / FP
static inline fixed_point_t FpDiv(fixed_point_t x, fixed_point_t y) {
    return (fixed_point_t)((int64_t)x * F / y);
}

// fp / inteiro 
static inline fixed_point_t FpDivInt(fixed_point_t x, int n) {
    return x / n;
}

#endif 
