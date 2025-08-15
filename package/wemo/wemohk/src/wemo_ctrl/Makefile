LINK = $(CC)

LFLAGS += -pthread

TARGET = wemo_ctrl

SRCS = $(wildcard *.c)
OBJS = ${SRCS:.c=.o}

LIBS = -lupnp -lsqlite3 -lixml -lbelkin_nvram

INCLUDES = -I../wemo_engine

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LINK) $(LDFLAGS) $(LFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:

	$(RM) *.o *~ $(TARGET)
