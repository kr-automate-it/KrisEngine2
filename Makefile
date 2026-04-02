CXX = /c/msys64/ucrt64/bin/g++
CXXFLAGS = -std=c++20 -O3 -march=native -flto -DNDEBUG -fpermissive
LDFLAGS = -lpthread -static

SRC = main.cpp bitboard.cpp position.cpp movegen.cpp eval.cpp search.cpp uci.cpp tt.cpp zobrist.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = engine.exe

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

debug: CXXFLAGS = -std=c++20 -g -O0 -Wall -Wextra
debug: clean all

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean debug
