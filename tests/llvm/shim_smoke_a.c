#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[ 6 ];
   p[ 0 ] = ( void * ) hb_vmsh_fortest;
   p[ 1 ] = ( void * ) hb_vmsh_pluseqpop;
   p[ 2 ] = ( void * ) hb_vmsh_inceq;
   p[ 3 ] = ( void * ) hb_vmsh_pushlocalref;
   p[ 4 ] = ( void * ) hb_vmsh_localinc;
   p[ 5 ] = ( void * ) hb_vmsh_localaddint;
   printf( "group A shims linkable: %p %p %p %p %p %p\n",
           p[0], p[1], p[2], p[3], p[4], p[5] );
   return 0;
}
