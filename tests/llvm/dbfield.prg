function Main()
   local cFile := "build/grpc_test.dbf"
   dbCreate( cFile, { { "NAME", "C", 10, 0 }, { "AGE", "N", 3, 0 } } )
   dbUseArea( .T., , cFile, "T" )
   dbAppend()
   T->NAME := "Harbour"
   T->AGE  := 35
   ? T->NAME, T->AGE
   dbCloseArea()
   FErase( cFile )
   return nil
