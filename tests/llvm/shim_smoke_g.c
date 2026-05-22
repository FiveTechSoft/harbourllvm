#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p = ( void * ) hb_vmsh_pushblock;
   printf( "group G shim linkable: %p\n", p );
   return 0;
}
