LINK = $(CC)

CFLAGS += -Wall
LDFLAGS += -pthread

TARGET = wemo_client

SRCS = $(wildcard *.c)
OBJS = ${SRCS:.c=.o}

LIBS = -lsqlite3 -lwemoengine -lixml -lcrypto -L../wemo_engine
INCLUDES = -I../wemo_engine

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LINK) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:

	$(RM) *.o *~ $(TARGET)
