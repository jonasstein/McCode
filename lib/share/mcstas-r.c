/*******************************************************************************
*
* McStas, neutron ray-tracing package
*         Copyright 1997-2002, All rights reserved
*         Risoe National Laboratory, Roskilde, Denmark
*         Institut Laue Langevin, Grenoble, France
*
* Runtime: share/mcstas-r.c
*
* %Identification
* Written by: KN
* Date:    Aug 29, 1997
* Release: McStas 1.6
* Version: 1.7
*
* Runtime system for McStas.
* Embedded within instrument in runtime mode.
*
* Usage: Automatically embbeded in the c code whenever required.
*
* $Id: mcstas-r.c,v 1.50 2003-01-23 10:57:50 pkwi Exp $
*
* $Log: not supported by cvs2svn $
* Revision 1.7 2002/10/19 22:46:21 ef
*        gravitation for all with -g. Various output formats.
*
* Revision 1.6 2002/09/17 12:01:21 ef
*        changed randvec_target_sphere to circle
* added randvec_target_rect
*
* Revision 1.5 2002/09/03 19:48:01 ef
*        corrected randvec_target_sphere. created target_rect.
*
* Revision 1.4 2002/09/02 18:59:05 ef
*        moved adapt_tree functions to independent lib. Updated sighandler.
*
* Revision 1.3 2002/08/28 11:36:37 ef
*        Changed to lib/share/c code
*
* Revision 1.2 2001/10/10 11:36:37 ef
*        added signal handler
*
* Revision 1.1 1998/08/29 11:36:37 kn
*        Initial revision
*
*******************************************************************************/

#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <time.h>

#ifndef MCSTAS_R_H
#include "mcstas-r.h"
#endif


#ifdef MC_ANCIENT_COMPATIBILITY
int mctraceenabled = 0;
int mcdefaultmain  = 0;
#endif

static long mcseed      = 0;
mcstatic int mcdotrace  = 0;
static int mcascii_only = 0;
static int mcdisable_output_files = 0;
static int mcsingle_file= 0;
mcstatic int mcgravitation = 0;
static long mcstartdate = 0;

static FILE *mcsiminfo_file = NULL;
static char *mcdirname = NULL;
static char *mcsiminfo_name= "mcstas";
static char  mcsig_message[256];  /* ADD: E. Farhi, Sep 20th 2001 */

/* MCDISPLAY support. ======================================================= */

void mcdis_magnify(char *what){
  printf("MCDISPLAY: magnify('%s')\n", what);
}

void mcdis_line(double x1, double y1, double z1,
                double x2, double y2, double z2){
  printf("MCDISPLAY: multiline(2,%g,%g,%g,%g,%g,%g)\n",
         x1,y1,z1,x2,y2,z2);
}

void mcdis_multiline(int count, ...){
  va_list ap;
  double x,y,z;

  printf("MCDISPLAY: multiline(%d", count);
  va_start(ap, count);
  while(count--)
  {
    x = va_arg(ap, double);
    y = va_arg(ap, double);
    z = va_arg(ap, double);
    printf(",%g,%g,%g", x, y, z);
  }
  va_end(ap);
  printf(")\n");
}

void mcdis_circle(char *plane, double x, double y, double z, double r){
  printf("MCDISPLAY: circle('%s',%g,%g,%g,%g)\n", plane, x, y, z, r);
}

/* coordinates handling ===================================================== */

/*******************************************************************************
* Since we use a lot of geometric calculations using Cartesian coordinates,
* we collect some useful routines here. However, it is also permissible to
* work directly on the underlying struct coords whenever that is most
* convenient (that is, the type Coords is not abstract).
*
* Coordinates are also used to store rotation angles around x/y/z axis.
*
* Since coordinates are used much like a basic type (such as double), the
* structure itself is passed and returned, rather than a pointer.
*
* At compile-time, the values of the coordinates may be unknown (for example
* a motor position). Hence coordinates are general expressions and not simple
* numbers. For this we used the type Coords_exp which has three CExp
* fields. For runtime (or calculations possible at compile time), we use
* Coords which contains three double fields.
*******************************************************************************/

/* Assign coordinates. */
Coords
coords_set(MCNUM x, MCNUM y, MCNUM z)
{
  Coords a;

  a.x = x;
  a.y = y;
  a.z = z;
  return a;
}

Coords 
coords_get(Coords a, MCNUM *x, MCNUM *y, MCNUM *z)
{
  *x = a.x;
  *y = a.y;
  *z = a.z;
  return a;
}

/* Add two coordinates. */
Coords
coords_add(Coords a, Coords b)
{
  Coords c;

  c.x = a.x + b.x;
  c.y = a.y + b.y;
  c.z = a.z + b.z;
  return c;
}

/* Subtract two coordinates. */
Coords
coords_sub(Coords a, Coords b)
{
  Coords c;

  c.x = a.x - b.x;
  c.y = a.y - b.y;
  c.z = a.z - b.z;
  return c;
}

/* Negate coordinates. */
Coords
coords_neg(Coords a)
{
  Coords b;

  b.x = -a.x;
  b.y = -a.y;
  b.z = -a.z;
  return b;
}

/*******************************************************************************
* The Rotation type implements a rotation transformation of a coordinate
* system in the form of a double[3][3] matrix.
*
* Contrary to the Coords type in coords.c, rotations are passed by
* reference. Functions that yield new rotations do so by writing to an
* explicit result parameter; rotations are not returned from functions. The
* reason for this is that arrays cannot by returned from functions (though
* structures can; thus an alternative would have been to wrap the
* double[3][3] array up in a struct). Such are the ways of C programming.
*
* A rotation represents the tranformation of the coordinates of a vector when
* changing between coordinate systems that are rotated with respect to each
* other. For example, suppose that coordinate system Q is rotated 45 degrees
* around the Z axis with respect to coordinate system P. Let T be the
* rotation transformation representing a 45 degree rotation around Z. Then to
* get the coordinates of a vector r in system Q, apply T to the coordinates
* of r in P. If r=(1,0,0) in P, it will be (sqrt(1/2),-sqrt(1/2),0) in
* Q. Thus we should be careful when interpreting the sign of rotation angles:
* they represent the rotation of the coordinate systems, not of the
* coordinates (which has opposite sign).
*******************************************************************************/

/*******************************************************************************
* Get transformation for rotation first phx around x axis, then phy around y,
* then phz around z.
*******************************************************************************/
void
rot_set_rotation(Rotation t, double phx, double phy, double phz)
{
  double cx = cos(phx);
  double sx = sin(phx);
  double cy = cos(phy);
  double sy = sin(phy);
  double cz = cos(phz);
  double sz = sin(phz);

  t[0][0] = cy*cz;
  t[0][1] = sx*sy*cz + cx*sz;
  t[0][2] = sx*sz - cx*sy*cz;
  t[1][0] = -cy*sz;
  t[1][1] = cx*cz - sx*sy*sz;
  t[1][2] = sx*cz + cx*sy*sz;
  t[2][0] = sy;
  t[2][1] = -sx*cy;
  t[2][2] = cx*cy;
}

/*******************************************************************************
* Matrix multiplication of transformations (this corresponds to combining
* transformations). After rot_mul(T1, T2, T3), doing T3 is equal to doing
* first T2, then T1.
* Note that T3 must not alias (use the same array as) T1 or T2.
*******************************************************************************/
void
rot_mul(Rotation t1, Rotation t2, Rotation t3)
{
  int i,j;

  for(i = 0; i < 3; i++)
    for(j = 0; j < 3; j++)
      t3[i][j] = t1[i][0]*t2[0][j] + t1[i][1]*t2[1][j] + t1[i][2]*t2[2][j];
}

/*******************************************************************************
* Copy a rotation transformation (needed since arrays cannot be assigned in C).
*******************************************************************************/
void
rot_copy(Rotation dest, Rotation src)
{
  dest[0][0] = src[0][0];
  dest[0][1] = src[0][1];
  dest[0][2] = src[0][2];
  dest[1][0] = src[1][0];
  dest[1][1] = src[1][1];
  dest[1][2] = src[1][2];
  dest[2][0] = src[2][0];
  dest[2][1] = src[2][1];
  dest[2][2] = src[2][2];
}

void
rot_transpose(Rotation src, Rotation dst)
{
  dst[0][0] = src[0][0];
  dst[0][1] = src[1][0];
  dst[0][2] = src[2][0];
  dst[1][0] = src[0][1];
  dst[1][1] = src[1][1];
  dst[1][2] = src[2][1];
  dst[2][0] = src[0][2];
  dst[2][1] = src[1][2];
  dst[2][2] = src[2][2];
}

Coords
rot_apply(Rotation t, Coords a)
{
  Coords b;

  b.x = t[0][0]*a.x + t[0][1]*a.y + t[0][2]*a.z;
  b.y = t[1][0]*a.x + t[1][1]*a.y + t[1][2]*a.z;
  b.z = t[2][0]*a.x + t[2][1]*a.y + t[2][2]*a.z;
  return b;
}

void
mccoordschange(Coords a, Rotation t, double *x, double *y, double *z,
               double *vx, double *vy, double *vz, double *time,
               double *s1, double *s2)
{
  Coords b, c;

  b.x = *x;
  b.y = *y;
  b.z = *z;
  c = rot_apply(t, b);
  b = coords_add(c, a);
  *x = b.x;
  *y = b.y;
  *z = b.z;

  b.x = *vx;
  b.y = *vy;
  b.z = *vz;
  c = rot_apply(t, b);
  *vx = c.x;
  *vy = c.y;
  *vz = c.z;
  /* ToDo: What to do about the spin? */
}


void
mccoordschange_polarisation(Rotation t, double *sx, double *sy, double *sz)
{
  Coords b, c;

  b.x = *sx;
  b.y = *sy;
  b.z = *sz;
  c = rot_apply(t, b);
  *sx = c.x;
  *sy = c.y;
  *sz = c.z;
}

void
mcstore_neutron(MCNUM *s, int index, double x, double y, double z,
               double vx, double vy, double vz, double t, 
               double sx, double sy, double sz, double p)
{
    s[11*index+1]  = x ; 
    s[11*index+2]  = y ; 
    s[11*index+3]  = z ; 
    s[11*index+4]  = vx; 
    s[11*index+5]  = vy; 
    s[11*index+6]  = vz; 
    s[11*index+7]  = t ; 
    s[11*index+8]  = sx; 
    s[11*index+9]  = sy; 
    s[11*index+10]  = sz; 
    s[11*index+0] = p ; 
} 

void
mcrestore_neutron(MCNUM *s, int index, double *x, double *y, double *z,
               double *vx, double *vy, double *vz, double *t, 
               double *sx, double *sy, double *sz, double *p)
{
    *x  =  s[11*index+1] ;
    *y  =  s[11*index+2] ;
    *z  =  s[11*index+3] ;
    *vx =  s[11*index+4] ;
    *vy =  s[11*index+5] ;
    *vz =  s[11*index+6] ;
    *t  =  s[11*index+7] ;
    *sx =  s[11*index+8] ;
    *sy =  s[11*index+9] ;
    *sz =  s[11*index+10] ;
    *p  =  s[11*index+0];
}


double
mcestimate_error(double N, double p1, double p2)
{
  double pmean, n1;
  if(N <= 1)
    return p1;
  pmean = p1 / N;
  n1 = N - 1;
  /* Note: underflow may cause p2 to become zero; the fabs() below guards
     against this. */
  return sqrt((N/n1)*fabs(p2 - pmean*pmean));
}

/* parameters handling ====================================================== */

/* Instrument input parameter type handling. */
static int
mcparm_double(char *s, void *vptr)
{
  char *p;
  double *v = (double *)vptr;

  if (!s) { *v = 0; return(1); }
  *v = strtod(s, &p);
  if(*s == '\0' || (p != NULL && *p != '\0') || errno == ERANGE)
    return 0;                        /* Failed */
  else
    return 1;                        /* Success */
}


static char *
mcparminfo_double(char *parmname)
{
  return "double";
}


static void
mcparmerror_double(char *parm, char *val)
{
  fprintf(stderr, "Error: Invalid value '%s' for floating point parameter %s\n",
          val, parm);
}


static void
mcparmprinter_double(char *f, void *vptr)
{
  double *v = (double *)vptr;
  sprintf(f, "%g", *v);
}


