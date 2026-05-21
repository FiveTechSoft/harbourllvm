#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[ 7 ];
   p[ 0 ] = ( void * ) hb_vmsh_arraypush;
   p[ 1 ] = ( void * ) hb_vmsh_arraypushref;
   p[ 2 ] = ( void * ) hb_vmsh_arraypop;
   p[ 3 ] = ( void * ) hb_vmsh_pushaparams;
   p[ 4 ] = ( void * ) hb_vmsh_arraydim;
   p[ 5 ] = ( void * ) hb_vmsh_arraygen;
   p[ 6 ] = ( void * ) hb_vmsh_hashgen;
   printf( "group B shims linkable: %p %p %p %p %p %p %p\n",
           p[0], p[1], p[2], p[3], p[4], p[5], p[6] );
   return 0;
}
