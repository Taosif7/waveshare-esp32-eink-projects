CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -I../include

.PHONY: test clean

test: build/test_pond
	./build/test_pond

build/test_pond: test_pond.cpp ../src/pond_proto.cpp ../src/pond_world.cpp ../src/koi_model.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ test_pond.cpp ../src/pond_proto.cpp ../src/pond_world.cpp ../src/koi_model.cpp

clean:
	rm -rf build
