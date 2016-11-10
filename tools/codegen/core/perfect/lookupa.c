/*
--------------------------------------------------------------------
lookupa.c, by Bob Jenkins, December 1996.  Same as lookup2.c
Use this code however you wish.  Public Domain.  No warranty.
Source is http://burtleburtle.net/bob/c/lookupa.c
--------------------------------------------------------------------
*/
#ifndef STANDARD
#include "standard.h"
#endif
#ifndef LOOKUPA
#include "lookupa.h"
#endif

/*
--------------------------------------------------------------------
mix -- mix 3 32-bit values reversibly.
For every delta with one or two bit set, and the deltas of all three
  high bits or all three low bits, whether the original value of a,b,c
  is almost all zero or is uniformly distributed,
* If mix() is run forward or backward, at least 32 bits in a,b,c
  have at least 1/4 probability of changing.
* If mix() is run forward, every bit of c will change between 1/3 and
  2/3 of the time.  (Well, 22/100 and 78/100 for some 2-bit deltas.)
mix() was built out of 36 single-cycle latency instructions in a 
  structure that could supported 2x parallelism, like so:
      a -= b; 
      a -= c; x = (c>>13);
      b -= c; a ^= x;
      b -= a; x = (a<<8);
      c -= a; b ^= x;
      c -= b; x = (b>>13);
      ...
  Unfortunately, superscalar Pentiums and Sparcs can't take advantage 
  of that parallelism.  They've also turned some of those single-cycle
  latency instructions into multi-cycle latency instructions.  Still,
  this is the fastest good hash I could find.  There were about 2^^68
  to choose from.  I only looked at a billion or so.
--------------------------------------------------------------------
*/
#define mix(a,b,c) \
{ \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<<8); \
  c -= a; c -= b; c ^= (b>>13); \
  a -= b; a -= c; a ^= (c>>12);  \
  b -= c; b -= a; b ^= (a<<16); \
  c -= a; c -= b; c ^= (b>>5); \
  a -= b; a -= c; a ^= (c>>3);  \
  b -= c; b -= a; b ^= (a<<10); \
  c -= a; c -= b; c ^= (b>>15); \
}

/*
--------------------------------------------------------------------
lookup() -- hash a variable-length key into a 32-bit value
  k     : the key (the unaligned variable-length array of bytes)
  len   : the length of the key, counting by bytes
  level : can be any 4-byte value
Returns a 32-bit value.  Every bit of the key affects every bit of
the return value.  Every 1-bit and 2-bit delta achieves avalanche.
About 6len+35 instructions.

The best hash table sizes are powers of 2.  There is no need to do
mod a prime (mod is sooo slow!).  If you need less than 32 bits,
use a bitmask.  For example, if you need only 10 bits, do
  h = (h & hashmask(10));
In which case, the hash table should have hashsize(10) elements.

If you are hashing n strings (ub1 **)k, do it like this:
  for (i=0, h=0; i<n; ++i) h = lookup( k[i], len[i], h);

By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.  You may use this
code any way you wish, private, educational, or commercial.

See http://burtleburtle.net/bob/hash/evahash.html
Use for hash table lookup, or anything where one collision in 2^32 is
acceptable.  Do NOT use for cryptographic purposes.
--------------------------------------------------------------------
*/

ub4 lookup( k, length, level)
register ub1 *k;        /* the key */
register ub4  length;   /* the length of the key */
register ub4  level;    /* the previous hash, or an arbitrary value */
{
   register ub4 a,b,c,len;

   /* Set up the internal state */
   len = length;
   a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
   c = level;           /* the previous hash value */

   /*---------------------------------------- handle most of the key */
   while (len >= 12)
   {
      a += (k[0] +((ub4)k[1]<<8) +((ub4)k[2]<<16) +((ub4)k[3]<<24));
      b += (k[4] +((ub4)k[5]<<8) +((ub4)k[6]<<16) +((ub4)k[7]<<24));
      c += (k[8] +((ub4)k[9]<<8) +((ub4)k[10]<<16)+((ub4)k[11]<<24));
      mix(a,b,c);
      k += 12; len -= 12;
   }

   /*------------------------------------- handle the last 11 bytes */
   c += length;
   switch(len)              /* all the case statements fall through */
   {
   case 11: c+=((ub4)k[10]<<24);
   case 10: c+=((ub4)k[9]<<16);
   case 9 : c+=((ub4)k[8]<<8);
      /* the first byte of c is reserved for the length */
   case 8 : b+=((ub4)k[7]<<24);
   case 7 : b+=((ub4)k[6]<<16);
   case 6 : b+=((ub4)k[5]<<8);
   case 5 : b+=k[4];
   case 4 : a+=((ub4)k[3]<<24);
   case 3 : a+=((ub4)k[2]<<16);
   case 2 : a+=((ub4)k[1]<<8);
   case 1 : a+=k[0];
     /* case 0: nothing left to add */
   }
   mix(a,b,c);
   /*-------------------------------------------- report the result */
   return c;
}


