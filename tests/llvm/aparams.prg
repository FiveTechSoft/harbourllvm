function Main()
   local a := { 10, 20, 30 }
   ? Sum3( HB_ARRAYTOPARAMS( a ) )
   return nil

function Sum3( x, y, z )
   return x + y + z