static int
mcparm_int(char *s, void *vptr)
{
  char *p;
  int *v = (int *)vptr;
  long x;

  if (!s) { *v = 0; return(1); }
  *v = 0;
  x = strtol(s, &p, 10);
  if(x < INT_MIN || x > INT_MAX)
    return 0;                        /* Under/overflow */
  *v = x;
  if(*s == '\0' || (p != NULL && *p != '\0') || errno == ERANGE)
    return 0;                        /* Failed */
  else
    return 1;                        /* Success */
}


static char *
mcparminfo_int(char *parmname)
{
  return "int";
}


static void
mcparmerror_int(char *parm, char *val)
{
  fprintf(stderr, "Error: Invalid value '%s' for integer parameter %s\n",
          val, parm);
}


static void
mcparmprinter_int(char *f, void *vptr)
{
  int *v = (int *)vptr;
  sprintf(f, "%d", *v);
}


static int
mcparm_string(char *s, void *vptr)
{
  char **v = (char **)vptr;
  if (!s) { *v = NULL; return(1); }
  *v = (char *)malloc(strlen(s) + 1);
  if(*v == NULL)
  {
    fprintf(stderr, "Error: Out of memory (mcparm_string).\n");
    exit(1);
  }
  strcpy(*v, s);
  return 1;                        /* Success */
}


static char *
mcparminfo_string(char *parmname)
{
  return "string";
}


static void
mcparmerror_string(char *parm, char *val)
{
  fprintf(stderr, "Error: Invalid value '%s' for string parameter %s\n",
          val, parm);
}


static void
mcparmprinter_string(char *f, void *vptr)
{
  char **v = (char **)vptr;
  char *p;
  char *c = " \0";
  if (!*v) { *f='\0'; return; }
  strcpy(f, "");
  for(p = *v; *p != '\0'; p++)
  {
    switch(*p)
    {
      case '\n':
        strcat(f, "\\n");
        break;
      case '\r':
        strcat(f, "\\r");
        break;
      case '"':
        strcat(f, "\\\"");
        break;
      case '\\':
        strcat(f, "\\\\");
        break;
      default:
        c[0] = p[0]; strcat(f, c);
    }
  }
  /* strcat(f, "\""); */
}


static struct
  {
    int (*getparm)(char *, void *);
    char * (*parminfo)(char *);
    void (*error)(char *, char *);
    void (*printer)(char *, void *);
  } mcinputtypes[] =
      {
        mcparm_double, mcparminfo_double, mcparmerror_double,
                mcparmprinter_double,
        mcparm_int, mcparminfo_int, mcparmerror_int,
                mcparmprinter_int,
        mcparm_string, mcparminfo_string, mcparmerror_string,
                mcparmprinter_string
      };

/* init/run/rand handling =================================================== */

void
mcreadparams(void)
{
  int i,j,status;
  char buf[1024];
  char *p;
  int len;

  for(i = 0; mcinputtable[i].name != 0; i++)
  {
    do
    {
      printf("Set value of instrument parameter %s (%s):\n",
             mcinputtable[i].name,
             (*mcinputtypes[mcinputtable[i].type].parminfo)
                  (mcinputtable[i].name));
      fflush(stdout);
      p = fgets(buf, 1024, stdin);
      if(p == NULL)
      {
        fprintf(stderr, "Error: empty input\n");
        exit(1);
      }
      len = strlen(buf);
      for(j = 0; j < 2; j++)
      {
        if(len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        {
          len--;
          buf[len] = '\0';
        }
      }
      status = (*mcinputtypes[mcinputtable[i].type].getparm)
                   (buf, mcinputtable[i].par);
      if(!status)
      {
        (*mcinputtypes[mcinputtable[i].type].error)(mcinputtable[i].name, buf);
      }
    } while(!status);
  }
}



void
mcsetstate(double x, double y, double z, double vx, double vy, double vz,
           double t, double sx, double sy, double sz, double p)
{
  extern double mcnx, mcny, mcnz, mcnvx, mcnvy, mcnvz;
  extern double mcnt, mcnsx, mcnsy, mcnsz, mcnp;

  mcnx = x;
  mcny = y;
  mcnz = z;
  mcnvx = vx;
  mcnvy = vy;
  mcnvz = vz;
  mcnt = t;
  mcnsx = sx;
  mcnsy = sy;
  mcnsz = sz;
  mcnp = p;
}

void
mcgenstate(void)
{
  mcsetstate(0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1);
}

/* McStas random number routine. */

/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * This is derived from the Berkeley source:
 *        @(#)random.c        5.5 (Berkeley) 7/6/88
 * It was reworked for the GNU C Library by Roland McGrath.
 * Rewritten to use reentrant functions by Ulrich Drepper, 1995.
 */

/*******************************************************************************
* Modified for McStas from glibc 2.0.7pre1 stdlib/random.c and
* stdlib/random_r.c.
*
* This way random() is more than four times faster compared to calling
* standard glibc random() on ix86 Linux, probably due to multithread support,
* ELF shared library overhead, etc. It also makes McStas generated
* simulations more portable (more likely to behave identically across
* platforms, important for parrallel computations).
*******************************************************************************/


#define        TYPE_3                3
#define        BREAK_3                128
#define        DEG_3                31
#define        SEP_3                3

static mc_int32_t randtbl[DEG_3 + 1] =
  {
    TYPE_3,

    -1726662223, 379960547, 1735697613, 1040273694, 1313901226,
    1627687941, -179304937, -2073333483, 1780058412, -1989503057,
    -615974602, 344556628, 939512070, -1249116260, 1507946756,
    -812545463, 154635395, 1388815473, -1926676823, 525320961,
    -1009028674, 968117788, -123449607, 1284210865, 435012392,
    -2017506339, -911064859, -370259173, 1132637927, 1398500161,
    -205601318,
  };

static mc_int32_t *fptr = &randtbl[SEP_3 + 1];
static mc_int32_t *rptr = &randtbl[1];
static mc_int32_t *state = &randtbl[1];
#define rand_deg DEG_3
#define rand_sep SEP_3
static mc_int32_t *end_ptr = &randtbl[sizeof (randtbl) / sizeof (randtbl[0])];

mc_int32_t
mc_random (void)
{
  mc_int32_t result;

  *fptr += *rptr;
  /* Chucking least random bit.  */
  result = (*fptr >> 1) & 0x7fffffff;
  ++fptr;
  if (fptr >= end_ptr)
  {
    fptr = state;
    ++rptr;
  }
  else
  {
    ++rptr;
    if (rptr >= end_ptr)
      rptr = state;
  }
  return result;
}

void
mc_srandom (unsigned int x)
{
  /* We must make sure the seed is not 0.  Take arbitrarily 1 in this case.  */
  state[0] = x ? x : 1;
  {
    long int i;
    for (i = 1; i < rand_deg; ++i)
    {
      /* This does:
         state[i] = (16807 * state[i - 1]) % 2147483647;
         but avoids overflowing 31 bits.  */
      long int hi = state[i - 1] / 127773;
      long int lo = state[i - 1] % 127773;
      long int test = 16807 * lo - 2836 * hi;
      state[i] = test + (test < 0 ? 2147483647 : 0);
    }
    fptr = &state[rand_sep];
    rptr = &state[0];
    for (i = 0; i < 10 * rand_deg; ++i)
      random ();
  }
}

/* "Mersenne Twister", by Makoto Matsumoto and Takuji Nishimura. */
/* See http://www.math.keio.ac.jp/~matumoto/emt.html for original source. */

/*   Coded by Takuji Nishimura, considering the suggestions by */
/* Topher Cooper and Marc Rieffel in July-Aug. 1997.           */

/* This library is free software; you can redistribute it and/or   */
/* modify it under the terms of the GNU Library General Public     */
/* License as published by the Free Software Foundation; either    */
/* version 2 of the License, or (at your option) any later         */
/* version.                                                        */
/* This library is distributed in the hope that it will be useful, */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of  */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.            */
/* See the GNU Library General Public License for more details.    */
/* You should have received a copy of the GNU Library General      */
/* Public License along with this library; if not, write to the    */
/* Free Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA   */ 
/* 02111-1307  USA                                                 */

/* Copyright (C) 1997 Makoto Matsumoto and Takuji Nishimura.       */
/* When you use this, send an email to: matumoto@math.keio.ac.jp   */
/* with an appropriate reference to your work.                     */

#define N 624
#define M 397
#define MATRIX_A 0x9908b0df   /* constant vector a */
#define UPPER_MASK 0x80000000 /* most significant w-r bits */
#define LOWER_MASK 0x7fffffff /* least significant r bits */

#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000
#define TEMPERING_SHIFT_U(y)  (y >> 11)
#define TEMPERING_SHIFT_S(y)  (y << 7)
#define TEMPERING_SHIFT_T(y)  (y << 15)
#define TEMPERING_SHIFT_L(y)  (y >> 18)

static unsigned long mt[N]; /* the array for the state vector  */
static int mti=N+1; /* mti==N+1 means mt[N] is not initialized */

/* initializing the array with a NONZERO seed */
void
mt_srandom(unsigned long seed)
{
    /* setting initial seeds to mt[N] using         */
    /* the generator Line 25 of Table 1 in          */
    /* [KNUTH 1981, The Art of Computer Programming */
    /*    Vol. 2 (2nd Ed.), pp102]                  */
    mt[0]= seed & 0xffffffff;
    for (mti=1; mti<N; mti++)
        mt[mti] = (69069 * mt[mti-1]) & 0xffffffff;
}

unsigned long
mt_random(void)
{
    unsigned long y;
    static unsigned long mag01[2]={0x0, MATRIX_A};
    /* mag01[x] = x * MATRIX_A  for x=0,1 */

    if (mti >= N) { /* generate N words at one time */
        int kk;

        if (mti == N+1)   /* if sgenrand() has not been called, */
            mt_srandom(4357); /* a default initial seed is used   */

        for (kk=0;kk<N-M;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1];
        }
        for (;kk<N-1;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1];
        }
        y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
        mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1];

        mti = 0;
    }
  
    y = mt[mti++];
    y ^= TEMPERING_SHIFT_U(y);
    y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
    y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
    y ^= TEMPERING_SHIFT_L(y);

    return y;
}
#undef N
#undef M
#undef MATRIX_A
#undef UPPER_MASK
#undef LOWER_MASK
#undef TEMPERING_MASK_B
#undef TEMPERING_MASK_C
#undef TEMPERING_SHIFT_U
#undef TEMPERING_SHIFT_S
#undef TEMPERING_SHIFT_T
#undef TEMPERING_SHIFT_L
/* End of "Mersenne Twister". */

/* End of McStas random number routine. */

double
randnorm(void)
{
  static double v1, v2, s;
  static int phase = 0;
  double X, u1, u2;

  if(phase == 0)
  {
    do
    {
      u1 = rand01();
      u2 = rand01();
      v1 = 2*u1 - 1;
      v2 = 2*u2 - 1;
      s = v1*v1 + v2*v2;
    } while(s >= 1 || s == 0);

    X = v1*sqrt(-2*log(s)/s);
  }
  else
  {
    X = v2*sqrt(-2*log(s)/s);
  }

  phase = 1 - phase;
  return X;
}

/* intersect handling ======================================================= */

/* Compute normal vector to (x,y,z). */
void normal_vec(double *nx, double *ny, double *nz,
                double x, double y, double z)
{
  double ax = fabs(x);
  double ay = fabs(y);
  double az = fabs(z);
  double l;
  if(x == 0 && y == 0 && z == 0)
  {
    *nx = 0;
    *ny = 0;
    *nz = 0;
    return;
  }
  if(ax < ay)
  {
    if(ax < az)
    {                           /* Use X axis */
      l = sqrt(z*z + y*y);
      *nx = 0;
      *ny = z/l;
      *nz = -y/l;
      return;
    }
  }
  else
  {
    if(ay < az)
    {                           /* Use Y axis */
      l = sqrt(z*z + x*x);
      *nx = z/l;
      *ny = 0;
      *nz = -x/l;
      return;
    }
  }
  /* Use Z axis */
  l = sqrt(y*y + x*x);
  *nx = y/l;
  *ny = -x/l;
  *nz = 0;
}

