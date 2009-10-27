#ifndef _SOFTFLOAT_H
#define _SOFTFLOAT_H

#ifdef __cplusplus
  extern "C" {
#endif

/*============================================================================

This C header file is part of the SoftFloat IEC/IEEE Floating-point Arithmetic
Package, Release 2b.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://www.cs.berkeley.edu/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.

=============================================================================*/

/*----------------------------------------------------------------------------
| The macro `FLOATX80' must be defined to enable the extended double-precision
| floating-point format `floatx80'.  If this macro is not defined, the
| `floatx80' type will not be defined, and none of the functions that either
| input or output the `floatx80' type will be defined.  The same applies to
| the `FLOAT128' macro and the quadruple-precision format `float128'.
*----------------------------------------------------------------------------*/
#define FLOATX80
#define FLOAT128

#include <arch/types.h>

/* asw */
typedef uint8_t flag;
typedef uint8_t bits8;
typedef int8_t sbits8;
typedef uint16_t bits16;
typedef int16_t sbits16;
typedef uint32_t bits32;
typedef int32_t sbits32;
typedef uint64_t bits64;
typedef int64_t sbits64;

#define INLINE
#define LIT64( a ) a##LL

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point types.
*----------------------------------------------------------------------------*/
typedef unsigned int float32;
typedef unsigned long long float64;
#ifdef FLOATX80
typedef struct {
    unsigned short high;
    unsigned long long low;
} floatx80;
#endif
#ifdef FLOAT128
typedef struct {
    unsigned long long high, low;
} float128;
#endif

/*----------------------------------------------------------------------------
| Internal canonical NaN format.
*----------------------------------------------------------------------------*/
typedef struct {
    flag sign;
    bits64 high, low;
} commonNaNT;

INLINE bits32 extractFloat32Frac( float32 a );
INLINE int16_t extractFloat32Exp( float32 a );
INLINE flag extractFloat32Sign( float32 a );
INLINE float32 packFloat32( flag zSign, int16_t zExp, bits32 zSig );
INLINE bits64 extractFloat64Frac( float64 a );
INLINE int16_t extractFloat64Exp( float64 a );
INLINE flag extractFloat64Sign( float64 a );
INLINE float64 packFloat64( flag zSign, int16_t zExp, bits64 zSig );
INLINE bits64 extractFloatx80Frac( floatx80 a );
INLINE int32_t extractFloatx80Exp( floatx80 a );
INLINE flag extractFloatx80Sign( floatx80 a );
INLINE floatx80 packFloatx80( flag zSign, int32_t zExp, bits64 zSig );
INLINE bits64 extractFloat128Frac1( float128 a );
INLINE bits64 extractFloat128Frac0( float128 a );
INLINE int32_t extractFloat128Exp( float128 a );
INLINE flag extractFloat128Sign( float128 a );
INLINE float128 packFloat128( flag zSign, int32_t zExp, bits64 zSig0, bits64 zSig1 );

typedef struct
{
  int8_t float_detect_tininess;
  int8_t float_rounding_mode;
  int8_t float_exception_flags;
  #ifdef FLOATX80
    int floatx80_rounding_precision;
  #endif
} softfloat_t;

float32 subFloat32Sigs( softfloat_t* sf, float32 a, float32 b, flag zSign );
float64 subFloat64Sigs( softfloat_t* sf, float64 a, float64 b, flag zSign );
floatx80 subFloatx80Sigs( softfloat_t* sf, floatx80 a, floatx80 b, flag zSign );
float128 subFloat128Sigs( softfloat_t* sf, float128 a, float128 b, flag zSign );
float32 addFloat32Sigs( softfloat_t* sf, float32 a, float32 b, flag zSign );
float64 addFloat64Sigs( softfloat_t* sf, float64 a, float64 b, flag zSign );
floatx80 addFloatx80Sigs( softfloat_t* sf, floatx80 a, floatx80 b, flag zSign );
float128 addFloat128Sigs( softfloat_t* sf, float128 a, float128 b, flag zSign );
float32 normalizeRoundAndPackFloat32( softfloat_t* sf, flag zSign, int16_t zExp, bits32 zSig );
float64 normalizeRoundAndPackFloat64( softfloat_t* sf, flag zSign, int16_t zExp, bits64 zSig );
floatx80 normalizeRoundAndPackFloatx80( softfloat_t* sf,
     int8_t roundingPrecision, flag zSign, int32_t zExp, bits64 zSig0, bits64 zSig1);
float128 normalizeRoundAndPackFloat128( softfloat_t* sf,
     flag zSign, int32_t zExp, bits64 zSig0, bits64 zSig1 );
int32_t roundAndPackInt32( softfloat_t* sf, flag zSign, bits64 absZ );
int64_t roundAndPackInt64( softfloat_t* sf, flag zSign, bits64 absZ0, bits64 absZ1 );
float32 roundAndPackFloat32( softfloat_t* sf, flag zSign, int16_t zExp, bits32 zSig );
float64 roundAndPackFloat64( softfloat_t* sf, flag zSign, int16_t zExp, bits64 zSig );
floatx80 roundAndPackFloatx80( softfloat_t* sf,
     int8_t roundingPrecision, flag zSign, int32_t zExp, bits64 zSig0, bits64 zSig1);
float128 roundAndPackFloat128( softfloat_t* sf,
     flag zSign, int32_t zExp, bits64 zSig0, bits64 zSig1, bits64 zSig2 );
void normalizeFloat32Subnormal( bits32 aSig, int16_t *zExpPtr, bits32 *zSigPtr );
void normalizeFloat64Subnormal( bits64 aSig, int16_t *zExpPtr, bits64 *zSigPtr );
void normalizeFloatx80Subnormal( bits64 aSig, int32_t *zExpPtr, bits64 *zSigPtr );
void normalizeFloat128Subnormal(
     bits64 aSig0,
     bits64 aSig1,
     int32_t *zExpPtr,
     bits64 *zSig0Ptr,
     bits64 *zSig1Ptr
 );

INLINE flag float32_is_nan( softfloat_t* sf, float32 a );
commonNaNT float32ToCommonNaN( softfloat_t* sf, float32 a );
float32 commonNaNToFloat32( softfloat_t* sf, commonNaNT a );
float32 propagateFloat32NaN( softfloat_t* sf, float32 a, float32 b );
flag float64_is_nan( softfloat_t* sf, float64 a );
commonNaNT float64ToCommonNaN( softfloat_t* sf, float64 a );
float64 commonNaNToFloat64( softfloat_t* sf, commonNaNT a );
float64 propagateFloat64NaN( softfloat_t* sf, float64 a, float64 b );
flag floatx80_is_nan( softfloat_t* sf, floatx80 a );
commonNaNT floatx80ToCommonNaN( softfloat_t* sf, floatx80 a );
floatx80 commonNaNToFloatx80( softfloat_t* sf, commonNaNT a );
floatx80 propagateFloatx80NaN( softfloat_t* sf, floatx80 a, floatx80 b );
flag float128_is_nan( softfloat_t* sf, float128 a );
commonNaNT float128ToCommonNaN( softfloat_t* sf, float128 a );
float128 commonNaNToFloat128( softfloat_t* sf, commonNaNT a );
float128 propagateFloat128NaN( softfloat_t* sf, float128 a, float128 b );

/*----------------------------------------------------------------------------
| Routine to raise any or all of the software IEC/IEEE floating-point
| exception flags.
*----------------------------------------------------------------------------*/
INLINE void float_raise( softfloat_t* sf, int );

/*----------------------------------------------------------------------------
| Software IEC/IEEE integer-to-floating-point conversion routines.
*----------------------------------------------------------------------------*/
float32 int32_to_float32( softfloat_t* sf, int );
float64 int32_to_float64( softfloat_t* sf, int );
#ifdef FLOATX80
floatx80 int32_to_floatx80( softfloat_t* sf, int );
#endif
#ifdef FLOAT128
float128 int32_to_float128( softfloat_t* sf, int );
#endif
float32 int64_to_float32( softfloat_t* sf, long long );
float64 int64_to_float64( softfloat_t* sf, long long );
#ifdef FLOATX80
floatx80 int64_to_floatx80( softfloat_t* sf, long long );
#endif
#ifdef FLOAT128
float128 int64_to_float128( softfloat_t* sf, long long );
#endif

/*----------------------------------------------------------------------------
| Software IEC/IEEE single-precision conversion routines.
*----------------------------------------------------------------------------*/
int float32_to_int32( softfloat_t* sf, float32 );
int float32_to_int32_round_to_zero( softfloat_t* sf, float32 );
long long float32_to_int64( softfloat_t* sf, float32 );
long long float32_to_int64_round_to_zero( softfloat_t* sf, float32 );
float64 float32_to_float64( softfloat_t* sf, float32 );
#ifdef FLOATX80
floatx80 float32_to_floatx80( softfloat_t* sf, float32 );
#endif
#ifdef FLOAT128
float128 float32_to_float128( softfloat_t* sf, float32 );
#endif

/*----------------------------------------------------------------------------
| Software IEC/IEEE single-precision operations.
*----------------------------------------------------------------------------*/
float32 float32_round_to_int( softfloat_t* sf, float32 );
float32 float32_add( softfloat_t* sf, float32, float32 );
float32 float32_sub( softfloat_t* sf, float32, float32 );
float32 float32_mul( softfloat_t* sf, float32, float32 );
float32 float32_div( softfloat_t* sf, float32, float32 );
float32 float32_rem( softfloat_t* sf, float32, float32 );
float32 float32_sqrt( softfloat_t* sf, float32 );
flag float32_eq( softfloat_t* sf, float32, float32 );
flag float32_le( softfloat_t* sf, float32, float32 );
flag float32_lt( softfloat_t* sf, float32, float32 );
flag float32_eq_signaling( softfloat_t* sf, float32, float32 );
flag float32_le_quiet( softfloat_t* sf, float32, float32 );
flag float32_lt_quiet( softfloat_t* sf, float32, float32 );
flag float32_is_signaling_nan( softfloat_t* sf, float32 );

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision conversion routines.
*----------------------------------------------------------------------------*/
int float64_to_int32( softfloat_t* sf, float64 );
int float64_to_int32_round_to_zero( softfloat_t* sf, float64 );
long long float64_to_int64( softfloat_t* sf, float64 );
long long float64_to_int64_round_to_zero( softfloat_t* sf, float64 );
float32 float64_to_float32( softfloat_t* sf, float64 );
#ifdef FLOATX80
floatx80 float64_to_floatx80( softfloat_t* sf, float64 );
#endif
#ifdef FLOAT128
float128 float64_to_float128( softfloat_t* sf, float64 );
#endif

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision operations.
*----------------------------------------------------------------------------*/
float64 float64_round_to_int( softfloat_t* sf, float64 );
float64 float64_add( softfloat_t* sf, float64, float64 );
float64 float64_sub( softfloat_t* sf, float64, float64 );
float64 float64_mul( softfloat_t* sf, float64, float64 );
float64 float64_div( softfloat_t* sf, float64, float64 );
float64 float64_rem( softfloat_t* sf, float64, float64 );
float64 float64_sqrt( softfloat_t* sf, float64 );
flag float64_eq( softfloat_t* sf, float64, float64 );
flag float64_le( softfloat_t* sf, float64, float64 );
flag float64_lt( softfloat_t* sf, float64, float64 );
flag float64_eq_signaling( softfloat_t* sf, float64, float64 );
flag float64_le_quiet( softfloat_t* sf, float64, float64 );
flag float64_lt_quiet( softfloat_t* sf, float64, float64 );
flag float64_is_signaling_nan( softfloat_t* sf, float64 );

#ifdef FLOATX80

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision conversion routines.
*----------------------------------------------------------------------------*/
int floatx80_to_int32( softfloat_t* sf, floatx80 );
int floatx80_to_int32_round_to_zero( softfloat_t* sf, floatx80 );
long long floatx80_to_int64( softfloat_t* sf, floatx80 );
long long floatx80_to_int64_round_to_zero( softfloat_t* sf, floatx80 );
float32 floatx80_to_float32( softfloat_t* sf, floatx80 );
float64 floatx80_to_float64( softfloat_t* sf, floatx80 );
#ifdef FLOAT128
float128 floatx80_to_float128( softfloat_t* sf, floatx80 );
#endif

#endif

#ifdef FLOATX80
/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision operations.
*----------------------------------------------------------------------------*/
floatx80 floatx80_round_to_int( softfloat_t* sf, floatx80 );
floatx80 floatx80_add( softfloat_t* sf, floatx80, floatx80 );
floatx80 floatx80_sub( softfloat_t* sf, floatx80, floatx80 );
floatx80 floatx80_mul( softfloat_t* sf, floatx80, floatx80 );
floatx80 floatx80_div( softfloat_t* sf, floatx80, floatx80 );
floatx80 floatx80_rem( softfloat_t* sf, floatx80, floatx80 );
floatx80 floatx80_sqrt( softfloat_t* sf, floatx80 );
flag floatx80_eq( softfloat_t* sf, floatx80, floatx80 );
flag floatx80_le( softfloat_t* sf, floatx80, floatx80 );
flag floatx80_lt( softfloat_t* sf, floatx80, floatx80 );
flag floatx80_eq_signaling( softfloat_t* sf, floatx80, floatx80 );
flag floatx80_le_quiet( softfloat_t* sf, floatx80, floatx80 );
flag floatx80_lt_quiet( softfloat_t* sf, floatx80, floatx80 );
flag floatx80_is_signaling_nan( softfloat_t* sf, floatx80 );
#endif

#ifdef FLOAT128

/*----------------------------------------------------------------------------
| Software IEC/IEEE quadruple-precision conversion routines.
*----------------------------------------------------------------------------*/
int float128_to_int32( softfloat_t* sf, float128 );
int float128_to_int32_round_to_zero( softfloat_t* sf, float128 );
long long float128_to_int64( softfloat_t* sf, float128 );
long long float128_to_int64_round_to_zero( softfloat_t* sf, float128 );
float32 float128_to_float32( softfloat_t* sf, float128 );
float64 float128_to_float64( softfloat_t* sf, float128 );
#ifdef FLOATX80
floatx80 float128_to_floatx80( softfloat_t* sf, float128 );
#endif

/*----------------------------------------------------------------------------
| Software IEC/IEEE quadruple-precision operations.
*----------------------------------------------------------------------------*/
float128 float128_round_to_int( softfloat_t* sf, float128 );
float128 float128_add( softfloat_t* sf, float128, float128 );
float128 float128_sub( softfloat_t* sf, float128, float128 );
float128 float128_mul( softfloat_t* sf, float128, float128 );
float128 float128_div( softfloat_t* sf, float128, float128 );
float128 float128_rem( softfloat_t* sf, float128, float128 );
float128 float128_sqrt( softfloat_t* sf, float128 );
flag float128_eq( softfloat_t* sf, float128, float128 );
flag float128_le( softfloat_t* sf, float128, float128 );
flag float128_lt( softfloat_t* sf, float128, float128 );
flag float128_eq_signaling( softfloat_t* sf, float128, float128 );
flag float128_le_quiet( softfloat_t* sf, float128, float128 );
flag float128_lt_quiet( softfloat_t* sf, float128, float128 );
flag float128_is_signaling_nan( softfloat_t* sf, float128 );

#endif

void softfloat_init(softfloat_t* sf);

enum {
    float_tininess_after_rounding  = 0,
    float_tininess_before_rounding = 1
};

enum {
    float_round_nearest_even = 0,
    float_round_to_zero      = 1,
    float_round_up           = 2,
    float_round_down         = 3
};

enum {
    float_flag_inexact   =  1,
    float_flag_divbyzero =  2,
    float_flag_underflow =  4,
    float_flag_overflow  =  8,
    float_flag_invalid   = 16
};

#ifdef __cplusplus
 }
#endif

#endif
