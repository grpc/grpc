/*
------------------------------------------------------------------------------
perfhex.c: code to generate code for a hash for perfect hashing.
(c) Bob Jenkins, December 31 1999
You may use this code in any way you wish, and it is free.  No warranty.
I hereby place this in the public domain.
Source is http://burtleburtle.net/bob/c/perfhex.c

The task of this file is to do the minimal amount of mixing needed to
find distinct (a,b) for each key when each key is a distinct ub4.  That
means trying all possible ways to mix starting with the fastest.  The
output is those (a,b) pairs and code in the *final* structure for producing
those pairs.
------------------------------------------------------------------------------
*/

#ifndef STANDARD
#include "standard.h"
#endif
#ifndef LOOKUPA
#include "lookupa.h"
#endif
#ifndef RECYCLE
#include "recycle.h"
#endif
#ifndef PERFECT
#include "perfect.h"
#endif

/* 
 * Find a perfect hash when there is only one key.  Zero instructions.
 * Hint: the one key always hashes to 0
 */
static void hexone(keys, final)
key     *keys;
gencode *final;
{
  /* 1 key: the hash is always 0 */
  keys->a_k = 0;
  keys->b_k = 0;
  final->used = 1;
  sprintf(final->line[0], "  ub4 rsl = 0;\n");                    /* h1a: 37 */
}



/*
 * Find a perfect hash when there are only two keys.  Max 2 instructions.
 * There exists a bit that is different for the two keys.  Test it.
 * Note that a perfect hash of 2 keys is automatically minimal.
 */
static void hextwo(keys, final)
key     *keys;
gencode *final;
{
  ub4 a = keys->hash_k;
  ub4 b = keys->next_k->hash_k;
  ub4 i;
  
  if (a == b)
  {
    printf("fatal error: duplicate keys\n");
    exit(SUCCESS);
  }

  final->used = 1;
  
  /* one instruction */
  if ((a&1) != (b&1))
  {
    sprintf(final->line[0], "  ub4 rsl = (val & 1);\n");         /* h2a: 3,4 */
    return;
  }

  /* two instructions */
  for (i=0; i<UB4BITS; ++i)
  {
    if ((a&((ub4)1<<i)) != (b&((ub4)1<<i))) break;
  }
  /* h2b: 4,6 */
  sprintf(final->line[0], "  ub4 rsl = ((val << %ld) & 1);\n", i);
}



/*
 * find the value to xor to a and b and c to make none of them 3 
 * assert, (a,b,c) are three distinct values in (0,1,2,3).
 */
static ub4 find_adder(a,b,c)
ub4 a;
ub4 b;
ub4 c;
{
  return (a^b^c^3);
}



/*
 * Find a perfect hash when there are only three keys.  Max 6 instructions.
 *
 * keys a,b,c.  
 * There exists bit i such that a[i] != b[i].
 * Either c[i] != a[i] or c[i] != b[i], assume c[i] != a[i].
 * There exists bit j such that b[j] != c[j].  Note i != j.
 * Final hash should be no longer than val[i]^val[j].
 *
 * A minimal perfect hash needs to xor one of 0,1,2,3 afterwards to cause
 * the hole to land on 3.  find_adder() finds that constant
 */
static void hexthree(keys, final, form)
key      *keys;
gencode  *final;
hashform *form;
{
  ub4 a = keys->hash_k;
  ub4 b = keys->next_k->hash_k;
  ub4 c = keys->next_k->next_k->hash_k;
  ub4 i,j,x,y,z;
  
  final->used = 1;

  if (a == b || a == c || b == c)
  {
    printf("fatal error: duplicate keys\n");
    exit(SUCCESS);
  }
  
  /* one instruction */
  x = a&3; 
  y = b&3;
  z = c&3;
  if (x != y && x != z && y != z)
  {
    if (form->perfect == NORMAL_HP || (x != 3 && y != 3 && z != 3))
    {
      /* h3a: 0,1,2 */
      sprintf(final->line[0], "  ub4 rsl = (val & 3);\n");
    }
    else
    {
      /* h3b: 0,3,2 */
      sprintf(final->line[0], "  ub4 rsl = ((val & 3) ^ %d);\n",
	      find_adder(x,y,z));
    }
    return;
  }

  x = a>>(UB4BITS-2); 
  y = b>>(UB4BITS-2); 
  z = c>>(UB4BITS-2); 
  if (x != y && x != z && y != z)
  {
    if (form->perfect == NORMAL_HP || (x != 3 && y != 3 && z != 3)) 
    {
      /* h3c: 3fffffff, 7fffffff, bfffffff */
      sprintf(final->line[0], "  ub4 rsl = (val >> %ld);\n", (ub4)(UB4BITS-2));
    }
    else
    {
      /* h3d: 7fffffff, bfffffff, ffffffff */
      sprintf(final->line[0], "  ub4 rsl = ((val >> %ld) ^ %ld);\n",
	      (ub4)(UB4BITS-2), find_adder(x,y,z));
    }
    return;
  }

  /* two instructions */
  for (i=0; i<final->highbit; ++i)
  {
    x = (a>>i)&3;
    y = (b>>i)&3;
    z = (c>>i)&3;
    if (x != y && x != z && y != z)
    {
      if (form->perfect == NORMAL_HP || (x != 3 && y != 3 && z != 3))
      {
	/* h3e: ffff3fff, ffff7fff, ffffbfff */
	sprintf(final->line[0], "  ub4 rsl = ((val >> %ld) & 3);\n", i);
      }
      else
      {
	/* h3f: ffff7fff, ffffbfff, ffffffff */
	sprintf(final->line[0], "  ub4 rsl = (((val >> %ld) & 3) ^ %ld);\n", i,
		find_adder(x,y,z));
      }
      return;
    }
  }

  /* three instructions */
  for (i=0; i<=final->highbit; ++i)
  {
    x = (a+(a>>i))&3;
    y = (b+(b>>i))&3;
    z = (c+(c>>i))&3;
    if (x != y && x != z && y != z)
    {
      if (form->perfect == NORMAL_HP || (x != 3 && y != 3 && z != 3))
      {
	/* h3g: 0x000, 0x001, 0x100 */
	sprintf(final->line[0], "  ub4 rsl = ((val+(val>>%ld))&3);\n", i);
      }
      else
      {
	/* h3h: 0x001, 0x100, 0x101 */
	sprintf(final->line[0], "  ub4 rsl = (((val+(val>>%ld))&3)^%ld);\n", i,
		find_adder(x,y,z));
      }
      return;
    }
  }

  /*
   * Four instructions: I can prove this will always work.
   *
   * If the three values are distinct, there are two bits which 
   * distinguish them.  Choose the two such bits that are closest together.
   * If those bits are values 001 and 100 for those three values,
   * then there either aren't any bits in between
   * or the in-between bits aren't valued 001, 110, 100, 011, 010, or 101,
   * because that would violate the closest-together assumption.
   * So any in-between bits must be 000 or 111, and of 000 and 111 with
   * the distinguishing bits won't cause them to stop being distinguishing.
   */
  for (i=final->lowbit; i<=final->highbit; ++i)
  {
    for (j=i; j<=final->highbit; ++j)
    {
      x = ((a>>i)^(a>>j))&3;
      y = ((b>>i)^(b>>j))&3;
      z = ((c>>i)^(c>>j))&3;
      if (x != y && x != z && y != z)
      {
	if (form->perfect == NORMAL_HP || (x != 3 && y != 3 && z != 3))
	{
	  /* h3i: 0x00, 0x04, 0x10 */
	  sprintf(final->line[0], 
		  "  ub4 rsl = (((val>>%ld) ^ (val>>%ld)) & 3);\n", i, j);
	}
	else
	{
	  /* h3j: 0x04, 0x10, 0x14 */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((((val>>%ld) ^ (val>>%ld)) & 3) ^ %ld);\n",
		  i, j, find_adder(x,y,z));
	}
	return;
      }
    }
  }

  printf("fatal error: hexthree\n");
  exit(SUCCESS);
}



