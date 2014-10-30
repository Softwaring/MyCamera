include Makefile.param


OBJS=$(SRCS:%.c=%.o)
INCS += -I$(SDK_PATH)/mpp/include
CFLAGS += $(INCS)

yan:$(OBJS)
	$(CC) $(CFLAGS) -lpthread -lm -o $@ $^ $(SDK_LIBS)

clean:
	-rm -rf $(OBJS)
