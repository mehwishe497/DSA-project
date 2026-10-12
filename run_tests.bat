@echo off
copy source.bin source_demo.bin >nul

echo ===== 1 nested func =====
(
echo func a
echo func b
echo func_end
echo func_end
) > source.bin
server.exe

echo ===== 2 func never closed =====
(
echo func main
echo set a 1
) > source.bin
server.exe

echo ===== 3 extra func_end =====
(
echo func main
echo func_end
echo func_end
) > source.bin
server.exe

echo ===== 4 no main =====
(
echo func foo
echo func_end
) > source.bin
server.exe

echo ===== 5 call undefined function =====
(
echo func main
echo call xyz
echo func_end
) > source.bin
server.exe

echo ===== 6 division by zero =====
(
echo func main
echo set a 5
echo div a 0
echo func_end
) > source.bin
server.exe

echo ===== 7 recursion / stack overflow =====
(
echo func main
echo call main
echo func_end
) > source.bin
server.exe

echo ===== 8 wrong number of arguments =====
(
echo func foo a b
echo func_end
echo func main
echo call foo 1
echo func_end
) > source.bin
server.exe

echo ===== 9 demo program (should work) =====
copy source_demo.bin source.bin >nul
server.exe

del source_demo.bin
echo ===== done, source.bin restored =====