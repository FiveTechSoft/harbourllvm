#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p = ( void * ) hb_vmsh_switchidx;
   printf( "group F shim linkable: %p\n", p );
   return 0;
}