/*
 * Check that a,b,c,d are some permutation of 0,1,2,3
 * Assume that a,b,c,d are all have values less than 32.
 */
static int testfour(a,b,c,d)
ub4 a;
ub4 b;
ub4 c;
ub4 d;
{
  ub4 mask = (1<<a)^(1<<b)^(1<<c)^(1<<d);
  return (mask == 0xf);
}



/*
 * Find a perfect hash when there are only four keys.  Max 10 instructions.
 * Note that a perfect hash for 4 keys will automatically be minimal.
 */
static void hexfour(keys, final)
key     *keys;
gencode *final;
{
  ub4 a = keys->hash_k;
  ub4 b = keys->next_k->hash_k;
  ub4 c = keys->next_k->next_k->hash_k;
  ub4 d = keys->next_k->next_k->next_k->hash_k;
  ub4 w,x,y,z;
  ub4 i,j,k;

  if (a==b || a==c || a==d || b==c || b==d || c==d)
  {
    printf("fatal error: Duplicate keys\n");
    exit(SUCCESS);
  }

  final->used = 1;

  /* one instruction */
  if ((final->diffbits & 3) == 3)
  {
    w = a&3;
    x = b&3;
    y = c&3;
    z = d&3;
    if (testfour(w,x,y,z))
    {
      sprintf(final->line[0], "  ub4 rsl = (val & 3);\n");   /* h4a: 0,1,2,3 */
      return;
    }
  }

  if (((final->diffbits >> (UB4BITS-2)) & 3) == 3)
  {
    w = a>>(UB4BITS-2);
    x = b>>(UB4BITS-2);
    y = c>>(UB4BITS-2);
    z = d>>(UB4BITS-2);
    if (testfour(w,x,y,z))
    {                         /* h4b: 0fffffff, 4fffffff, 8fffffff, cfffffff */
      sprintf(final->line[0], "  ub4 rsl = (val >> %ld);\n", (ub4)(UB4BITS-2));
      return;
    }
  }

  /* two instructions */
  for (i=final->lowbit; i<final->highbit; ++i)
  {
    if (((final->diffbits >> i) & 3) == 3)
    {
      w = (a>>i)&3;
      x = (b>>i)&3;
      y = (c>>i)&3;
      z = (d>>i)&3;
      if (testfour(w,x,y,z))
      {                                                      /* h4c: 0,2,4,6 */
	sprintf(final->line[0], "  ub4 rsl = ((val >> %ld) & 3);\n", i);
	return;
      }
    }
  }

  /* three instructions (linear with the number of diffbits) */
  if ((final->diffbits & 3) != 0)
  {
    for (i=final->lowbit; i<=final->highbit; ++i)
    {
      if (((final->diffbits >> i) & 3) != 0)
      {
	w = (a+(a>>i))&3;
	x = (b+(b>>i))&3;
	y = (c+(c>>i))&3;
	z = (d+(d>>i))&3;
	if (testfour(w,x,y,z))
	{                                                    /* h4d: 0,1,2,4 */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((val + (val >> %ld)) & 3);\n", i);
	  return;
	}

	w = (a-(a>>i))&3;
	x = (b-(b>>i))&3;
	y = (c-(c>>i))&3;
	z = (d-(d>>i))&3;
	if (testfour(w,x,y,z))
	{                                                    /* h4e: 0,1,3,5 */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((val - (val >> %ld)) & 3);\n", i);
	  return;
	}

	/* h4f: ((val>>k)-val)&3: redundant with h4e */

	w = (a^(a>>i))&3;
	x = (b^(b>>i))&3;
	y = (c^(c>>i))&3;
	z = (d^(d>>i))&3;
	if (testfour(w,x,y,z))
	{                                                    /* h4g: 3,4,5,8 */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((val ^ (val >> %ld)) & 3);\n", i);
	  return;
	}
      }
    }
  }

  /* four instructions (linear with the number of diffbits) */
  if ((final->diffbits & 3) != 0)
  {
    for (i=final->lowbit; i<=final->highbit; ++i)
    {
      if ((((final->diffbits >> i) & 1) != 0) &&
	  ((final->diffbits & 2) != 0))
      {
	w = (a&3)^((a>>i)&1);
	x = (b&3)^((b>>i)&1);
	y = (c&3)^((c>>i)&1);
	z = (d&3)^((d>>i)&1);
	if (testfour(w,x,y,z))
	{                                                    /* h4h: 1,2,6,8 */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((val & 3) ^ ((val >> %ld) & 1));\n", i);
	  return;
	}

	w = (a&2)^((a>>i)&1);
	x = (b&2)^((b>>i)&1);
	y = (c&2)^((c>>i)&1);
	z = (d&2)^((d>>i)&1);
	if (testfour(w,x,y,z))
	{                                                    /* h4i: 1,2,8,a */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((val & 2) ^ ((val >> %ld) & 1));\n", i);
	  return;
	}
      }

      if ((((final->diffbits >> i) & 2) != 0) &&
	  ((final->diffbits & 1) != 0))
      {
	w = (a&3)^((a>>i)&2);
	x = (b&3)^((b>>i)&2);
	y = (c&3)^((c>>i)&2);
	z = (d&3)^((d>>i)&2);
	if (testfour(w,x,y,z))
	{                                                    /* h4j: 0,1,3,4 */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((val & 3) ^ ((val >> %ld) & 2));\n", i);
	  return;
	}

	w = (a&1)^((a>>i)&2);
	x = (b&1)^((b>>i)&2);
	y = (c&1)^((c>>i)&2);
	z = (d&1)^((d>>i)&2);
	if (testfour(w,x,y,z))
	{                                                    /* h4k: 1,4,7,8 */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((val & 1) ^ ((val >> %ld) & 2));\n", i);
	  return;
	}
      }
    }
  }

  /* four instructions (quadratic in the number of diffbits) */
  for (i=final->lowbit; i<=final->highbit; ++i)
  {
    if (((final->diffbits >> i) & 1) == 1)
    {
      for (j=final->lowbit; j<=final->highbit; ++j)
      {
	if (((final->diffbits >> j) & 3) != 0)
	{
	  /* test + */
	  w = ((a>>i)+(a>>j))&3;
	  x = ((b>>i)+(a>>j))&3;
	  y = ((c>>i)+(a>>j))&3;
	  z = ((d>>i)+(a>>j))&3;
	  if (testfour(w,x,y,z))
	  {                                                /* h4l: testcase? */
	    sprintf(final->line[0], 
		    "  ub4 rsl = (((val >> %ld) + (val >> %ld)) & 3);\n", 
		    i, j);
	    return;
	  }

	  /* test - */
	  w = ((a>>i)-(a>>j))&3;
	  x = ((b>>i)-(a>>j))&3;
	  y = ((c>>i)-(a>>j))&3;
	  z = ((d>>i)-(a>>j))&3;
	  if (testfour(w,x,y,z))
	  {                                                /* h4m: testcase? */
	    sprintf(final->line[0], 
		    "  ub4 rsl = (((val >> %ld) - (val >> %ld)) & 3);\n",
		    i, j);
	    return;
	  }

	  /* test ^ */
	  w = ((a>>i)^(a>>j))&3;
	  x = ((b>>i)^(a>>j))&3;
	  y = ((c>>i)^(a>>j))&3;
	  z = ((d>>i)^(a>>j))&3;
	  if (testfour(w,x,y,z))
	  {                                                /* h4n: testcase? */
	    sprintf(final->line[0], 
		    "  ub4 rsl = (((val >> %ld) ^ (val >> %ld)) & 3);\n",
		    i, j);
	    return;
	  }
	}
      }
    }
  }

  /* five instructions (quadratic in the number of diffbits) */
  for (i=final->lowbit; i<=final->highbit; ++i)
  {
    if (((final->diffbits >> i) & 1) != 0)
    {
      for (j=final->lowbit; j<=final->highbit; ++j)
      {
	if (((final->diffbits >> j) & 3) != 0)
	{
	  w = ((a>>j)&3)^((a>>i)&1);
	  x = ((b>>j)&3)^((b>>i)&1);
	  y = ((c>>j)&3)^((c>>i)&1);
	  z = ((d>>j)&3)^((d>>i)&1);
	  if (testfour(w,x,y,z))
	  {                                                  /* h4o: 0,4,8,a */
	    sprintf(final->line[0], 
		    "  ub4 rsl = (((val >> %ld) & 3) ^ ((val >> %ld) & 1));\n", 
		    j, i);
	    return;
	  }
	  
	  w = ((a>>j)&2)^((a>>i)&1);
	  x = ((b>>j)&2)^((b>>i)&1);
	  y = ((c>>j)&2)^((c>>i)&1);
	  z = ((d>>j)&2)^((d>>i)&1);
	  if (testfour(w,x,y,z))
	  {                                   /* h4p: 0x04, 0x08, 0x10, 0x14 */
	    sprintf(final->line[0], 
		    "  ub4 rsl = (((val >> %ld) & 2) ^ ((val >> %ld) & 1));\n", 
		    j, i);
	    return;
	  }
	}
	
	if (i==0)
	{
	  w = ((a>>j)^(a<<1))&3;
	  x = ((b>>j)^(b<<1))&3;
	  y = ((c>>j)^(c<<1))&3;
	  z = ((d>>j)^(d<<1))&3;
	}
	else
	{
	  w = ((a>>j)&3)^((a>>(i-1))&2);
	  x = ((b>>j)&3)^((b>>(i-1))&2);
	  y = ((c>>j)&3)^((c>>(i-1))&2);
	  z = ((d>>j)&3)^((d>>(i-1))&2);
	}
	if (testfour(w,x,y,z))
	{
	  if (i==0)                                          /* h4q: 0,4,5,8 */
	  {
	    sprintf(final->line[0], 
		    "  ub4 rsl = (((val >> %ld) ^ (val << 1)) & 3);\n",
		    j);
	  }
	  else if (i==1)                         /* h4r: 0x01,0x09,0x0b,0x10 */
	  {
	    sprintf(final->line[0], 
		    "  ub4 rsl = (((val >> %ld) & 3) ^ (val & 2));\n",
		    j);
	  }
	  else                                               /* h4s: 0,2,6,8 */
	  {
	    sprintf(final->line[0], 
		    "  ub4 rsl = (((val >> %ld) & 3) ^ ((val >> %ld) & 2));\n",
		    j, (i-1));
	  }
	  return;
	}
	  
	w = ((a>>j)&1)^((a>>i)&2);
	x = ((b>>j)&1)^((b>>i)&2);
	y = ((c>>j)&1)^((c>>i)&2);
	z = ((d>>j)&1)^((d>>i)&2);
	if (testfour(w,x,y,z))                   /* h4t: 0x20,0x14,0x10,0x06 */
	{                   
	  sprintf(final->line[0], 
		  "  ub4 rsl = (((val >> %ld) & 1) ^ ((val >> %ld) & 2));\n",
		  j, i);
	  return;
	}
      }
    }
  }

  /*
   * OK, bring out the big guns.
   * There exist three bits i,j,k which distinguish a,b,c,d.
   * i^(j<<1)^(k*q) is guaranteed to work for some q in {0,1,2,3},
   *   proven by exhaustive search of all (8 choose 4) cases.
   * Find three such bits and try the 4 cases.
   * Linear with the number of diffbits.
   * Some cases below may duplicate some cases above.  I did it that way
   *   so that what is below is guaranteed to work, no matter what was
   *   attempted above.
   * The generated hash is at most 10 instructions.
   */
  for (i=final->lowbit; i<UB4BITS; ++i)
  {
    y = (c>>i)&1;
    z = (d>>i)&1;
    if (y != z)
      break;
  }

  for (j=final->lowbit; j<UB4BITS; ++j)
  {
    x = ((b>>i)&1)^(((b>>j)&1)<<1);
    y = ((c>>i)&1)^(((c>>j)&1)<<1);
    z = ((d>>i)&1)^(((d>>j)&1)<<1);
    if (x != y && x != z && y != z)
      break;
  }

  for (k=final->lowbit; k<UB4BITS; ++k)
  {
    w = ((a>>i)&1)^(((a>>j)&1)<<1)^(((a>>k)&1)<<2);
    x = ((b>>i)&1)^(((b>>j)&1)<<1)^(((b>>k)&1)<<2);
    y = ((c>>i)&1)^(((c>>j)&1)<<1)^(((c>>k)&1)<<2);
    z = ((d>>i)&1)^(((d>>j)&1)<<1)^(((d>>k)&1)<<2);
    if (w != x && w != y && w != z && x != y && x != z && y != z)
      break;
  }

  /* Assert: bits i,j,k were found which distinguish a,b,c,d */
  if (i==UB4BITS || j==UB4BITS || k==UB4BITS)
  {
    printf("Fatal error: hexfour(), i %ld j %ld k %ld\n", i,j,k);
    exit(SUCCESS);
  }

  /* now try the four cases */
  {
    ub4 m,n,o,p;
    
    /* if any bit has two 1s and two 0s, make that bit o */
    if (((a>>i)&1)+((b>>i)&1)+((c>>i)&1)+((d>>i)&1) != 2)
      { m=j; n=k; o=i; }
    else if (((a>>j)&1)+((b>>j)&1)+((c>>j)&1)+((d>>j)&1) != 2)
      { m=i; n=k; o=j; }
    else
      { m=i; n=j; o=k; }
    if (m > n) {p=m; m=n; n=p; }                          /* guarantee m < n */

    /* printf("m %ld n %ld o %ld  %ld %ld %ld %ld\n", m, n, o, w,x,y,z); */

    /* seven instructions, multiply bit o by 1 */
    w = (((a>>m)^(a>>o))&1)^((a>>(n-1))&2);
    x = (((b>>m)^(b>>o))&1)^((b>>(n-1))&2);
    y = (((c>>m)^(c>>o))&1)^((c>>(n-1))&2);
    z = (((d>>m)^(d>>o))&1)^((d>>(n-1))&2);
    if (testfour(w,x,y,z))
    {
      if (m>o) {p=m; m=o; o=p;}                 /* make sure m < o and m < n */

      if (m==0)                                                   /* 0,2,8,9 */
      {
	sprintf(final->line[0], 
		"  ub4 rsl = (((val^(val>>%ld))&1)^((val>>%ld)&2));\n", o, n-1);
      }
      else                                            /* 0x00,0x04,0x10,0x12 */
      {
	sprintf(final->line[0], 
		"  ub4 rsl = ((((val>>%ld) ^ (val>>%ld)) & 1) ^ ((val>>%ld) & 2));\n",
		m, o, n-1);
      }
      return;
    }
    
    /* six to seven instructions, multiply bit o by 2 */
    w = ((a>>m)&1)^((((a>>n)^(a>>o))&1)<<1);
    x = ((b>>m)&1)^((((b>>n)^(b>>o))&1)<<1);
    y = ((c>>m)&1)^((((c>>n)^(c>>o))&1)<<1);
    z = ((d>>m)&1)^((((d>>n)^(d>>o))&1)<<1);
    if (testfour(w,x,y,z))
    {
      if (m==o-1) {p=n; n=o; o=p;}                /* make m==n-1 if possible */

      if (m==0)                                                   /* 0,1,5,8 */
      {
	sprintf(final->line[0], 
		"  ub4 rsl = ((val & 1) ^ (((val>>%ld) ^ (val>>%ld)) & 2));\n",
		n-1, o-1);
      }
      else if (o==0)                                  /* 0x00,0x04,0x05,0x10 */
      {
	sprintf(final->line[0], 
		"  ub4 rsl = (((val>>%ld) & 2) ^ (((val>>%ld) ^ val) & 1));\n",
		m-1, n);
      }
      else                                            /* 0x00,0x02,0x0a,0x10 */
      {
	sprintf(final->line[0], 
		"  ub4 rsl = (((val>>%ld) & 1) ^ (((val>>%ld) ^ (val>>%ld)) & 2));\n",
		m, n-1, o-1);
      }
      return;
    }
    
    /* multiplying by 3 is a pain: seven or eight instructions */
    w = (((a>>m)&1)^((a>>(n-1))&2))^((a>>o)&1)^(((a>>o)&1)<<1);
    x = (((b>>m)&1)^((b>>(n-1))&2))^((b>>o)&1)^(((b>>o)&1)<<1);
    y = (((c>>m)&1)^((c>>(n-1))&2))^((c>>o)&1)^(((c>>o)&1)<<1);
    z = (((d>>m)&1)^((d>>(n-1))&2))^((d>>o)&1)^(((d>>o)&1)<<1);
    if (testfour(w,x,y,z))
    {
      final->used = 2;
      sprintf(final->line[0], "  ub4 b = (val >> %ld) & 1;\n", o);
      if (m==o-1 && m==0)                             /* 0x02,0x10,0x11,0x18 */
      {
	sprintf(final->line[1], 
		"  ub4 rsl = ((val & 3) ^ ((val >> %ld) & 2) ^ b);\n", n-1);
      }
      else if (m==o-1)                                            /* 0,4,6,c */
      {
	sprintf(final->line[1], 
		"  ub4 rsl = (((val >> %ld) & 3) ^ ((val >> %ld) & 2) ^ b);\n",
		m, n-1);
      }
      else if (m==n-1 && m==0)                                /* 02,0a,0b,18 */
      {
	sprintf(final->line[1], 
		"  ub4 rsl = ((val & 3) ^ b ^ (b << 1));\n");
      }
      else if (m==n-1)                                            /* 0,2,4,8 */
      {
	sprintf(final->line[1], 
		"  ub4 rsl = (((val >> %ld) & 3) ^ b ^ (b << 1));\n", m);
      }
      else if (o==n-1 && m==0)                          /* h4am: not reached */
      {
	sprintf(final->line[1], 
		"  ub4 rsl = ((val & 1) ^ ((val >> %ld) & 3) ^ (b <<1 ));\n",
		o);
      }
      else if (o==n-1)                                /* 0x00,0x02,0x08,0x10 */
      {
	sprintf(final->line[1], 
		"  ub4 rsl = (((val >> %ld) & 1) ^ ((val >> %ld) & 3) ^ (b << 1));\n",
		m, o);
      }
      else if ((m != o-1) && (m != n-1) && (o != m-1) && (o != n-1))
      {
	final->used = 3;
	sprintf(final->line[0], "  ub4 newval = val & 0x%lx;\n", 
		(((ub4)1<<m)^((ub4)1<<n)^((ub4)1<<o)));
	if (o==0)                                     /* 0x00,0x01,0x04,0x10 */
	{
	  sprintf(final->line[1], "  ub4 b = -newval;\n");
	}
	else                                          /* 0x00,0x04,0x09,0x10 */
	{
	  sprintf(final->line[1], "  ub4 b = -(newval >> %ld);\n", o);
	}
	if (m==0)                                     /* 0x00,0x04,0x09,0x10 */
	{
	  sprintf(final->line[2], 
		  "  ub4 rsl = ((newval ^ (newval>>%ld) ^ b) & 3);\n", n-1);
	}
	else                                          /* 0x00,0x03,0x04,0x10 */
	{
	  sprintf(final->line[2], 
		  "  ub4 rsl = (((newval>>%ld) ^ (newval>>%ld) ^ b) & 3);\n",
		  m, n-1);
	}
      }
      else if (o == m-1)
      {
	if (o==0)                                     /* 0x02,0x03,0x0a,0x10 */
	{
	  sprintf(final->line[0], "  ub4 b = (val<<1) & 2;\n");
	}
	else if (o==1)                                /* 0x00,0x02,0x04,0x10 */
	{
	  sprintf(final->line[0], "  ub4 b = val & 2;\n");
	}
	else                                          /* 0x00,0x04,0x08,0x20 */
	{
	  sprintf(final->line[0], "  ub4 b = (val>>%ld) & 2;\n", o-1);
	}

	if (o==0)                                     /* 0x02,0x03,0x0a,0x10 */
	{
	  sprintf(final->line[1],
		  "  ub4 rsl = ((val & 3) ^ ((val>>%ld) & 1) ^ b);\n",
		  n);
	}
	else                                          /* 0x00,0x02,0x04,0x10 */
	{
	  sprintf(final->line[1],
		  "  ub4 rsl = (((val>>%ld) & 3) ^ ((val>>%ld) & 1) ^ b);\n",
		  o, n);
	}
      }
      else                         /* h4ax: 10 instructions, but not reached */
      {
	sprintf(final->line[1], 
		"  ub4 rsl = (((val>>%ld) & 1) ^ ((val>>%ld) & 2) ^ b ^ (b<<1));\n",
		m, n-1);
      }

      return;
    }

    /* five instructions, multiply bit o by 0, covered before the big guns */
    w = ((a>>m)&1)^(a>>(n-1)&2);
    x = ((b>>m)&1)^(b>>(n-1)&2);
    y = ((c>>m)&1)^(c>>(n-1)&2);
    z = ((d>>m)&1)^(d>>(n-1)&2);
    if (testfour(w,x,y,z))
    {                                                    /* h4v, not reached */
      sprintf(final->line[0], 
	      "  ub4 rsl = (((val>>%ld) & 1) ^ ((val>>%ld) & 2));\n", m, n-1);
      return;
    }
  }

  printf("fatal error: bug in hexfour!\n");
  exit(SUCCESS);
  return;
}


