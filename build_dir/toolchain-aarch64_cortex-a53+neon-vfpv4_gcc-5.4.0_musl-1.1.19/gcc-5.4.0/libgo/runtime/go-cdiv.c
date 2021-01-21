/* go-cdiv.c -- complex division routines

   Copyright 2013 The Go Authors. All rights reserved.
   Use of this source code is governed by a BSD-style
   license that can be found in the LICENSE file.  */

#include <complex.h>
#include <math.h>

/* Calls to these functions are generated by the Go frontend for
   division of complex64 or complex128.  We use these because Go's
   complex division expects slightly different results from the GCC
   default.  When dividing NaN+1.0i / 0+0i, Go expects NaN+NaNi but
   GCC generates NaN+Infi.  NaN+Infi seems wrong seems the rules of
   C99 Annex G specify that if either side of a complex number is Inf,
   the the whole number is Inf, but an operation involving NaN ought
   to result in NaN, not Inf.  */

complex float
__go_complex64_div (complex float a, complex float b)
{
  if (__builtin_expect (b == 0, 0))
    {
      if (!isinf (crealf (a))
	  && !isinf (cimagf (a))
	  && (isnan (crealf (a)) || isnan (cimagf (a))))
	{
	  /* Pass "1" to nanf to match math/bits.go.  */
	  return nanf("1") + nanf("1")*I;
	}
    }
  return a / b;
}

complex double
__go_complex128_div (complex double a, complex double b)
{
  if (__builtin_expect (b == 0, 0))
    {
      if (!isinf (creal (a))
	  && !isinf (cimag (a))
	  && (isnan (creal (a)) || isnan (cimag (a))))
	{
	  /* Pass "1" to nan to match math/bits.go.  */
	  return nan("1") + nan("1")*I;
	}
    }
  return a / b;
}
