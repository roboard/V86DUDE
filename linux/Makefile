v86dude.exe : v86dude.o
	gcc -o v86dude.exe v86dude.o -lstdc++
v86dude.o : ../v86dude.cpp
	gcc -c ../v86dude.cpp -I../

.PHONY : clean
clean :
	-rm v86dude.o v86dude.exe