/* If intersection with box dt_in and dt_out is returned */
/* This function written by Stine Nyborg, 1999. */
int box_intersect(double *dt_in, double *dt_out,
                  double x, double y, double z,
                  double vx, double vy, double vz,
                  double dx, double dy, double dz)
{
  double x_in, y_in, z_in, tt, t[6], a, b;
  int i, count, s;

      /* Calculate intersection time for each of the six box surface planes
       *  If the box surface plane is not hit, the result is zero.*/
  
  if(vx != 0)
   {
    tt = -(dx/2 + x)/vx;
    y_in = y + tt*vy;
    z_in = z + tt*vz;
    if( y_in > -dy/2 && y_in < dy/2 && z_in > -dz/2 && z_in < dz/2)
      t[0] = tt;
    else
      t[0] = 0;

    tt = (dx/2 - x)/vx;
    y_in = y + tt*vy;
    z_in = z + tt*vz;
    if( y_in > -dy/2 && y_in < dy/2 && z_in > -dz/2 && z_in < dz/2)
      t[1] = tt;
    else
      t[1] = 0;
   }
  else
    t[0] = t[1] = 0;

  if(vy != 0)
   {
    tt = -(dy/2 + y)/vy;
    x_in = x + tt*vx;
    z_in = z + tt*vz;
    if( x_in > -dx/2 && x_in < dx/2 && z_in > -dz/2 && z_in < dz/2)
      t[2] = tt;
    else
      t[2] = 0;

    tt = (dy/2 - y)/vy;
    x_in = x + tt*vx;
    z_in = z + tt*vz;
    if( x_in > -dx/2 && x_in < dx/2 && z_in > -dz/2 && z_in < dz/2)
      t[3] = tt;
    else
      t[3] = 0;
   }
  else
    t[2] = t[3] = 0;

  if(vz != 0)
   {
    tt = -(dz/2 + z)/vz;
    x_in = x + tt*vx;
    y_in = y + tt*vy;
    if( x_in > -dx/2 && x_in < dx/2 && y_in > -dy/2 && y_in < dy/2)
      t[4] = tt;
    else
      t[4] = 0;

    tt = (dz/2 - z)/vz;
    x_in = x + tt*vx;
    y_in = y + tt*vy;
    if( x_in > -dx/2 && x_in < dx/2 && y_in > -dy/2 && y_in < dy/2)
      t[5] = tt;
    else
      t[5] = 0;
   }
  else
    t[4] = t[5] = 0;

  /* The intersection is evaluated and *dt_in and *dt_out are assigned */

  a = b = s = 0;
  count = 0;

  for( i = 0; i < 6; i = i + 1 )
    if( t[i] == 0 )
      s = s+1;
    else if( count == 0 )
    {
      a = t[i];
      count = 1;
    }
    else
    {
      b = t[i];
      count = 2;
    }

  if ( a == 0 && b == 0 )
    return 0;
  else if( a < b )
  {
    *dt_in = a;
    *dt_out = b;
    return 1;
  }
  else
  {
    *dt_in = b;
    *dt_out = a;
    return 1;
  }

}

/* Written by: EM,NB,ABA 4.2.98 */
int
cylinder_intersect(double *t0, double *t1, double x, double y, double z,
                   double vx, double vy, double vz, double r, double h)
{
  double D, t_in, t_out, y_in, y_out;
  int ret=1;

  D = (2*vx*x + 2*vz*z)*(2*vx*x + 2*vz*z)
    - 4*(vx*vx + vz*vz)*(x*x + z*z - r*r);

  if (D>=0)
  {
    t_in  = (-(2*vz*z + 2*vx*x) - sqrt(D))/(2*(vz*vz + vx*vx));
    t_out = (-(2*vz*z + 2*vx*x) + sqrt(D))/(2*(vz*vz + vx*vx));
    y_in = vy*t_in + y;
    y_out =vy*t_out + y;

    if ( (y_in > h/2 && y_out > h/2) || (y_in < -h/2 && y_out < -h/2) )
      return 0;
    else
    {
      if (y_in > h/2)
        { t_in = ((h/2)-y)/vy; ret += 2; }
      else if (y_in < -h/2)
        { t_in = ((-h/2)-y)/vy; ret += 4; }
      if (y_out > h/2)
        { t_out = ((h/2)-y)/vy; ret += 8; }
      else if (y_out < -h/2)
        { t_out = ((-h/2)-y)/vy; ret += 16; }
    }
    *t0 = t_in;
    *t1 = t_out;
    return ret;
  }
  else
  {
    *t0 = *t1 = 0;
    return 0;
  }
}


/* Calculate intersection between line and sphere. */
int
sphere_intersect(double *t0, double *t1, double x, double y, double z,
                 double vx, double vy, double vz, double r)
{
  double A, B, C, D, v;

  v = sqrt(vx*vx + vy*vy + vz*vz);
  A = v*v;
  B = 2*(x*vx + y*vy + z*vz);
  C = x*x + y*y + z*z - r*r;
  D = B*B - 4*A*C;
  if(D < 0)
    return 0;
  D = sqrt(D);
  *t0 = (-B - D) / (2*A);
  *t1 = (-B + D) / (2*A);
  return 1;
}


/* ADD: E. Farhi, Aug 6th, 2001 plane_intersect_Gfast 
 * intersection of a plane and a trajectory with gravitation 
 * this function calculates the intersection between a neutron trajectory
 * and a plane with acceleration gx,gy,gz. The neutron starts at point x,y,z
 * with velocity vx, vy, vz. The plane has a normal vector nx,ny,nz and 
 * contains the point wx,wy,wz
 * The function returns 0 if no intersection occured after the neutron started
 * and non 0 if there is an intersection. Then *Idt is the time until 
 * the neutron hits the roof.
 * Let n=(nx,ny,nz) be the normal plane vector (one of the six sides) 
 * Let W=(wx,wy,wz) be Any point on this plane (for instance at z=0)
 * The problem consists in solving the 2nd order equation:
 *      1/2.n.g.t^2 + n.v.t + n.(r-W) = 0 (1)
 * Without acceleration, t=-n.(r-W)/n.v
 */
  
int plane_intersect_Gfast(double *Idt, 
                  double A,  double B,  double C)
{
  /* plane_intersect_Gfast(&dt, A, B, C)
   * A = 0.5 n.g; B = n.v; C = n.(r-W);
   * no acceleration when A=0
   */
  int ret=0;
  double dt0;

  *Idt = 0;

  if (B) dt0 = -C/B;
  if (fabs(A) < 1E-10) /* this plane is parallel to the acceleration */
  {
    if (B)
    { *Idt = dt0; ret=3; }
    /* else the speed is parallel to the plane, no intersection */
  }
  else
  {
    double D, sD, dt1, dt2;
    D = B*B - 4*A*C;
    if (D >= 0) /* Delta > 0: neutron trajectory hits the mirror */
    {
      sD = sqrt(D);
      dt1 = (-B + sD)/2/A;
      dt2 = (-B - sD)/2/A;
      if (B)
      {
        if (fabs(dt0-dt1) < fabs(dt0-dt2)) ret=1; else ret=2;
      }
      else
      {
        if (dt1 <= dt2) ret=1; else ret=2;
      }
      if (ret==1) *Idt = dt1; 
      else if (ret==2) *Idt = dt2;
    } /* else Delta <0: no intersection */
  }
  return(ret);
}


/* Choose random direction towards target at (x,y,z) with given radius. */
/* If radius is zero, choose random direction in full 4PI, no target. */
void
randvec_target_circle(double *xo, double *yo, double *zo, double *solid_angle,
               double xi, double yi, double zi, double radius)
{
  double l2, phi, theta, nx, ny, nz, xt, yt, zt, xu, yu, zu;

  if(radius == 0.0)
  {
    /* No target, choose uniformly a direction in full 4PI solid angle. */
    theta = acos (1 - rand0max(2));
    phi = rand0max(2 * PI);
    if(solid_angle)
      *solid_angle = 4*PI;
    nx = 1;
    ny = 0;
    nz = 0;
    xi = 0;
    yi = 1;
    zi = 0;
  }
  else
  {
    double costheta0;
    l2 = xi*xi + yi*yi + zi*zi; /* sqr Distance to target. */
    costheta0 = sqrt(l2/(radius*radius+l2));
    if (radius < 0) costheta0 *= -1;
    if(solid_angle)
    {
      /* Compute solid angle of target as seen from origin. */
        *solid_angle = 2*PI*(1 - costheta0);
    }

    /* Now choose point uniformly on sphere surface within angle theta0 */
    theta = acos (1 - rand0max(1 - costheta0)); /* radius on circle */
    phi = rand0max(2 * PI); /* rotation on circle at given radius */
    /* Now, to obtain the desired vector rotate (xi,yi,zi) angle theta around a
       perpendicular axis u=i x n and then angle phi around i. */
    if(xi == 0 && zi == 0)
    {
      nx = 1;
      ny = 0;
      nz = 0;
    }
    else
    {
      nx = -zi;
      nz = xi;
      ny = 0;
    }
  }
  
  /* [xyz]u = [xyz]i x n[xyz] (usually vertical) */
  vec_prod(xu,  yu,  zu, xi, yi, zi,        nx, ny, nz);   
  /* [xyz]t = [xyz]i rotated theta around [xyz]u */
  rotate  (xt,  yt,  zt, xi, yi, zi, theta, xu, yu, zu);
  /* [xyz]o = [xyz]t rotated phi around n[xyz] */
  rotate (*xo, *yo, *zo, xt, yt, zt, phi, xi, yi, zi);
}


/* Choose random direction towards target at (xi,yi,zi) with given       */
/* ANGULAR dimension height x width. height=phi_x, width=phi_y (radians)*/
/* If height or width is zero, choose random direction in full 4PI, no target. */
void
randvec_target_rect(double *xo, double *yo, double *zo, double *solid_angle,
               double xi, double yi, double zi, double width, double height)
{
  double theta, phi, nx, ny, nz, xt, yt, zt, xu, yu, zu;

  if(height == 0.0 || width == 0.0)
  {
    randvec_target_circle(xo, yo, zo, solid_angle,
               0, 0, 0, 0);
  }
  else
  {
    if(solid_angle)
    {
      /* Compute solid angle of target as seen from origin. */
      *solid_angle = 2*fabs(width*sin(height/2));
    }

    /* Now choose point uniformly on quadrant within angle theta0/phi0 */
    theta = width*randpm1()/2.0;
    phi   = height*randpm1()/2.0; 
    /* Now, to obtain the desired vector rotate (xi,yi,zi) angle phi around 
       n, and then theta around u. */
    if(xi == 0 && zi == 0)
    {
      nx = 1;
      ny = 0;
      nz = 0;
    }
    else
    {
      nx = -zi;
      nz = xi;
      ny = 0;
    }
  }
  
  /* [xyz]u = [xyz]i x n[xyz] (usually vertical) */
  vec_prod(xu,  yu,  zu, xi, yi, zi,        nx, ny, nz);   
  /* [xyz]t = [xyz]i rotated theta around [xyz]u */
  rotate  (xt,  yt,  zt, xi, yi, zi, phi, nx, ny, nz);
  /* [xyz]o = [xyz]t rotated phi around n[xyz] */
  rotate (*xo, *yo, *zo, xt, yt, zt, theta, xu,  yu,  zu);
}

/* Make sure a list is big enough to hold element COUNT.
*
* The list is an array, and the argument 'list' is a pointer to a pointer to
* the array start. The argument 'size' is a pointer to the number of elements
* in the array. The argument 'elemsize' is the sizeof() an element. The
* argument 'count' is the minimum number of elements needed in the list.
*
* If the old array is to small (or if *list is NULL or *size is 0), a
* sufficuently big new array is allocated, and *list and *size are updated.
*/
void extend_list(int count, void **list, int *size, size_t elemsize)
{
  if(count >= *size)
  {
    void *oldlist = *list;
    if(*size > 0)
      *size *= 2;
    else
      *size = 32;
    *list = malloc(*size*elemsize);
    if(!*list)
    {
      fprintf(stderr, "\nError: Out of memory (extend_list).\n");
      exit(1);
    }
    if(oldlist)
    {
      memcpy(*list, oldlist, count*elemsize);
      free(oldlist);
    }
  }
}