/* test if a_k is distinct and in range for all keys */
static int testeight(keys, badmask)
key      *keys;                                         /* keys being hashed */
ub1       badmask;                       /* used for minimal perfect hashing */
{
  ub1  mask = badmask;
  key *mykey;

  for (mykey=keys; mykey; mykey=mykey->next_k)
  {
    if (bit(mask, 1<<mykey->a_k)) return FALSE;
    bis(mask, 1<<mykey->a_k);
  }
  return TRUE;
}



/*
 * Try to find a perfect hash when there are five to eight keys.
 *
 * We can't deterministically find a perfect hash, but there's a reasonable
 * chance we'll get lucky.  Give it a shot.  Return TRUE if we succeed.
 */
static int hexeight(keys, nkeys, final, form)
key      *keys;
ub4       nkeys;
gencode  *final;
hashform *form;
{
  key *mykey;                                       /* walk through the keys */
  ub4  i,j,k;
  ub1  badmask;

  printf("hexeight\n");

  /* what hash values should never be used? */
  badmask = 0;
  if (form->perfect == MINIMAL_HP)
  {
    for (i=nkeys; i<8; ++i)
      bis(badmask,(1<<i));
  }

  /* one instruction */
  for (mykey=keys; mykey; mykey=mykey->next_k)
    mykey->a_k = mykey->hash_k & 7;
  if (testeight(keys, badmask))
  {                                                                   /* h8a */
    final->used = 1;
    sprintf(final->line[0], "  ub4 rsl = (val & 7);\n");
    return TRUE;
  }

  /* two instructions */
  for (i=final->lowbit; i<=final->highbit-2; ++i)
  {
    for (mykey=keys; mykey; mykey=mykey->next_k)
      mykey->a_k = (mykey->hash_k >> i) & 7;
    if (testeight(keys, badmask))
    {                                                                 /* h8b */
      final->used = 1;
      sprintf(final->line[0], "  ub4 rsl = ((val >> %ld) & 7);\n", i);
      return TRUE;
    }
  }

  /* four instructions */
  for (i=final->lowbit; i<=final->highbit; ++i)
  {
    for (j=i+1; j<=final->highbit; ++j)
    {
      for (mykey=keys; mykey; mykey=mykey->next_k)
	mykey->a_k = ((mykey->hash_k >> i)+(mykey->hash_k >> j)) & 7;
      if (testeight(keys, badmask))
      {
	final->used = 1;
	if (i == 0)                                                   /* h8c */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((val + (val >> %ld)) & 7);\n", j);
	else                                                          /* h8d */
	  sprintf(final->line[0], 
		  "  ub4 rsl = (((val >> %ld) + (val >> %ld)) & 7);\n", i, j);
	return TRUE;
      }

      for (mykey=keys; mykey; mykey=mykey->next_k)
	mykey->a_k = ((mykey->hash_k >> i)^(mykey->hash_k >> j)) & 7;
      if (testeight(keys, badmask))
      {
	final->used = 1;
	if (i == 0)                                                   /* h8e */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((val ^ (val >> %ld)) & 7);\n", j);
	else                                                          /* h8f */
	  sprintf(final->line[0], 
		  "  ub4 rsl = (((val >> %ld) ^ (val >> %ld)) & 7);\n", i, j);

	return TRUE;
      }

      for (mykey=keys; mykey; mykey=mykey->next_k)
	mykey->a_k = ((mykey->hash_k >> i)-(mykey->hash_k >> j)) & 7;
      if (testeight(keys, badmask))
      {
	final->used = 1;
	if (i == 0)                                                   /* h8g */
	  sprintf(final->line[0], 
		  "  ub4 rsl = ((val - (val >> %ld)) & 7);\n", j);
	else                                                          /* h8h */
	  sprintf(final->line[0], 
		  "  ub4 rsl = (((val >> %ld) - (val >> %ld)) & 7);\n", i, j);

	return TRUE;
      }
    }
  }


  /* six instructions */
  for (i=final->lowbit; i<=final->highbit; ++i)
  {
    for (j=i+1; j<=final->highbit; ++j)
    {
      for (k=j+1; k<=final->highbit; ++k)
      {
	for (mykey=keys; mykey; mykey=mykey->next_k)
	  mykey->a_k  = ((mykey->hash_k >> i) +
			 (mykey->hash_k >> j) +
			 (mykey->hash_k >> k)) & 7;
	if (testeight(keys, badmask))
	{                                                             /* h8i */
	  final->used = 1;
	  sprintf(final->line[0], 
		  "  ub4 rsl = (((val >> %ld) + (val >> %ld) + (val >> %ld)) & 7);\n", 
		  i, j, k);
	  return TRUE;
	}
      }
    }
  }


  return FALSE;
}



