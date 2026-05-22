function Main()
   local x, nSum := 0, cJoin := ""
   for each x in { 10, 20, 30, 40 }
      nSum += x
   next
   for each x in "abc"
      cJoin += x
   next
   ? nSum
   ? cJoin
   return nil