/* Number of neutron histories to simulate. */
static double mcncount = 1e6;
double mcrun_num = 0;

void
mcset_ncount(double count) 
{
  mcncount = count;
}

double
mcget_ncount(void)
{
  return mcncount;
}

double
mcget_run_num(void)
{
  return mcrun_num;
}

static void
mcsetn_arg(char *arg)
{
  mcset_ncount(strtod(arg, NULL));
}

static void
mcsetseed(char *arg)
{
  mcseed = atol(arg);
  if(mcseed)
    srandom(mcseed);
  else
  {
    fprintf(stderr, "Error: seed most not be zero.\n");
    exit(1);
  }
}

static void
mchelp(char *pgmname)
{
  int i;

  fprintf(stderr, "Usage: %s [options] [parm=value ...]\n", pgmname);
  fprintf(stderr,
"Options are:\n"
"  -s SEED   --seed=SEED      Set random seed (must be != 0)\n"
"  -n COUNT  --ncount=COUNT   Set number of neutrons to simulate.\n"
"  -d DIR    --dir=DIR        Put all data files in directory DIR.\n"
"  -f FILE   --file=FILE      Put all data in a single file.\n"
"  -t        --trace          Enable trace of neutron through instrument.\n"
"  -g        --gravitation    Enable gravitation for all trajectories.\n"
"  -a        --data-only      Do not put any headers in the data files.\n"
"  --no-output-files          Do not write any data files.\n"
"  -h        --help           Show this help message.\n"
"  -i        --info           Detailed instrument information.\n"
"  --format=FORMAT            Output data files using format FORMAT\n"
);
  if(mcnumipar > 0)
  {
    fprintf(stderr, "Instrument parameters are:\n");
    for(i = 0; i < mcnumipar; i++)
      fprintf(stderr, "  %-16s(%s)\n", mcinputtable[i].name,
        (*mcinputtypes[mcinputtable[i].type].parminfo)(mcinputtable[i].name));
  }
  fprintf(stderr, "Available output formats are:\n");
  for (i=0; i < mcNUMFORMATS; fprintf(stderr,"\"%s\" " , mcformats[i++].Name) );
  fprintf(stderr, "\nFormat modifiers: FORMAT may be followed by 'binary float' or \n");
  fprintf(stderr, "'binary double' to save data blocks as binary. Please use also -a flag then.\n");
  if (mcNUMFORMATS <= 2) fprintf(stderr, "Re-compile with -DALL_FORMATS to enable more output formats.\n");
}

static void
mcshowhelp(char *pgmname)
{
  mchelp(pgmname);
  exit(0);
}

static void
mcusage(char *pgmname)
{
  fprintf(stderr, "Error: incorrect command line arguments\n");
  mchelp(pgmname);
  exit(1);
}

static void
mcenabletrace(void)
{
 if(mctraceenabled)
  mcdotrace = 1;
 else
 {
   fprintf(stderr,
           "Error: trace not enabled.\n"
           "Please re-run the McStas compiler "
                   "with the --trace option, or rerun the\n"
           "C compiler with the MC_TRACE_ENABLED macro defined.\n");
   exit(1);
 }
}

/* file i/o handling ======================================================== */
/* opens a new file within mcdirname if non NULL */
/* if mode is non 0, then mode is used, else mode is 'w' */

FILE *
mcnew_file(char *name, char *mode)
{
  int dirlen;
  char *mem;
  FILE *file;

  if (!name || strlen(name) == 0) return(NULL);
  
  dirlen = mcdirname ? strlen(mcdirname) : 0;
  mem = malloc(dirlen + 1 + strlen(name) + 1);
  if(!mem)
  {
    fprintf(stderr, "Error: Out of memory (mcnew_file)\n");
    exit(1);
  }
  strcpy(mem, "");
  if(dirlen)
  {
    strcat(mem, mcdirname);
    if(mcdirname[dirlen - 1] != MC_PATHSEP_C &&
       name[0] != MC_PATHSEP_C)
      strcat(mem, MC_PATHSEP_S);
  }
  strcat(mem, name);
  file = fopen(mem, (mode ? mode : "w"));
  if(!file)
    fprintf(stderr, "Warning: could not open output file '%s'\n", mem);
  free(mem);
  return file;
} /* mcnew_file */

/* mcvalid_name: makes a valid string for variable names.
 * copy 'original' into 'valid', replacing invalid characters by '_'
 * char arrays must be pre-allocated. n can be 0, or the maximum number of
 * chars to be copied/checked
 */
static char *mcvalid_name(char *valid, char *original, int n)
{
  long i;
  
  if (!n) n = strlen(valid);
  if (original == NULL || strlen(original) == 0) 
  { strcpy(valid, "noname"); return(valid); }
  
  if (n > strlen(original)) n = strlen(original);
  strncpy(valid, original, n);
  
  for (i=0; i < n; i++)
  { 
    if ( (valid[i] > 122) 
      || (valid[i] < 32) 
      || (strchr("!\"#$%&'()*+,-.:;<=>?@[\\]^`/ ", valid[i]) != NULL) )
    {
      if (i) valid[i] = '_'; else valid[i] = 'm';
    }
  }
  valid[i] = '\0';
  
  return(valid);
} /* mcvalid_name */

#if defined(NL_ARGMAX) || defined(WIN32)
static int pfprintf(FILE *f, char *fmt, char *fmt_args, ...)
{
/* this function 
1- look for the maximum %d$ field in fmt
2- looks for all %d$ fields up to max in fmt and set their type (next alpha)
3- retrieve va_arg up to max, and save pointer to arg in local arg array
4- use strchr to split around '%' chars, until all pieces are written

usage: just as fprintf, but with (char *)fmt_args being the list of arg type
 */
  
  #define MyNL_ARGMAX 50
  char  *fmt_pos;
  
  char *arg_char[MyNL_ARGMAX];
  int   arg_int[MyNL_ARGMAX];
  long  arg_long[MyNL_ARGMAX];
  double arg_double[MyNL_ARGMAX];
  
  char *arg_posB[MyNL_ARGMAX];  /* position of '%' */
  char *arg_posE[MyNL_ARGMAX];  /* position of '$' */
  char *arg_posT[MyNL_ARGMAX];  /* position of type */
  
  int   arg_num[MyNL_ARGMAX];   /* number of argument (between % and $) */
  int   this_arg=0;
  int   arg_max=0;
  va_list ap;

  if (!f || !fmt_args || !fmt) return(-1);
  for (this_arg=0; this_arg<MyNL_ARGMAX;  arg_num[this_arg++] =0); this_arg = 0;
  fmt_pos = fmt;
  while(1)  /* analyse the format string 'fmt' */
  {
    char *tmp;
    
    arg_posB[this_arg] = (char *)strchr(fmt_pos, '%');
    tmp = arg_posB[this_arg];
    if (tmp)
    {
      arg_posE[this_arg] = (char *)strchr(tmp, '$');
      if (arg_posE[this_arg] && tmp[1] != '%')
      {
        char  this_arg_chr[10];
        char  printf_formats[]="dliouxXeEfgGcs\0";
        
        /* extract positional argument index %*$ in fmt */
        strncpy(this_arg_chr, arg_posB[this_arg]+1, arg_posE[this_arg]-arg_posB[this_arg]-1);
        this_arg_chr[arg_posE[this_arg]-arg_posB[this_arg]-1] = '\0';
        arg_num[this_arg] = atoi(this_arg_chr);
        if (arg_num[this_arg] <=0 || arg_num[this_arg] >= MyNL_ARGMAX)
          return(-fprintf(stderr,"pfprintf: invalid positional argument number (<=0 or >=%i) %s.\n", MyNL_ARGMAX, arg_posB[this_arg]));
        /* get type of positional argument: follows '%' -> arg_posE[this_arg]+1 */
        fmt_pos = arg_posE[this_arg]+1;
        if (!strchr(printf_formats, fmt_pos[0])) 
          return(-fprintf(stderr,"pfprintf: invalid positional argument type (%c != expected %c).\n", fmt_pos[0], fmt_args[arg_num[this_arg]-1]));
        if (fmt_pos[0] == 'l' && fmt_pos[1] == 'i') fmt_pos++;
        arg_posT[this_arg] = fmt_pos;
        /* get next argument... */
        this_arg++;
      } 
      else
      {
        if  (tmp[1] != '%')
          return(-fprintf(stderr,"pfprintf: must use only positional arguments (%s).\n", arg_posB[this_arg]));
        else fmt_pos = arg_posB[this_arg]+2;  /* found %% */
      }
    } else 
      break;  /* no more % argument */
  }
  arg_max = this_arg;
  /* get arguments from va_arg list, according to their type */
  va_start(ap, fmt_args);
  for (this_arg=0; this_arg<strlen(fmt_args); this_arg++)
  {
    
    switch(fmt_args[this_arg])
    {
      case 's':                       /* string */
              arg_char[this_arg] = va_arg(ap, char *);
              break;
      case 'd':
      case 'i':  
      case 'c':                     /* int */
              arg_int[this_arg] = va_arg(ap, int);
              break;
      case 'l':                       /* int */
              arg_long[this_arg] = va_arg(ap, long int);
              break;
      case 'f': 
      case 'g': 
      case 'G':                      /* double */
              arg_double[this_arg] = va_arg(ap, double);
              break;
      default: fprintf(stderr,"pfprintf: argument type is not implemented (arg %%%i$ type %c).\n", this_arg+1, fmt_args[this_arg]);
    }
  }
  va_end(ap);
  /* split fmt string into bits containing only 1 argument */
  fmt_pos = fmt;
  for (this_arg=0; this_arg<arg_max; this_arg++)
  {
    char *fmt_bit;
    int   arg_n;
    
    if (arg_posB[this_arg]-fmt_pos>0)
    {
      fmt_bit = (char*)malloc(arg_posB[this_arg]-fmt_pos+10);
      if (!fmt_bit) return(-fprintf(stderr,"pfprintf: not enough memory.\n"));
      strncpy(fmt_bit, fmt_pos, arg_posB[this_arg]-fmt_pos);
      fmt_bit[arg_posB[this_arg]-fmt_pos] = '\0';
      fprintf(f, fmt_bit); /* fmt part without argument */
    } else 
    {
      fmt_bit = (char*)malloc(10);
      if (!fmt_bit) return(-fprintf(stderr,"pfprintf: not enough memory.\n"));
    }
    arg_n = arg_num[this_arg]-1; /* must be >= 0 */
    strcpy(fmt_bit, "%");
    strncat(fmt_bit, arg_posE[this_arg]+1, arg_posT[this_arg]-arg_posE[this_arg]);
    fmt_bit[arg_posT[this_arg]-arg_posE[this_arg]+1] = '\0';
    
    switch(fmt_args[arg_n])
    {
      case 's': fprintf(f, fmt_bit, arg_char[arg_n]);
                break;
      case 'd': 
      case 'i':
      case 'c':                      /* int */
              fprintf(f, fmt_bit, arg_int[arg_n]);
              break;
      case 'l':                       /* long */
              fprintf(f, fmt_bit, arg_long[arg_n]);
              break;
      case 'f': 
      case 'g': 
      case 'G':                       /* double */
              fprintf(f, fmt_bit, arg_double[arg_n]);
              break;
    }
    fmt_pos = arg_posT[this_arg]+1;
    if (this_arg == arg_max-1)
    { /* add eventual leading characters for last parameter */
      if (fmt_pos < fmt+strlen(fmt))
        fprintf(f, "%s", fmt_pos);
    }
    if (fmt_bit) free(fmt_bit);
    
  }
  return(this_arg);
}
#else
static int pfprintf(FILE *f, char *fmt, char *fmt_args, ...)
{ /* wrapper to standard fprintf */
  va_list ap;
  int tmp;

  va_start(ap, fmt_args);
  tmp=vfprintf(f, fmt, ap);
  va_end(ap);
  return(tmp);
}
#endif

/* mcfile_header: output header/footer using specific file format.
 * outputs, in file 'name' having preallocated 'f' handle, the format Header
 * 'part' may be 'header' or 'footer' depending on part to write
 * if name == NULL, ignore function (no header/footer output)
 */