/*
--------------------------------------------------------------------
mixc -- mixc 8 4-bit values as quickly and thoroughly as possible.
Repeating mix() three times achieves avalanche.
Repeating mix() four times eliminates all funnels and all
  characteristics stronger than 2^{-11}.
--------------------------------------------------------------------
*/
#define mixc(a,b,c,d,e,f,g,h) \
{ \
   a^=b<<11; d+=a; b+=c; \
   b^=c>>2;  e+=b; c+=d; \
   c^=d<<8;  f+=c; d+=e; \
   d^=e>>16; g+=d; e+=f; \
   e^=f<<10; h+=e; f+=g; \
   f^=g>>4;  a+=f; g+=h; \
   g^=h<<8;  b+=g; h+=a; \
   h^=a>>9;  c+=h; a+=b; \
}

/*
--------------------------------------------------------------------
checksum() -- hash a variable-length key into a 256-bit value
  k     : the key (the unaligned variable-length array of bytes)
  len   : the length of the key, counting by bytes
  state : an array of CHECKSTATE 4-byte values (256 bits)
The state is the checksum.  Every bit of the key affects every bit of
the state.  There are no funnels.  About 112+6.875len instructions.

If you are hashing n strings (ub1 **)k, do it like this:
  for (i=0; i<8; ++i) state[i] = 0x9e3779b9;
  for (i=0, h=0; i<n; ++i) checksum( k[i], len[i], state);

See http://burtleburtle.net/bob/hash/evahash.html
Use to detect changes between revisions of documents, assuming nobody
is trying to cause collisions.  Do NOT use for cryptography.
--------------------------------------------------------------------
*/
void  checksum( k, len, state)
register ub1 *k;
register ub4  len;
register ub4 *state;
{
   register ub4 a,b,c,d,e,f,g,h,length;

   /* Use the length and level; add in the golden ratio. */
   length = len;
   a=state[0]; b=state[1]; c=state[2]; d=state[3];
   e=state[4]; f=state[5]; g=state[6]; h=state[7];

   /*---------------------------------------- handle most of the key */
   while (len >= 32)
   {
      a += (k[0] +(k[1]<<8) +(k[2]<<16) +(k[3]<<24));
      b += (k[4] +(k[5]<<8) +(k[6]<<16) +(k[7]<<24));
      c += (k[8] +(k[9]<<8) +(k[10]<<16)+(k[11]<<24));
      d += (k[12]+(k[13]<<8)+(k[14]<<16)+(k[15]<<24));
      e += (k[16]+(k[17]<<8)+(k[18]<<16)+(k[19]<<24));
      f += (k[20]+(k[21]<<8)+(k[22]<<16)+(k[23]<<24));
      g += (k[24]+(k[25]<<8)+(k[26]<<16)+(k[27]<<24));
      h += (k[28]+(k[29]<<8)+(k[30]<<16)+(k[31]<<24));
      mixc(a,b,c,d,e,f,g,h);
      mixc(a,b,c,d,e,f,g,h);
      mixc(a,b,c,d,e,f,g,h);
      mixc(a,b,c,d,e,f,g,h);
      k += 32; len -= 32;
   }

   /*------------------------------------- handle the last 31 bytes */
   h += length;
   switch(len)
   {
   case 31: h+=(k[30]<<24);
   case 30: h+=(k[29]<<16);
   case 29: h+=(k[28]<<8);
   case 28: g+=(k[27]<<24);
   case 27: g+=(k[26]<<16);
   case 26: g+=(k[25]<<8);
   case 25: g+=k[24];
   case 24: f+=(k[23]<<24);
   case 23: f+=(k[22]<<16);
   case 22: f+=(k[21]<<8);
   case 21: f+=k[20];
   case 20: e+=(k[19]<<24);
   case 19: e+=(k[18]<<16);
   case 18: e+=(k[17]<<8);
   case 17: e+=k[16];
   case 16: d+=(k[15]<<24);
   case 15: d+=(k[14]<<16);
   case 14: d+=(k[13]<<8);
   case 13: d+=k[12];
   case 12: c+=(k[11]<<24);
   case 11: c+=(k[10]<<16);
   case 10: c+=(k[9]<<8);
   case 9 : c+=k[8];
   case 8 : b+=(k[7]<<24);
   case 7 : b+=(k[6]<<16);
   case 6 : b+=(k[5]<<8);
   case 5 : b+=k[4];
   case 4 : a+=(k[3]<<24);
   case 3 : a+=(k[2]<<16);
   case 2 : a+=(k[1]<<8);
   case 1 : a+=k[0];
   }
   mixc(a,b,c,d,e,f,g,h);
   mixc(a,b,c,d,e,f,g,h);
   mixc(a,b,c,d,e,f,g,h);
   mixc(a,b,c,d,e,f,g,h);

   /*-------------------------------------------- report the result */
   state[0]=a; state[1]=b; state[2]=c; state[3]=d;
   state[4]=e; state[5]=f; state[6]=g; state[7]=h;
}