/*
 * Guns aren't enough.  Bring out the Bomb.  Use tab[].
 * This finds the initial (a,b) when we need to use tab[].
 *
 * We need to produce a different (a,b) every time this is called.  Try all
 * reasonable cases, fastest first.
 *
 * The initial mix (which this determines) can be filled into final starting
 * at line[1].  val is set and a,b are declared.  The final hash (at line[7])
 * is a^tab[b] or a^scramble[tab[b]].
 *
 * The code will probably look like this, minus some stuff:
 *     val += CONSTANT;
 *     val ^= (val<<16);
 *     val += (val>>8);
 *     val ^= (val<<4);
 *     b = (val >> l) & 7;
 *     a = (val + (val<<m)) >> 29;
 *     return a^scramble[tab[b]];
 * Note that *a* and tab[b] will be computed in parallel by most modern chips.
 *
 * final->i is the current state of the state machine.
 * final->j and final->k are counters in the loops the states simulate.
 */
static void hexn(keys, salt, alen, blen, final)
key     *keys;
ub4      salt;
ub4      alen;
ub4      blen;
gencode *final;
{
  key *mykey;
  ub4  highbit = final->highbit;
  ub4  lowbit = final->lowbit;
  ub4  alog = mylog2(alen);
  ub4  blog = mylog2(blen);

  for (;;)
  {
    switch(final->i)
    {
    case 1:
      /* a = val>>30; b=val&3 */
      for (mykey=keys; mykey; mykey=mykey->next_k)
      {
	mykey->a_k = (mykey->hash_k << (UB4BITS-(highbit+1)))>>(UB4BITS-alog);
	mykey->b_k = (mykey->hash_k >> lowbit) & (blen-1);
      }
      if (lowbit == 0)                                                /* hna */
	sprintf(final->line[5], "  b = (val & 0x%lx);\n", 
		blen-1);
      else                                                            /* hnb */
	sprintf(final->line[5], "  b = ((val >> %ld) & 0x%lx);\n", 
		lowbit, blen-1);
      if (highbit+1 == UB4BITS)                                       /* hnc */
	sprintf(final->line[6], "  a = (val >> %ld);\n",
		UB4BITS-alog);
      else                                                            /* hnd */
	sprintf(final->line[6], "  a = ((val << %ld ) >> %ld);\n",
		UB4BITS-(highbit+1), UB4BITS-alog);
  
      ++final->i;
      return;

    case 2:
      /* a = val&3; b=val>>30 */
      for (mykey=keys; mykey; mykey=mykey->next_k)
      {
	mykey->a_k = (mykey->hash_k >> lowbit) & (alen-1);
	mykey->b_k = (mykey->hash_k << (UB4BITS-(highbit+1)))>>(UB4BITS-blog);
      }
      if (highbit+1 == UB4BITS)                                       /* hne */
	sprintf(final->line[5], "  b = (val >> %ld);\n",
		UB4BITS-blog);
      else                                                            /* hnf */
	sprintf(final->line[5], "  b = ((val << %ld ) >> %ld);\n",
		UB4BITS-(highbit+1), UB4BITS-blog);
      if (lowbit == 0)                                                /* hng */
	sprintf(final->line[6], "  a = (val & 0x%lx);\n", 
		alen-1);
      else                                                            /* hnh */
	sprintf(final->line[6], "  a = ((val >> %ld) & 0x%lx);\n", 
		lowbit, alen-1);
  
      ++final->i;
      return;

    case 3:
      /*
       * cases 3,4,5:
       * for (k=lowbit; k<=highbit; ++k)
       *   for (j=lowbit; j<=highbit; ++j)
       *     b = (val>>j)&3;
       *     a = (val<<k)>>30;
       */
      final->k = lowbit;
      final->j = lowbit;
      ++final->i;
      break;

    case 4:
      if (!(final->j < highbit))
      {
	++final->i;
	break;
      }
      for (mykey=keys; mykey; mykey=mykey->next_k)
      {
	mykey->b_k = (mykey->hash_k >> (final->j)) & (blen-1);
	mykey->a_k = (mykey->hash_k << (UB4BITS-final->k-1)) >> (UB4BITS-alog);
      }
      if (final->j == 0)                                              /* hni */
	sprintf(final->line[5], "  b = val & 0x%lx;\n",
		blen-1);
      else if (blog+final->j == UB4BITS)                             /* hnja */
	sprintf(final->line[5], "  b = val >> %ld;\n",
		final->j);
      else
	sprintf(final->line[5], "  b = (val >> %ld) & 0x%lx;\n",      /* hnj */
		final->j, blen-1);
      if (UB4BITS-final->k-1 == 0)                                    /* hnk */
	sprintf(final->line[6], "  a = (val >> %ld);\n",
		UB4BITS-alog);
      else                                                            /* hnl */
	sprintf(final->line[6], "  a = ((val << %ld) >> %ld);\n",
		UB4BITS-final->k-1, UB4BITS-alog);
      while (++final->j < highbit)
      {
	if (((final->diffbits>>(final->j)) & (blen-1)) > 2)
	  break;
      }
      return;

    case 5:
      while (++final->k < highbit)
      {
	if ((((final->diffbits<<(UB4BITS-final->k-1))>>alog) & (alen-1)) > 0)
	  break;
      }
      if (!(final->k < highbit))
      {
	++final->i;
	break;
      }
      final->j = lowbit;
      final->i = 4;
      break;


    case 6:
      /*
       * cases 6,7,8:
       * for (k=0; k<UB4BITS-alog; ++k)
       *   for (j=0; j<UB4BITS-blog; ++j)
       *     val = val+f(salt);
       *     val ^= (val >> 16);
       *     val += (val << 8);
       *     val ^= (val >> 4);
       *     b = (val >> j) & 3;
       *     a = (val + (val << k)) >> 30;
       */
      final->k = 0;
      final->j = 0;
      ++final->i;
      break;

    case 7:
      /* Just do something that will surely work */
      {
	ub4 addk = 0x9e3779b9*salt;

	if (!(final->j <= UB4BITS-blog))
	{
	  ++final->i;
	  break;
	}
	for (mykey=keys; mykey; mykey=mykey->next_k)
	{
	  ub4 val = mykey->hash_k + addk;
	  if (final->highbit+1 - final->lowbit > 16)
	    val ^= (val >> 16);
	  if (final->highbit+1 - final->lowbit > 8)
	    val += (val << 8);
	  val ^= (val >> 4);
	  mykey->b_k = (val >> final->j) & (blen-1);
	  if (final->k == 0)
	    mykey->a_k = val >> (UB4BITS-alog);
	  else
	    mykey->a_k = (val + (val << final->k)) >> (UB4BITS-alog);
	}
	sprintf(final->line[1], "  val += 0x%lx;\n", addk);
	if (final->highbit+1 - final->lowbit > 16)                    /* hnm */
	  sprintf(final->line[2], "  val ^= (val >> 16);\n");
	if (final->highbit+1 - final->lowbit > 8)                     /* hnn */
	  sprintf(final->line[3], "  val += (val << 8);\n");
	sprintf(final->line[4], "  val ^= (val >> 4);\n");
	if (final->j == 0)              /* hno: don't know how to reach this */
	  sprintf(final->line[5], "  b = val & 0x%lx;\n", blen-1);
	else                                                          /* hnp */
	  sprintf(final->line[5], "  b = (val >> %ld) & 0x%lx;\n",
		  final->j, blen-1);
	if (final->k == 0)                                            /* hnq */
	  sprintf(final->line[6], "  a = val >> %ld;\n", UB4BITS-alog);
	else                                                          /* hnr */
	  sprintf(final->line[6], "  a = (val + (val << %ld)) >> %ld;\n",
		  final->k, UB4BITS-alog);

	++final->j;
	return;
      }

    case 8:
      ++final->k;
      if (!(final->k <= UB4BITS-alog))
      {
	++final->i;
	break;
      }
      final->j = 0;
      final->i = 7;
      break;

    case 9:
      final->i = 6;
      break;
    }
  }
}