static int mcfile_header(FILE *f, struct mcformats_struct format, char *part, char *pre, char *name, char *parent)
{
  char user[64];
  char date[64];
  char *HeadFoot;
  long date_l; /* date as a long number */
  time_t t;
  char valid_parent[256];
  char instrname[256];
  char file[256];
  
  if(!f)
    return (-1);
    
  time(&t);
  
  if (part && !strcmp(part,"footer")) 
  {
    HeadFoot = format.Footer;
    date_l = (long)t;;
  }
  else 
  {
    HeadFoot = format.Header;
    date_l = mcstartdate;
  }
  t = (time_t)date_l;
    
  if (!strlen(HeadFoot) || (!name)) return (-1);

  sprintf(file,"%s",name);
  sprintf(user,"%s on %s", getenv("USER"), getenv("HOST"));
  sprintf(instrname,"%s (%s)", mcinstrument_name, mcinstrument_source);
  strncpy(date, ctime(&t), 64); 
  if (strlen(date)) date[strlen(date)-1] = '\0';
  
  if (parent && strlen(parent)) mcvalid_name(valid_parent, parent, 256);
  else strcpy(valid_parent, "root");
  
  return(pfprintf(f, HeadFoot, "sssssssl", 
    pre,                  /* %1$s */
    instrname,            /* %2$s */
    file,                 /* %3$s */
    format.Name,          /* %4$s */
    date,                 /* %5$s */
    user,                 /* %6$s */
    valid_parent,         /* %7$s*/
    date_l));             /* %8$li */
} /* mcfile_header */

/* mcfile_tag: output tag/value using specific file format.
 * outputs, in file with 'f' handle, a tag/value pair.
 * if name == NULL, ignore function (no section definition)
 */
static int mcfile_tag(FILE *f, struct mcformats_struct format, char *pre, char *section, char *name, char *value)
{
  char valid_section[256];
  int i;
  
  if (!strlen(format.AssignTag) || (!name) || (!f)) return(-1);
  
  mcvalid_name(valid_section, section, 256);
  
  /* remove quote chars in values */
  if (strstr(format.Name, "Scilab") || strstr(format.Name, "Matlab") || strstr(format.Name, "IDL"))
    for(i = 0; i < strlen(value); i++)
      if (value[i] == '"' || value[i] == '\'') value[i] = ' ';
  
  return(pfprintf(f, format.AssignTag, "ssss",
    pre,          /* %1$s */
    valid_section,/* %2$s */
    name,         /* %3$s */
    value));      /* %4$s */
} /* mcfile_tag */

/* mcfile_section: output section start/end using specific file format.
 * outputs, in file 'name' having preallocated 'f' handle, the format Section.
 * 'part' may be 'begin' or 'end' depending on section part to write
 * 'type' may be e.g. 'instrument','simulation','component','data'
 * if name == NULL, ignore function (no section definition)
 * the prefix 'pre' is automatically idented/un-indented (pre-allocated !)
 */
 
static int mcfile_section(FILE *f, struct mcformats_struct format, char *part, char *pre, char *name, char *type, char *parent, int level) 
{
  char *Section;
  char valid_name[256];
  char valid_parent[256];
  int  ret;
  
  if(!f)
    return (-1);
  
  if (part && !strcmp(part,"end")) Section = format.EndSection;
  else Section = format.BeginSection;
    
  if (!strlen(Section) || (!name)) return (-1);
  
  mcvalid_name(valid_name, name, 256);
  if (parent && strlen(parent)) mcvalid_name(valid_parent, parent, 256);
  else strcpy(valid_parent, "root");
  
  if (!strcmp(part,"end") && pre) 
  {
    if (strlen(pre) <= 2) strcpy(pre,"");
    else pre[strlen(pre)-2]='\0'; 
  }
  
  ret = pfprintf(f, Section, "ssssssl",
    pre,          /* %1$s */
    type,         /* %2$s */
    name,         /* %3$s */
    valid_name,   /* %4$s */
    parent,       /* %5$s */
    valid_parent, /* %6$s */
    level);       /* %7$li */
  
  if (!strcmp(part,"begin")) 
  {
    strcat(pre,"  ");
    if (name && strlen(name)) 
      mcfile_tag(f, format, pre, name, "name", name);
    if (parent && strlen(parent)) 
      mcfile_tag(f, format, pre, name, "parent", parent);
  }
  
  
  return(ret);
} /* mcfile_section */

static void mcinfo_instrument(FILE *f, struct mcformats_struct format, 
  char *pre, char *name)
{
  char Value[1300] = "";
  int  i;
  
  if (!f) return;

  for(i = 0; i < mcnumipar; i++)
  {
    char ThisParam[256];
    if (strlen(mcinputtable[i].name) > 200) break;
    sprintf(ThisParam, " %s(%s)", mcinputtable[i].name,
            (*mcinputtypes[mcinputtable[i].type].parminfo)
                (mcinputtable[i].name));
    strcat(Value, ThisParam);
    if (strlen(Value) > 1024) break;
  }
  mcfile_tag(f, format, pre, name, "Parameters", Value);
  mcfile_tag(f, format, pre, name, "Source", mcinstrument_source);
  mcfile_tag(f, format, pre, name, "Trace_enabled", mctraceenabled ? "yes" : "no");
  mcfile_tag(f, format, pre, name, "Default_main", mcdefaultmain ? "yes" : "no");
  mcfile_tag(f, format, pre, name, "Embedded_runtime", 
#ifdef MC_EMBEDDED_RUNTIME
         "yes"
#else
         "no"
#endif
         );
} /* mcinfo_instrument */

void mcinfo_simulation(FILE *f, struct mcformats_struct format, 
  char *pre, char *name) 
{
  int i;
  double run_num, ncount;
  time_t t;
  char Value[256];
  
  if (!f) return;
    
  run_num = mcget_run_num();
  ncount  = mcget_ncount();
  time(&t);
  strncpy(Value, ctime(&t), 256); if (strlen(Value)) Value[strlen(Value)-1] = '\0';
  mcfile_tag(f, format, pre, name, "Date", Value); 
  if (run_num == 0 || run_num == ncount) sprintf(Value, "%g", ncount);
  else sprintf(Value, "%g/%g", run_num, ncount);
  mcfile_tag(f, format, pre, name, "Ncount", Value);
  mcfile_tag(f, format, pre, name, "Trace", mcdotrace ? "yes" : "no");
  mcfile_tag(f, format, pre, name, "Gravitation", mcgravitation ? "yes" : "no");
  if(mcseed)
  {
    sprintf(Value, "%ld", mcseed);
    mcfile_tag(f, format, pre, name, "Seed", Value);
  }
  if (strstr(format.Name, "McStas"))
  {
    for(i = 0; i < mcnumipar; i++)
    {
      (*mcinputtypes[mcinputtable[i].type].printer)(Value, mcinputtable[i].par);
      fprintf(f, "%sParam: %s=%s", pre, mcinputtable[i].name, Value);
      fprintf(f, "\n");
    }   
  }
  else
  {
    mcfile_section(f, format, "begin", pre, "parameters", "parameters", name, 3);
    for(i = 0; i < mcnumipar; i++)
    {
      (*mcinputtypes[mcinputtable[i].type].printer)(Value, mcinputtable[i].par);
      mcfile_tag(f, format, pre, "parameters", mcinputtable[i].name, Value);
    }  
    mcfile_section(f, format, "end", pre, "parameters", "parameters", name, 3);
  }
} /* mcinfo_simulation */

static void mcinfo_data(FILE *f, struct mcformats_struct format, 
  char *pre, char *parent, char *title,
  int m, int n, int p,
  char *xlabel, char *ylabel, char *zlabel, 
  char *xvar, char *yvar, char *zvar, 
  double x1, double x2, double y1, double y2, double z1, double z2, 
  char *filename,
  double *p0, double *p1, double *p2, char istransposed)
{
  char type[256];
  char stats[256];
  char vars[256];
  char signal[256];
  char values[256];
  char limits[256];
  char lim_field[10];
  char c[32];
  double run_num, ncount;
  char ratio[256];
  
  double sum_xz  = 0;
  double sum_yz  = 0;
  double sum_z   = 0;
  double sum_y   = 0;
  double sum_x   = 0;
  double sum_x2z = 0;
  double sum_y2z = 0;
  double min_z   = 0;
  double max_z   = 0;
  double fmon_x=0, smon_x=0, fmon_y=0, smon_y=0, mean_z=0;
  double Nsum=0;
  double P2sum=0;
  
  int    i,j;
  
  if (!f || m*n*p == 0) return;
  
  if (p1)
  {
    min_z   = p1[0];
    max_z   = min_z;
    for(j = 0; j < n*p; j++)
    {
      for(i = 0; i < m; i++)
      {
        double x,y,z;
        double N, E;
        long index;

        if (!istransposed) index = i*n*p + j;
        else index = i+j*m;
        if (p0) N = p0[index];
        if (p2) E = p2[index];

        if (m) x = x1 + (i + 0.5)/m*(x2 - x1); else x = 0;
        if (n) y = y1 + (j + 0.5)/n/p*(y2 - y1); else y = 0;
        z = p1[index];
        sum_xz += x*z;
        sum_yz += y*z;
        sum_x += x;
        sum_y += y;
        sum_z += z;
        sum_x2z += x*x*z;
        sum_y2z += y*y*z;
        if (z > max_z) max_z = z;
        if (z < min_z) min_z = z;

        Nsum += p0 ? N : 1;
        P2sum += p2 ? E : z*z;
      }
    }
    if (sum_z && n*m*p)
    {
      fmon_x = sum_xz/sum_z; 
      fmon_y = sum_yz/sum_z;
      smon_x = sqrt(sum_x2z/sum_z-fmon_x*fmon_x);
      smon_y = sqrt(sum_y2z/sum_z-fmon_y*fmon_y);
      mean_z = sum_z/n/m/p;
    }
  }
  
  if (m*n*p == 1) 
  { strcpy(type, "array_0d"); strcpy(stats, ""); }
  else if (n == 1 || m == 1) 
  { if (m == 1) {m = n; n = 1; }
    sprintf(type, "array_1d(%d)", m); 
    sprintf(stats, "X0=%g; dX=%g;", fmon_x, smon_x); }
  else  
  { if (p == 1) sprintf(type, "array_2d(%d, %d)", m, n); 
    else sprintf(type, "array_3d(%d, %d, %d)", m, n, p); 
    sprintf(stats, "X0=%g; dX=%g; Y0=%g; dY=%g;", fmon_x, smon_x, fmon_y, smon_y); }
  strcpy(c, "I ");
  if (zvar && strlen(zvar)) strncpy(c, zvar,32);
  else if (yvar && strlen(yvar)) strncpy(c, yvar,32);
  else if (xvar && strlen(xvar)) strncpy(c, xvar,32);
  else strncpy(c, xvar,32);
  if (m == 1 || n == 1) sprintf(vars, "%s %s %s_err N", xvar, c, c);
  else sprintf(vars, "%s %s_err N", c, c);

  run_num = mcget_run_num();
  ncount  = mcget_ncount();
  sprintf(ratio, "%g/%g", run_num, ncount);
  
  mcfile_tag(f, format, pre, parent, "type", type);
  if (parent) mcfile_tag(f, format, pre, parent, (strstr(format.Name,"McStas") ? "component" : "parent"), parent);
  if (title) mcfile_tag(f, format, pre, parent, "title", title);
  mcfile_tag(f, format, pre, parent, "variables", vars);
  mcfile_tag(f, format, pre, parent, "ratio", ratio);
  if (filename) {
    mcfile_tag(f, format, pre, parent, "filename", filename);
    mcfile_tag(f, format, pre, parent, "format", format.Name);
  } else mcfile_tag(f, format, pre, parent, "filename", "");
  
  if (p1)
  {
    if (n*m*p > 1) 
    {
      sprintf(signal, "Min=%g; Max=%g; Mean= %g;", min_z, max_z, mean_z); 
      if (y1 == 0 && y2 == 0) { y1 = min_z; y2 = max_z;}
      else if (z1 == 0 && z2 == 0) { z1 = min_z; z2 = max_z;}
    } else strcpy(signal, "");

    mcfile_tag(f, format, pre, parent, "statistics", stats);
    mcfile_tag(f, format, pre, parent, "signal", signal);

    sprintf(values, "%g %g %g", sum_z, mcestimate_error(Nsum, sum_z, P2sum), Nsum);
    mcfile_tag(f, format, pre, parent, "values", values);
  }
  strcpy(lim_field, "xylimits");
  if (n*m > 1) 
  {
    mcfile_tag(f, format, pre, parent, "xvar", xvar);
    mcfile_tag(f, format, pre, parent, "yvar", yvar);
    mcfile_tag(f, format, pre, parent, "xlabel", xlabel);
    mcfile_tag(f, format, pre, parent, "ylabel", ylabel);
    if ((n == 1 || m == 1) && strstr(format.Name, "McStas"))
    {
      sprintf(limits, "%g %g", x1, x2);
      strcpy(lim_field, "xlimits");
    }
    else
    {
      mcfile_tag(f, format, pre, parent, "zvar", zvar);
      mcfile_tag(f, format, pre, parent, "zlabel", zlabel);
      sprintf(limits, "%g %g %g %g %g %g", x1, x2, y1, y2, z1, z2);
    }
  } else strcpy(limits, "0 0 0 0 0 0");
  mcfile_tag(f, format, pre, parent, lim_field, limits);
} /* mcinfo_data */

