#include "hbvmsh.h"
#include <stdio.h>

/* Compile/link-only smoke test: proves the shim symbols are exported.
   Not executed (the VM is not initialised here). */
int main( void )
{
   void * p[ 4 ];
   p[ 0 ] = ( void * ) hb_vmsh_pushlocal;
   p[ 1 ] = ( void * ) hb_vmsh_plus;
   p[ 2 ] = ( void * ) hb_vmsh_function;
   p[ 3 ] = ( void * ) hb_vmsh_retvalue;
   printf( "shim symbols linkable: %p %p %p %p\n", p[0], p[1], p[2], p[3] );
   return 0;
}
