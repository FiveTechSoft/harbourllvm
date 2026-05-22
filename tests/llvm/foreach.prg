function Main()
   local x, nSum := 0, cJoin := "", nDesc := 0
   for each x in { 10, 20, 30, 40 }
      nSum += x
   next
   for each x in "abc"
      cJoin += x
   next
   for each x in { 1, 2, 3, 4, 5 } descend
      nDesc := nDesc * 10 + x
   next
   ? nSum
   ? cJoin
   ? nDesc
   return nil