/* main output function, works for 0d, 1d, 2d data */

static void
mcsiminfo_init(FILE *f)
{
  char info_name[256];
  
  if (mcdisable_output_files) return;
  if (!f && (!mcsiminfo_name || !strlen(mcsiminfo_name))) return;
  if (!strchr(mcsiminfo_name,'.')) sprintf(info_name, "%s.%s", mcsiminfo_name, mcformat.Extension); else strcpy(info_name, mcsiminfo_name);
  if (!f) mcsiminfo_file = mcnew_file(info_name, "w");
  else mcsiminfo_file = f;
  if(!mcsiminfo_file)
    fprintf(stderr,
            "Warning: could not open simulation description file '%s'\n",
            info_name);
  else
  {
    char pre[20];
    int  ismcstas;
    char simname[1024];
    char root[10];
    
    strcpy(pre, "");
    ismcstas = (strstr(mcformat.Name, "McStas") != NULL);
    if (strstr(mcformat.Name, "XML") == NULL && strstr(mcformat.Name, "NeXus") == NULL) strcpy(root, "mcstas");
    else strcpy(root, "root");
    if (mcdirname) sprintf(simname, "%s%s%s", mcdirname, MC_PATHSEP_S, mcsiminfo_name); else sprintf(simname, "%s%s%s", ".", MC_PATHSEP_S, mcsiminfo_name);
    
    mcfile_header(mcsiminfo_file, mcformat, "header", pre, simname, root);
    mcfile_section(mcsiminfo_file, mcformat, "begin", pre, mcinstrument_name, "instrument", root, 1);
    mcinfo_instrument(mcsiminfo_file, mcformat, pre, mcinstrument_name);
    if (ismcstas) mcfile_section(mcsiminfo_file, mcformat, "end", pre, mcinstrument_name, "instrument", root, 1);
    mcfile_section(mcsiminfo_file, mcformat, "begin", pre, simname, "simulation", mcinstrument_name, 2);
    mcinfo_simulation(mcsiminfo_file, mcformat, pre, simname);
    if (ismcstas) mcfile_section(mcsiminfo_file, mcformat, "end", pre, simname, "simulation", mcinstrument_name, 2);
  }
} /* mcsiminfo_init */

static void
mcsiminfo_close(void)
{
  if(mcsiminfo_file)
  {
    int  ismcstas;
    char simname[1024];
    char root[10];
    char pre[10];
    
    strcpy(pre, "  ");
    ismcstas = (strstr(mcformat.Name, "McStas") != NULL);
    if (mcdirname) sprintf(simname, "%s%s%s", mcdirname, MC_PATHSEP_S, mcsiminfo_name); else sprintf(simname, "%s%s%s", ".", MC_PATHSEP_S, mcsiminfo_name);
    if (strstr(mcformat.Name, "XML") == NULL && strstr(mcformat.Name, "NeXus") == NULL) strcpy(root, "mcstas"); else strcpy(root, "root");
    
    if (!ismcstas) 
    {
      mcfile_section(mcsiminfo_file, mcformat, "end", pre, simname, "simulation", mcinstrument_name, 2);
      mcfile_section(mcsiminfo_file, mcformat, "end", pre, mcinstrument_name, "instrument", root, 1);
    }
    mcfile_header(mcsiminfo_file, mcformat, "footer", pre, simname, root);
    
    if (mcsiminfo_file != stdout) fclose(mcsiminfo_file);
    mcsiminfo_file = NULL;
  }
} /* mcsiminfo_close */

/* mcfile_datablock: output a single data block using specific file format.
 * 'part' can be 'data','errors','ncount'
 * if y1 == y2 == 0 and McStas format, then stores as a 1D array with [I,E,N]
 * return value: 0=0d/2d, 1=1d
 * when !single_file, create independent data files, with header and data tags
 * if one of the dimensions m,n,p is negative, the data matrix will be written
 * after transposition of m/x and n/y dimensions
 */

static int mcfile_datablock(FILE *f, struct mcformats_struct format, 
  char *pre, char *parent, char *part,
  double *p0, double *p1, double *p2, int m, int n, int p,
  char *xlabel, char *ylabel, char *zlabel, char *title,
  char *xvar, char *yvar, char *zvar,
  double x1, double x2, double y1, double y2, double z1, double z2, 
  char *filename, char istransposed)
{
  char *Begin;
  char *End;
  char valid_xlabel[64];
  char valid_ylabel[64];
  char valid_zlabel[64];
  char valid_parent[64];
  FILE *datafile= NULL;
  int  isdata=0;
  int  just_header=0;
  int  i,j, is1d;
  double Nsum=0, Psum=0, P2sum=0;
  char sec[256];
  char isdata_present;
  
  if (strstr(part,"data")) 
  { isdata = 1; Begin = format.BeginData; End = format.EndData; }
  if (strstr(part,"errors")) 
  { isdata = 2; Begin = format.BeginErrors; End = format.EndErrors; }
  if (strstr(part,"ncount")) 
  { isdata = 0; Begin = format.BeginNcount; End = format.EndNcount; }
  if (strstr(part, "begin")) just_header = 1;
  if (strstr(part, "end"))   just_header = 2;
  
  isdata_present=((isdata==1 && p1) || (isdata==2 && p2) || (isdata==0 && p0));
  
  is1d = (!y1 && y1 == y2 && strstr(format.Name,"McStas"));
  mcvalid_name(valid_xlabel, xlabel, 64);
  mcvalid_name(valid_ylabel, ylabel, 64);
  mcvalid_name(valid_zlabel, zlabel, 64);
  
  if (strstr(format.Name, "McStas") || !filename || strlen(filename) == 0) 
    mcvalid_name(valid_parent, parent, 64);
  else mcvalid_name(valid_parent, filename, 64);
  
  /* if normal or begin and part == data: output info_data (sim/data_file) */
  if (isdata == 1 && just_header != 2 && f)
  {
    mcinfo_data(f, format, pre, valid_parent, title, m, n, p,
          xlabel, ylabel, zlabel, xvar, yvar, zvar, 
          x1, x2, y1, y2, z1, z2, filename, p0, p1, p2, istransposed);
  }

  /* if normal or begin: begin part (sim/data file) */
  if (strlen(Begin) && just_header != 2 && f)
    pfprintf(f, Begin, "ssssssssssssslllgggggg",
      pre,          /* %1$s */
      valid_parent, /* %2$s */
      title,        /* %3$s */
      filename,     /* %4$s */
      xlabel,       /* %5$s */
      valid_xlabel, /* %6$s*/
      ylabel,       /* %7$s */
      valid_ylabel, /* %8$s */
      zlabel,       /* %9$s*/
      valid_zlabel, /* %10$s*/
      xvar,         /* %11$s */
      yvar,         /* %12$s */
      zvar,         /* %13$s */
      m,            /* %14$li */
      n,            /* %15$li */
      p,            /* %16$li */
      x1,           /* %17$g */
      x2,           /* %18$g */
      y1,           /* %19$g*/
      y2,           /* %20$g */
      z1,           /* %21$g */
      z2);          /* %22$g */
      
 /* if normal, and !single:
  *   open datafile, 
  *   if !ascii_only
  *     if data: write file header, 
  *     call datablock part+header(begin)
  * else data file = f
  */
  if (!mcsingle_file && just_header == 0)
  {
    /* if data: open new file for data else append for error/ncount */
    if (filename) datafile = mcnew_file(filename, 
      (isdata != 1 || strstr(format.Name, "append") ? "a" : "w"));
    else datafile = NULL;
    /* special case of IDL: can not have empty vectors. Init to 'empty' */
    if (strstr(format.Name, "IDL") && f) fprintf(f, "'external'");
    /* if data, start with root header plus tags of parent data */
    if (datafile && !mcascii_only) 
    { 
      char mode[32];
      if (isdata == 1) mcfile_header(datafile, format, "header",
          (strstr(format.Name, "McStas") ? "# " : ""), 
          filename, valid_parent); 
      sprintf(mode, "%s begin", part);
      /* write header+data block begin tags into datafile */
      mcfile_datablock(datafile, format, 
          (strstr(format.Name, "McStas") ? "# " : ""), 
          valid_parent, mode,
          p0, p1, p2, m, n, p,
          xlabel,  ylabel, zlabel, title,
          xvar, yvar, zvar,
          x1, x2, y1, y2, z1, z2, filename, istransposed);
      
      
    }
  }
  else if (just_header == 0)
  {
    if (strstr(format.Name, "McStas") && m*n*p>1 && f) 
    {
      if (is1d) sprintf(sec,"array_1d(%d)", m);
      else if (p==1) sprintf(sec,"array_2d(%d,%d)", m,n);
      else sprintf(sec,"array_3d(%d,%d,%d)", m,n,p);
      fprintf(f,"%sbegin %s\n", pre, sec);
      datafile = f;
    }
    if (mcsingle_file) datafile = f;
  }
  
  /* if normal: [data] in data file */
  /* do loops: 2 loops on m,n. */
  if (just_header == 0)
  {
    char eol_char[3];
    int  isIDL, isPython;
    int  isBinary=0;
    
    if (strstr(format.Name, "binary float")) isBinary=1;
    else if (strstr(format.Name, "binary double")) isBinary=2;
    isIDL    = (strstr(format.Name, "IDL") != NULL);
    isPython = (strstr(format.Name, "Python") != NULL);
    if (isIDL) strcpy(eol_char,"$\n"); else strcpy(eol_char,"\n");
      
    for(j = 0; j < n*p; j++)  /* loop on rows(y) */
    {
      if(datafile && !isBinary)
        fprintf(datafile,"%s", pre);
      for(i = 0; i < m; i++)  /* write all columns (x) */
      {
        double I=0, E=0, N=0;
        double value=0;
        long index;

        if (!istransposed) index = i*n*p + j;
        else index = i+j*m;
        if (p0) N = p0[index];
        if (p1) I = p1[index];
        if (p2) E = p2[index];

        Nsum += p0 ? N : 1;
        Psum += I;
        P2sum += p2 ? E : I*I;

        if (p0 && p1 && p2) E = mcestimate_error(N,I,E);
        if(datafile && !isBinary && isdata_present)
        {
          if (isdata == 1) value = I;
          else if (isdata == 0) value = N;
          else if (isdata == 2) value = E;
          if (is1d) 
          {
            double x;
            
            x = x1+(x2-x1)*(index)/(m*n*p);
            if (m*n*p > 1) fprintf(datafile, "%g %g %g %g\n", x, I, E, N);
          }
          else 
          {
            fprintf(datafile, "%g", value);
            if ((isIDL || isPython) && ((i+1)*(j+1) < m*n*p)) fprintf(datafile, ","); 
            else fprintf(datafile, " ");
          }
        }
      }
      if (datafile && !isBinary && isdata_present) fprintf(datafile, eol_char);
    } /* end 2 loops if not Binary */
    if (datafile && isBinary)
    {
      double *d=NULL;
      if (isdata==1) d=p1;
      else if (isdata==2) d=p2;
      else if (isdata==0) d=p0;

      if (d && isBinary == 1)  /* float */
      {
        float *s;
        s = (float*)malloc(m*n*p*sizeof(float));
        if (s) 
        {
          long    i, count;
          for (i=0; i<m*n*p; i++)
            { if (isdata != 2) s[i] = (float)d[i]; 
              else s[i] = (float)mcestimate_error(p0[i],p1[i],p2[i]); }
          count = fwrite(s, sizeof(float), m*n*p, datafile);
          if (count != m*n*p) fprintf(stderr, "McStas: error writing float binary file '%s' (%li instead of %li).\n", filename,count, (long)m*n*p);
          free(s);
        } else fprintf(stderr, "McStas: Out of memory for writing float binary file '%s'.\n", filename);
      }
      else if (d && isBinary == 2)  /* double */
      {
        long count;
        double *s=NULL;
        if (isdata == 2) 
        { 
          s = (double*)malloc(m*n*p*sizeof(double));
          if (s) { long i;
            for (i=0; i<m*n*p; i++)
              s[i] = (double)mcestimate_error(p0[i],p1[i],p2[i]);
            d = s;
          }
          else fprintf(stderr, "McStas: Out of memory for writing 'errors' part of double binary file '%s'.\n", filename);
        }
        count = fwrite(d, sizeof(double), m*n*p, datafile);
        if (isdata == 2 && s) free(s);
        if (count != m*n*p) fprintf(stderr, "McStas: error writing double binary file '%s' (%li instead of %li).\n", filename,count, (long)m*n*p);
      }
    } /* end if Binary */
  }
  if (strstr(format.Name, "McStas") || !filename || strlen(filename) == 0) 
    mcvalid_name(valid_parent, parent, 64);
  else mcvalid_name(valid_parent, filename, 64);
  /* if normal or end: end_data */
  if (strlen(End) && just_header != 1 && f)
  {
    pfprintf(f, End, "ssssssssssssslllgggggg",
      pre,          /* %1$s */
      valid_parent, /* %2$s */
      title,        /* %3$s */
      filename,     /* %4$s */
      xlabel,       /* %5$s */
      valid_xlabel, /* %6$s*/
      ylabel,       /* %7$s */
      valid_ylabel, /* %8$s */
      zlabel,       /* %9$s*/
      valid_zlabel, /* %10$s*/
      xvar,         /* %11$s */
      yvar,         /* %12$s */
      zvar,         /* %13$s */
      m,            /* %14$li */
      n,            /* %15$li */
      p,            /* %16$li */
      x1,           /* %17$g */
      x2,           /* %18$g */
      y1,           /* %19$g*/
      y2,           /* %20$g */
      z1,           /* %21$g */
      z2);          /* %22$g */
  }
      
 /* if normal and !single and datafile: 
  *   datablock part+footer
  *   write file footer
  *   close datafile
  */
  if (!mcsingle_file && just_header == 0)
  {
    char mode[32];

    if (datafile && datafile != f && !mcascii_only)
    {
      sprintf(mode, "%s end", part);
      /* write header+data block end tags into datafile */
      mcfile_datablock(datafile, format, 
          (strstr(format.Name, "McStas") ? "# " : ""),
          valid_parent, mode,
          p0, p1, p2, m, n, p,
          xlabel,  ylabel, zlabel, title,
          xvar, yvar, zvar,
          x1, x2, y1, y2, z1, z2, filename, istransposed);
      if ((isdata == 1 && is1d) || strstr(part,"ncount") || !p0 || !p2) /* either ncount, or 1d */
        if (!strstr(format.Name, "partial"))
          mcfile_header(datafile, format, "footer", 
          (strstr(format.Name, "McStas") ? "# " : ""),
          filename, valid_parent);
    }
    if (datafile) fclose(datafile); 
  }
  else
  {
    if (strstr(format.Name, "McStas") && just_header == 0 && m*n*p > 1) 
      fprintf(f,"%send %s\n", pre, sec);
  }
      
  /* set return value */      
  return(is1d);
} /* mcfile_datablock */

