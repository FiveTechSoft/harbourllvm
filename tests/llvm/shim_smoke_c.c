#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[ 7 ];
   p[ 0 ] = ( void * ) hb_vmsh_pushalias;
   p[ 1 ] = ( void * ) hb_vmsh_pushfield;
   p[ 2 ] = ( void * ) hb_vmsh_popfield;
   p[ 3 ] = ( void * ) hb_vmsh_pushmemvar;
   p[ 4 ] = ( void * ) hb_vmsh_popmemvar;
   p[ 5 ] = ( void * ) hb_vmsh_pushaliasedfield;
   p[ 6 ] = ( void * ) hb_vmsh_pushvariable;
   printf( "group C shims linkable: %p %p %p %p %p %p %p\n",
           p[0], p[1], p[2], p[3], p[4], p[5], p[6] );
   return 0;
}
