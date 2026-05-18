/* linktest.c — standalone test for hb_llvmLinkExe.
 * Usage: linktest <in.o> <libdir> <out.exe>
 * Links <in.o> against the Harbour runtime under <libdir> and writes <out.exe>.
 * Expected exit 0 and a runnable executable. */
#include "hb_llvmobj.h"
#include <stdio.h>

int main( int argc, char ** argv )
{
   if( argc != 4 )
   {
      fprintf( stderr, "usage: linktest <in.o> <libdir> <out.exe>\n" );
      return 2;
   }
   return hb_llvmLinkExe( argv[ 1 ], argv[ 2 ], argv[ 3 ] );
}
