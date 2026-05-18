#include "hb_llvmobj.h"
#include <stdio.h>

int main( int argc, char ** argv )
{
   if( argc != 3 )
   {
      fprintf( stderr, "usage: objtest <in.ll> <out.o>\n" );
      return 2;
   }
   return hb_llvmEmitObject( argv[ 1 ], argv[ 2 ] );
}
