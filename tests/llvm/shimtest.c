#include "hb_lldshim.h"
#include <stdio.h>

int main( void )
{
   const char * argv[] = {
      "ld.lld", "-o", "build/shimtest_out.exe", "build/shimtest_obj.o"
   };
   int rc = hb_lld_link_mingw( 4, argv );
   printf( "lld returned %d\n", rc );
   return rc;
}
