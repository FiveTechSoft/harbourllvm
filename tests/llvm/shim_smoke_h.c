#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[] = {
      ( void * ) hb_vmsh_macropushindex,
      ( void * ) hb_vmsh_macropushref,
      ( void * ) hb_vmsh_macrosymbol,
      ( void * ) hb_vmsh_macrotext,
      ( void * ) hb_vmsh_macropop,
      ( void * ) hb_vmsh_macropopaliased,
      ( void * ) hb_vmsh_macropush,
      ( void * ) hb_vmsh_macropushlist,
      ( void * ) hb_vmsh_macropushpare,
      ( void * ) hb_vmsh_macropushaliased,
      ( void * ) hb_vmsh_macroarraygen,
      ( void * ) hb_vmsh_macrodo,
      ( void * ) hb_vmsh_macrofunc,
      ( void * ) hb_vmsh_macrosend
   };
   int i;
   for( i = 0; i < ( int )( sizeof( p ) / sizeof( p[ 0 ] ) ); ++i )
      printf( "group H shim %2d linkable: %p\n", i, p[ i ] );
   return 0;
}