/* mcfile_data: output data/errors/ncounts using specific file format.
 * if McStas 1D then data is stored
 * as a long 1D array [p0, p1, p2] to reorder -> don't output err/ncount again.
 * if p1 or p2 is NULL then skip that part.
 */
static int mcfile_data(FILE *f, struct mcformats_struct format, 
  char *pre, char *parent, 
  double *p0, double *p1, double *p2, int m, int n, int p,
  char *xlabel, char *ylabel, char *zlabel, char *title,
  char *xvar, char *yvar, char *zvar,
  double x1, double x2, double y1, double y2, double z1, double z2,
  char *filename, char istransposed)
{
  int is1d;
  
  /* return if f,n,m,p1 NULL */
  if ((m*n*p == 0) || !p1) return (-1);
  
  /* output data block */
  is1d = mcfile_datablock(f, format, pre, parent, "data",
    p0, p1, p2, m, n, p,
    xlabel,  ylabel, zlabel, title,
    xvar, yvar, zvar,
    x1, x2, y1, y2, z1, z2, filename, istransposed);
  /* return if 1D data */
  if (is1d) return(is1d);
  /* output error block and p2 non NULL */
  if (p0 && p2) mcfile_datablock(f, format, pre, parent, "errors",
    p0, p1, p2, m, n, p,
    xlabel,  ylabel, zlabel, title,
    xvar, yvar, zvar,
    x1, x2, y1, y2, z1, z2, filename, istransposed);
  /* output ncount block and p0 non NULL */
  if (p0 && p2) mcfile_datablock(f, format, pre, parent, "ncount",
    p0, p1, p2, m, n, p,
    xlabel,  ylabel, zlabel, title,
    xvar, yvar, zvar,
    x1, x2, y1, y2, z1, z2, filename, istransposed);
  
  return(is1d);
} /* mcfile_data */

double
mcdetector_out(char *cname, double p0, double p1, double p2, char *filename)
{
  printf("Detector: %s_I=%g %s_ERR=%g %s_N=%g",
         cname, p1, cname, mcestimate_error(p0,p1,p2), cname, p0);
  if(filename && strlen(filename))
    printf(" \"%s\"", filename);
  printf("\n");
  return(p0);
}

/* parent is the component name */

static double mcdetector_out_012D(struct mcformats_struct format, 
  char *pre, char *parent, char *title,
  int m, int n,  int p,
  char *xlabel, char *ylabel, char *zlabel, 
  char *xvar, char *yvar, char *zvar, 
  double x1, double x2, double y1, double y2, double z1, double z2, 
  char *filename,
  double *p0, double *p1, double *p2)
{  
  char simname[512];
  int i,j;
  double Nsum=0, Psum=0, P2sum=0;
  FILE *local_f=NULL;
  char istransposed=0;
  
  if (m<0 || n<0 || p<0 || strstr(format.Name, "binary"))  /* do the swap once for all */
  { 
    double tmp1, tmp2;
    char   *lab;
    istransposed = 1; 
    tmp1=x1; tmp2=x2;
    x1=y1; x2=y2; y1=tmp1; y2=tmp2;
    lab = xlabel; xlabel=ylabel; ylabel=lab;
    lab = xvar; xvar=yvar; yvar=lab;
    i=m; m=abs(n); n=abs(i); p=abs(p); 
  }

  if (!strstr(format.Name,"partial")) local_f = mcsiminfo_file;
  if (mcdirname) sprintf(simname, "%s%s%s", mcdirname, MC_PATHSEP_S, mcsiminfo_name); else sprintf(simname, "%s%s%s", ".", MC_PATHSEP_S, mcsiminfo_name);
  
  mcfile_section(local_f, format, "begin", pre, parent, "component", simname, 3);
  mcfile_section(local_f, format, "begin", pre, filename, "data", parent, 4);
  mcfile_data(local_f, format, 
    pre, parent, 
    p0, p1, p2, m, n, p,
    xlabel, ylabel, zlabel, title,
    xvar, yvar, zvar, 
    x1, x2, y1, y2, z1, z2, filename, istransposed);
  
  mcfile_section(local_f, format, "end", pre, filename, "data", parent, 4);
  mcfile_section(local_f, format, "end", pre, parent, "component", simname, 3);

  if (local_f)
  {
    for(j = 0; j < n*p; j++)
    {
      for(i = 0; i < m; i++)
      {
        double N,I,E;
        int index;
        if (!istransposed) index = i*n*p + j;
        else index = i+j*m;
        if (p0) N = p0[index];
        if (p1) I = p1[index];
        if (p2) E = p2[index];

        Nsum += p0 ? N : 1;
        Psum += I;
        P2sum += p2 ? E : I*I;
      }
    }
    /* give 0D detector output. */
    mcdetector_out(parent, Nsum, Psum, P2sum, filename);
  }
  return(Psum);
} /* mcdetector_out_012D */

void mcheader_out(FILE *f,char *parent,
  int m, int n, int p,
  char *xlabel, char *ylabel, char *zlabel, char *title,
  char *xvar, char *yvar, char *zvar,
  double x1, double x2, double y1, double y2, double z1, double z2, 
  char *filename)
{
  int  loc_single_file;
  char pre[3];
  char simname[512];
  loc_single_file = mcsingle_file; mcsingle_file = 1;
  
  if (!strstr(mcformat.Name, "McStas")) strcpy(pre,""); else strcpy(pre,"# ");
  
  mcfile_header(f, mcformat, "header", pre, mcinstrument_name, "mcstas");
  mcinfo_instrument(f, mcformat, pre, mcinstrument_name);
  if (mcdirname) sprintf(simname, "%s%s%s", mcdirname, MC_PATHSEP_S, mcsiminfo_name); else sprintf(simname, "%s%s%s", ".", MC_PATHSEP_S, mcsiminfo_name);

  mcfile_datablock(f, mcformat, 
    pre, parent, "data",
    NULL,NULL,NULL, m, n, p,
    xlabel, ylabel, zlabel, title,
    xvar, yvar, zvar, x1,  x2,  y1,  y2,  z1,  z2, 
    filename, 0);
  
  mcsingle_file = loc_single_file;
  mcfile_header(f, mcformat, "footer", pre, mcinstrument_name, "mcstas");
}


double mcdetector_out_0D(char *t, double p0, double p1, double p2, char *c)
{
  char pre[20];
  
  strcpy(pre, "");
  return(mcdetector_out_012D(mcformat, 
    pre, c, t,
    1, 1, 1,
    "I", "", "", 
    "I", "", "", 
    0, 0, 0, 0, 0, 0, NULL,
    &p0, &p1, &p2));
}

double mcdetector_out_1D(char *t, char *xl, char *yl,
                  char *xvar, double x1, double x2, int n,
                  double *p0, double *p1, double *p2, char *f, char *c)
{
  char pre[20];
  
  strcpy(pre, "");
  return(mcdetector_out_012D(mcformat, 
    pre, c, t,
    n, 1, 1,
    xl, yl, "Intensity", 
    xvar, "(I,I_err)", "I", 
    x1, x2, x1, x2, 0, 0, f,
    p0, p1, p2));
}

double mcdetector_out_2D(char *t, char *xl, char *yl,
                  double x1, double x2, double y1, double y2, int m,
                  int n, double *p0, double *p1, double *p2, char *f, char *c)
{
  char xvar[3];
  char yvar[3];
  char pre[20];
  
  strcpy(pre, ""); strcpy(xvar, "x "); strcpy(yvar, "y ");
  if (xl && strlen(xl)) strncpy(xvar, xl, 2);
  if (yl && strlen(yl)) strncpy(yvar, yl, 2);
  
  return(mcdetector_out_012D(mcformat, 
    pre, c, t,
    m, n, 1,
    xl, yl, "Intensity", 
    xvar, yvar, "I", 
    x1, x2, y1, y2, 0, 0, f,
    p0, p1, p2));
}

double mcdetector_out_3D(char *t, char *xl, char *yl, char *zl,
      char *xvar, char *yvar, char *zvar,
                  double x1, double x2, double y1, double y2, double z1, double z2, int m,
                  int n, int p, double *p0, double *p1, double *p2, char *f, char *c)
{
  char pre[20];
  
  strcpy(pre, "");
  return(mcdetector_out_012D(mcformat, 
    pre, c, t,
    m, n, p,
    xl, yl, zl, 
    xvar, yvar, zvar, 
    x1, x2, y1, y2, z1, z2, f,
    p0, p1, p2));
}
 
