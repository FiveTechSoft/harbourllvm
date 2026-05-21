function Main()
   local i, nSum := 0
   for i := 10 to 1 step -2
      nSum += i
   next
   for i := 0 to 20 step 5
      nSum += i
   next
   ? nSum
   return nil
