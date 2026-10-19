#ifndef _NVX_MATH_H
#define _NVX_MATH_H
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define M_PI 3.14159265358979323846
double floor(double); double ceil(double); double fabs(double); double fmod(double,double);
double trunc(double); double round(double); double sqrt(double); double pow(double,double);
double exp(double); double log(double); double log2(double); double log10(double);
double sin(double); double cos(double); double tan(double);
double asin(double); double acos(double); double atan(double); double atan2(double,double);
double sinh(double); double cosh(double); double tanh(double);
double frexp(double,int*); double ldexp(double,int); double modf(double,double*);
double fmax(double,double); double fmin(double,double);
int isnan_(double); int isinf_(double);
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define HUGE_VALF (__builtin_huge_valf())
double copysign(double,double);
#endif
