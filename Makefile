sssp: src/main.cpp 
	mpic++ -std=c++17 -O3 -Wall -Werror $^ -o $@ -lm -Wno-sign-compare