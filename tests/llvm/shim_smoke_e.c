#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[ 4 ];
   p[ 0 ] = ( void * ) hb_vmsh_enumstart;
   p[ 1 ] = ( void * ) hb_vmsh_enumnext;
   p[ 2 ] = ( void * ) hb_vmsh_enumprev;
   p[ 3 ] = ( void * ) hb_vmsh_enumend;
   printf( "group E shims linkable: %p %p %p %p\n", p[0], p[1], p[2], p[3] );
   return 0;
}
