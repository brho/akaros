#include <math.h>

static inline double i2d(unsigned long long i)
{
  double d;
  asm volatile("std %2,[%1]; ldd [%1],%0" : "=f"(d) : "r"(&d),"r"(i));
  return d;
}

static inline unsigned long long d2i(double d)
{
  unsigned long long i;
  asm volatile("std %2,[%1]; ldd [%1],%0" : "=r"(i) : "r"(&i),"f"(d));
  return i;
}

double do_recip(double b)
{
  unsigned long long i = d2i(b);
  unsigned long long i2 = ((2046-((i>>52)&~0x800)) | (i>>52)&0x800) << 52;
  unsigned long long i3 = (i >> 50) & 3;
  static const double divlut[4] = {1.0,0.8,0.666,0.571};
  double x = i2d(i2)*divlut[i3];

  x = x*(2.0-b*x);
  x = x*(2.0-b*x);
  x = x*(2.0-b*x);
  x = x*(2.0-b*x);
  x = x*(2.0-b*x);

  return x;
}

double do_rsqrt(double b)
{
  unsigned long long i = d2i(b);
  unsigned long long i2 = ((3069-((i>>52)&~0x800))>>1 | (i>>52)&0x800) << 52;
  unsigned long long i3 = (i >> 50) & 7;
  double x = i2d(i2);

  static const double sqrtlut[8] = {1.4142,1.264,1.155,1.069, 1.0,0.894,0.816,0.756};
  x *= sqrtlut[i3];

  x = 0.5*x*(3.0-b*x*x);
  x = 0.5*x*(3.0-b*x*x);
  x = 0.5*x*(3.0-b*x*x);
  x = 0.5*x*(3.0-b*x*x);
  x = 0.5*x*(3.0-b*x*x);

  return x;
}

double do_fdiv(double x, double y)
{
  if((d2i(y) & 0x7FF0000000000000ULL) == 0)
    return x/y;
  return x*do_recip(y);
}

double do_fsqrt(double x)
{
  if(d2i(x) & 0x8000000000000000ULL)
    return sqrt(x);
  return x*do_rsqrt(x);
}
