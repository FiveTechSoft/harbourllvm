function Main()
   local a := { 1, 2, 3 }
   Bump( @a[ 2 ] )
   ? a[ 2 ]
   return nil

function Bump( x )
   x := x + 100
   return nil