/* find the highest and lowest bit where any key differs */
static void setlow(keys, final)
key     *keys;
gencode *final;
{
  ub4  lowbit;
  ub4  highbit;
  ub4  i;
  key *mykey;
  ub4  firstkey;

  /* mark the interesting bits in final->mask */
  final->diffbits = (ub4)0;
  if (keys) firstkey = keys->hash_k;
  for (mykey=keys;  mykey!=(key *)0;  mykey=mykey->next_k)
    final->diffbits |= (firstkey ^ mykey->hash_k);

  /* find the lowest interesting bit */
  for (i=0; i<UB4BITS; ++i)
    if (final->diffbits & (((ub4)1)<<i))
      break;
  final->lowbit = i;

  /* find the highest interesting bit */
  for (i=UB4BITS; --i; )
    if (final->diffbits & (((ub4)1)<<i))
      break;
  final->highbit = i;
}

/* 
 * Initialize (a,b) when keys are integers.
 *
 * Normally there's an initial hash which produces a number.  That hash takes
 * an initializer.  Changing the initializer causes the initial hash to 
 * produce a different (uniformly distributed) number without any extra work.
 *
 * Well, here we start with a number.  There's no initial hash.  Any mixing
 * costs extra work.  So we go through a lot of special cases to minimize the
 * mixing needed to get distinct (a,b).  For small sets of keys, it's often
 * fastest to skip the final hash and produce the perfect hash from the number
 * directly.
 *
 * The target user for this is switch statement optimization.  The common case
 * is 3 to 16 keys, and instruction counts matter.  The competition is a 
 * binary tree of branches.
 *
 * Return TRUE if we found a perfect hash and no more work is needed.
 * Return FALSE if we just did an initial hash and more work is needed.
 */
