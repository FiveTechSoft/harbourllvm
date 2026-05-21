function Main()
   local oErr := ErrorNew()
   oErr:description := "disk full"
   oErr:subcode := 42
   ? oErr:description
   ? oErr:subcode
   ? UseObj( oErr )
   return nil

function UseObj( o )
   return o:subcode + 1