/* end of file i/o functions */



static void
mcuse_dir(char *dir)
{
#ifdef MC_PORTABLE
  fprintf(stderr, "Error: "
          "Directory output cannot be used with portable simulation.\n");
  exit(1);
#else  /* !MC_PORTABLE */
  if(mkdir(dir, 0777))
  {
    fprintf(stderr, "Error: unable to create directory '%s'.\n", dir);
    fprintf(stderr, "(Maybe the directory already exists?)\n");
    exit(1);
  }
  mcdirname = dir;
#endif /* !MC_PORTABLE */
}

static void
mcuse_file(char *file)
{
  mcsiminfo_name = file;
  mcsingle_file = 1;
}

void mcuse_format(char *format)
{
  int i;
  int i_format=-1;
  char *tmp;
  
  /* look for a specific format in mcformats.Name table */
  for (i=0; i < mcNUMFORMATS; i++)
  {
    if (strstr(format, mcformats[i].Name)) i_format = i;
  }
  if (i_format < 0)
  {
    i_format = 0; /* default format is #0 McStas */
    fprintf(stderr, "Warning: unknown output format %s. Using default (%s).\n", format, mcformats[i_format].Name);
  }

  mcformat = mcformats[i_format];
  tmp = malloc(256);
  if(!tmp) exit(fprintf(stderr, "Error: insufficient memory (mcuse_format)\n"));
  strcpy(tmp, mcformat.Name); 
  mcformat.Name = tmp;
  if (strstr(format,"binary"))
  {
    if (strstr(format,"double")) strcat(mcformat.Name," binary double data");
    else if (strstr(format,"NeXus")) strcat(mcformat.Name," binary NeXus data");
    else strcat(mcformat.Name," binary float data");
  }
} /* mcuse_format */

static void
mcinfo(void)
{
  mcsiminfo_init(stdout);
  mcsiminfo_close();
  exit(0);
}

void
mcparseoptions(int argc, char *argv[])
{
  int i, j;
  char *p;
  int paramset = 0, *paramsetarray;

  /* Add one to mcnumipar to aviod allocating zero size memory block. */
  paramsetarray = malloc((mcnumipar + 1)*sizeof(*paramsetarray));
  if(paramsetarray == NULL)
  {
    fprintf(stderr, "Error: insufficient memory (mcparseoptions)\n");
    exit(1);
  }
  for(j = 0; j < mcnumipar; j++)
    { paramsetarray[j] = 0; 
      (*mcinputtypes[mcinputtable[j].type].getparm)
                   (NULL, mcinputtable[j].par); }

  for(i = 1; i < argc; i++)
  {
    if(!strcmp("-s", argv[i]) && (i + 1) < argc)
      mcsetseed(argv[++i]);
    else if(!strncmp("-s", argv[i], 2))
      mcsetseed(&argv[i][2]);
    else if(!strcmp("--seed", argv[i]) && (i + 1) < argc)
      mcsetseed(argv[++i]);
    else if(!strncmp("--seed=", argv[i], 7))
      mcsetseed(&argv[i][7]);
    else if(!strcmp("-n", argv[i]) && (i + 1) < argc)
      mcsetn_arg(argv[++i]);
    else if(!strncmp("-n", argv[i], 2))
      mcsetn_arg(&argv[i][2]);
    else if(!strcmp("--ncount", argv[i]) && (i + 1) < argc)
      mcsetn_arg(argv[++i]);
    else if(!strncmp("--ncount=", argv[i], 9))
      mcsetn_arg(&argv[i][9]);
    else if(!strcmp("-d", argv[i]) && (i + 1) < argc)
      mcuse_dir(argv[++i]);
    else if(!strncmp("-d", argv[i], 2))
      mcuse_dir(&argv[i][2]);
    else if(!strcmp("--dir", argv[i]) && (i + 1) < argc)
      mcuse_dir(argv[++i]);
    else if(!strncmp("--dir=", argv[i], 6))
      mcuse_dir(&argv[i][6]);
    else if(!strcmp("-f", argv[i]) && (i + 1) < argc)
      mcuse_file(argv[++i]);
    else if(!strncmp("-f", argv[i], 2))
      mcuse_file(&argv[i][2]);
    else if(!strcmp("--file", argv[i]) && (i + 1) < argc)
      mcuse_file(argv[++i]);
    else if(!strncmp("--file=", argv[i], 7))
      mcuse_file(&argv[i][7]);
    else if(!strcmp("-h", argv[i]))
      mcshowhelp(argv[0]);
    else if(!strcmp("--help", argv[i]))
      mcshowhelp(argv[0]);
    else if(!strcmp("-i", argv[i]))
      mcinfo();
    else if(!strcmp("--info", argv[i]))
      mcinfo();
    else if(!strcmp("-t", argv[i]))
      mcenabletrace();
    else if(!strcmp("--trace", argv[i]))
      mcenabletrace();
    else if(!strcmp("-a", argv[i]))
      mcascii_only = 1;
    else if(!strcmp("--data-only", argv[i]))
      mcascii_only = 1;
    else if(!strcmp("--gravitation", argv[i]))
      mcgravitation = 1;
    else if(!strcmp("-g", argv[i]))
      mcgravitation = 1;
    else if(!strncmp("--format=", argv[i], 9))
      mcuse_format(&argv[i][9]);
    else if(!strcmp("--format", argv[i]) && (i + 1) < argc)
      mcuse_format(argv[++i]);
    else if(!strcmp("--no-output-files", argv[i]))  
      mcdisable_output_files = 1;
    else if(argv[i][0] != '-' && (p = strchr(argv[i], '=')) != NULL)
    {
      *p++ = '\0';
      for(j = 0; j < mcnumipar; j++)
        if(!strcmp(mcinputtable[j].name, argv[i]))
        {
          int status;
          status = (*mcinputtypes[mcinputtable[j].type].getparm)(p,
                        mcinputtable[j].par);
          if(!status)
          {
            (*mcinputtypes[mcinputtable[j].type].error)
              (mcinputtable[j].name, p);
            exit(1);
          }
          paramsetarray[j] = 1;
          paramset = 1;
          break;
        }
      if(j == mcnumipar)
      {                                /* Unrecognized parameter name */
        fprintf(stderr, "Error: unrecognized parameter %s\n", argv[i]);
        exit(1);
      }
    }
    else
      mcusage(argv[0]);
  }
  if (!mcascii_only) {
    if (strstr(mcformat.Name,"binary")) fprintf(stderr, "Warning: %s files will contain text headers. Use -a option to clean up.\n", mcformat.Name);
    strcat(mcformat.Name, " with text headers");
  }
  if(!paramset)
    mcreadparams();                /* Prompt for parameters if not specified. */
  else
  {
    for(j = 0; j < mcnumipar; j++)
      if(!paramsetarray[j])
      {
        fprintf(stderr, "Error: Instrument parameter %s left unset\n",
                mcinputtable[j].name);
        exit(1);
      }
  }
  free(paramsetarray);
}

#ifndef MAC
#ifndef WIN32
/* This is the signal handler that makes simulation stop, and save results */
void sighandler(int sig)
{
  /* MOD: E. Farhi, Sep 20th 2001: give more info */
  time_t t1;

  printf("\n# McStas: [pid %i] Signal %i detected", getpid(), sig);
  if (!strcmp(mcsig_message, "sighandler") && (sig != SIGUSR1) && (sig != SIGUSR2))
  {
    printf("\n# Fatal : unrecoverable loop ! Suicide (naughty boy).\n"); 
    kill(0, SIGKILL); /* kill myself if error occurs within sighandler: loops */
  }
  switch (sig) {
    case SIGINT : printf(" SIGINT (interrupt from terminal, Ctrl-C)"); break;
    case SIGQUIT : printf(" SIGQUIT (quit from terminal)"); break;
    case SIGABRT : printf(" SIGABRT (abort)"); break;
    case SIGTRAP : printf(" SIGTRAP (trace trap)"); break;
    case SIGTERM : printf(" SIGTERM (termination)"); break;
    case SIGPIPE : printf(" SIGPIPE (broken pipe)"); break;
    case SIGUSR1 : printf(" SIGUSR1 (Display info)"); break;
    case SIGUSR2 : printf(" SIGUSR2 (Save simulation)"); break;
    case SIGILL  : printf(" SIGILL (Illegal instruction)"); break;
    case SIGFPE  : printf(" SIGFPE (Math Error)"); break;
    case SIGBUS  : printf(" SIGBUS (bus error)"); break;
    case SIGSEGV : printf(" SIGSEGV (Mem Error)"); break;
    case SIGURG  : printf(" SIGURG (urgent socket condition)"); break;
    default : printf(" (look at signal list for signification)"); break;
  }
  printf("\n");
  printf("# Simulation: %s (%s) \n", mcinstrument_name, mcinstrument_source);
  printf("# Breakpoint: %s ", mcsig_message); 
  strcpy(mcsig_message, "sighandler");
  if (mcget_ncount() == 0)
    printf("(0 %%)\n" );
  else
  {
    printf("%.2f %% (%10.1f/%10.1f)\n", 100*mcget_run_num()/mcget_ncount(), mcget_run_num(), mcget_ncount());
  }
  t1 = time(NULL);
  printf("# Date      : %s",ctime(&t1));

  if (sig == SIGUSR1)
  {
    printf("# McStas: Resuming simulation (continue)\n");
    fflush(stdout);
    return;
  }
  else
  if (sig == SIGUSR2)
  {
    printf("# McStas: Saving data and resume simulation (continue)\n");
    mcsave(NULL);
    fflush(stdout);
    return;
  }
  else
  if ((sig == SIGTERM) || (sig == SIGINT))
  {
    printf("# McStas: Finishing simulation (save results and exit)\n");
    mcfinally();
    exit(0);
  }
  else
  {
    fflush(stdout);
    perror("# Last I/O Error"); 
    printf("# McStas: Simulation stop (abort)\n"); 
    exit(-1);
  }
 
}
#endif /* !MAC */
#endif /* !WIN32 */

/* McStas main() function. */
int
mcstas_main(int argc, char *argv[])
{
/*  double run_num = 0; */
  time_t t;
  
#ifdef MAC
  argc = ccommand(&argv);
#endif

  t = (time_t)mcstartdate;
  srandom(time(&t));
  strcpy(mcsig_message, "main (Start)");
  mcuse_format("McStas");  /* default is to output as McStas format */
  mcparseoptions(argc, argv);

#ifndef MAC
#ifndef WIN32
  /* install sig handler, but only once !! after parameters parsing */
  signal( SIGQUIT ,sighandler);   /* quit (ASCII FS) */
  signal( SIGABRT ,sighandler);   /* used by abort, replace SIGIOT in the future */
  signal( SIGTRAP ,sighandler);   /* trace trap (not reset when caught) */
  signal( SIGTERM ,sighandler);   /* software termination signal from kill */
  /* signal( SIGPIPE ,sighandler);*/   /* write on a pipe with no one to read it, used by mcdisplay */

  signal( SIGUSR1 ,sighandler); /* display simulation status */
  signal( SIGUSR2 ,sighandler);
  signal( SIGILL ,sighandler);    /* illegal instruction (not reset when caught) */
  signal( SIGFPE ,sighandler);    /* floating point exception */
  signal( SIGBUS ,sighandler);    /* bus error */
  signal( SIGSEGV ,sighandler);   /* segmentation violation */
  #ifdef SIGSYS
  signal( SIGSYS ,sighandler);    /* bad argument to system call */
  #endif
  signal( SIGURG ,sighandler);    /* urgent socket condition */
#endif /* !MAC */
#endif /* !WIN32 */
  mcsiminfo_init(NULL); mcsiminfo_close();  /* makes sure we can do that */
  strcpy(mcsig_message, "main (Init)");
  mcinit();
#ifndef MAC
#ifndef WIN32  
  signal( SIGINT ,sighandler);    /* interrupt (rubout) only after INIT */
#endif /* !MAC */
#endif /* !WIN32 */
  
  while(mcrun_num < mcncount)
  {
    mcsetstate(0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1);
    mcraytrace();
    mcrun_num++;
  }
  mcfinally();
  if (mcformat.Name) free(mcformat.Name);
  
  return 0;
}