int inithex(keys, nkeys, alen, blen, smax, salt, final, form)
key      *keys;                                          /* list of all keys */
ub4       nkeys;                                   /* number of keys to hash */
ub4       alen;                    /* (a,b) has a in 0..alen-1, a power of 2 */
ub4       blen;                    /* (a,b) has b in 0..blen-1, a power of 2 */
ub4       smax;                   /* maximum range of computable hash values */
ub4       salt;                     /* used to initialize the hash function */
gencode  *final;                          /* output, code for the final hash */
hashform *form;                                           /* user directives */
{
  setlow(keys, final);

  switch (nkeys)
  {
  case 1:
    hexone(keys, final);
    return TRUE;
  case 2:
    hextwo(keys, final);
    return TRUE;
  case 3:
    hexthree(keys, final, form);
    return TRUE;
  case 4:
    hexfour(keys, final);
    return TRUE;
  case 5:  case 6:  case 7:  case 8:
    if (salt == 1 &&                                  /* first time through */
	hexeight(keys, nkeys, final, form)) /* get lucky, don't need tab[] ? */
      return TRUE;
    /* fall through */
  default:
    if (salt == 1)
    {
      final->used = 8;
      final->i = 1;
      final->j = final->k = final->l = final->m = final->n = final->o = 0;
      sprintf(final->line[0], "  ub4 a, b, rsl;\n");
      sprintf(final->line[1], "\n");
      sprintf(final->line[2], "\n");
      sprintf(final->line[3], "\n");
      sprintf(final->line[4], "\n");
      sprintf(final->line[5], "\n");
      sprintf(final->line[6], "\n");
      if (blen < USE_SCRAMBLE)
      {                                                               /* hns */
	sprintf(final->line[7], "  rsl = (a^tab[b]);\n");
      }
      else
      {                                                               /* hnt */
	sprintf(final->line[7], "  rsl = (a^scramble[tab[b]]);\n");
      }
    }
    hexn(keys, salt, alen, blen, final);
    return FALSE;
  }
}
