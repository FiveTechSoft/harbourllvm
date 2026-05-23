#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[] = {
      ( void * ) hb_vmsh_seqbegin,
      ( void * ) hb_vmsh_seqend,
      ( void * ) hb_vmsh_seqrecover,
      ( void * ) hb_vmsh_seqalways,
      ( void * ) hb_vmsh_alwaysbegin,
      ( void * ) hb_vmsh_alwaysend,
      ( void * ) hb_vmsh_seqblock
   };
   int i;
   for( i = 0; i < ( int )( sizeof( p ) / sizeof( p[ 0 ] ) ); ++i )
      printf( "group I shim %d linkable: %p\n", i, p[ i ] );
   return 0;
}
