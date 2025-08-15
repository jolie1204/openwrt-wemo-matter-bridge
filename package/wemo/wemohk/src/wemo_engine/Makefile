LINK = $(CC)

LFLAGS += -pthread -shared

TARGET = libwemoengine.so

SRCS = $(wildcard *.c)
OBJS = ${SRCS:.c=.o}

LIBS=
INCLUDES=

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LINK) $(LFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -fPIC $(INCLUDES) -c $< -o $@

clean:

	$(RM) *.o *~ $(TARGET)
