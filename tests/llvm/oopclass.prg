#include "hbclass.ch"

function Main()
   local oPt := Point():New( 3, 4 )
   oPt:Move( 10, 20 )
   ? oPt:X, oPt:Y
   with object oPt
      :Move( 1, 1 )
      ? :X, :Y
   end
   return nil

CREATE CLASS Point
   VAR X
   VAR Y
   METHOD New( nX, nY )
   METHOD Move( dX, dY )
ENDCLASS

METHOD New( nX, nY ) CLASS Point
   ::X := nX
   ::Y := nY
   return Self

METHOD Move( dX, dY ) CLASS Point
   ::X := ::X + dX
   ::Y := ::Y + dY
   return Self
