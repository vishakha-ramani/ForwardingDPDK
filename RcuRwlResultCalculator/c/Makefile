CC = g++
CFLAGS  = -g -Wall -std=c++11 -O2
LIBS =

TARGET = calculator
SOURCES = calculator.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LIBS)

clean:
	$(RM) $(TARGET)
