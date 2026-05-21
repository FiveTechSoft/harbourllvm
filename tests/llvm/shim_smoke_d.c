#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[ 8 ];
   p[ 0 ] = ( void * ) hb_vmsh_pushself;
   p[ 1 ] = ( void * ) hb_vmsh_pushovarref;
   p[ 2 ] = ( void * ) hb_vmsh_withobjectstart;
   p[ 3 ] = ( void * ) hb_vmsh_withobjectend;
   p[ 4 ] = ( void * ) hb_vmsh_funcptr;
   p[ 5 ] = ( void * ) hb_vmsh_message;
   p[ 6 ] = ( void * ) hb_vmsh_withobjectmessage;
   p[ 7 ] = ( void * ) hb_vmsh_send;
   printf( "group D shims linkable: %p %p %p %p %p %p %p %p\n",
           p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7] );
   return 0;
}
