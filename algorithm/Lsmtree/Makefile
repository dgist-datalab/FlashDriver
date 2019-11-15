L_TARGET=array
AL_PWD=$(PWD)

ifeq ($(CC),cc)
 CC=gcc
 export CFLAGS_ALGO=\
		 -g\
		 -Wall\

 ifeq ($(CC),gcc)
  CFLAGS_ALGO+=\
			   -Wno-discarded-qualifiers\
			   -std=c99\
			   -DLsmtree\

 else
  CFLAGS_ALGO+=\
			   -DLsmtree\

 endif
endif



SRCS +=\
	   skiplist.c\
	   bloomfilter.c\
	   lsmtree.c\
	   compaction_io_module.c\
	   compaction_assistant.c\
	   pipeline_compaction2.c\
	   hw_compaction.c\
	   compaction.c\
	   page.c\
	   nocpy.c\
	   lops_default.c\
	   cache.c\
	   page_kv.c\
	   variable.c\
	   lsmtree_multi.c\
	   lsmtree_scheduling.c\
	   data_simul.c\
	   lsmtree_params_settings.c\
#merge_compaction.c\
	   run_array.c\
	   lsmtree_mop.c\
	   lsmtree_iter.c\
	   log_list.c\


OBJS :=\
	$(patsubst %.c,%.o,$(SRCS))\

TARGETOBJ:=\
	$(addprefix ../../object/,$(OBJS))\
#$(patsubst %.c,%.o,$(SRCS))\

DOBJ:=\
	$(patsubst %.c,%_d.o,$(SRCS))\

TARGETDOBJ=\
	$(addprefix ../../object/,$(DOBJ))\


MOBJS :=\
	$(patsubst %.c,%_m.o,$(SRCS))\

TARGETMOBJ:=\
	$(addprefix ../../object/,$(MOBJS))\


all:$(TARGETOBJ) set_check

DEBUG: $(TARGETDOBJ)

LEAK: $(TARGETMOBJ)

liblsm.a : $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)

set_check:../../include/lsm_settings.h
	$(MAKE) -C $(AL_PWD)/level_target/$(L_TARGET)

.c.o :  ../../include/lsm_settings.h ../../include/settings.h
	$(CC) $(CFLAGS_ALGO) -c $< -o $@ -lm -lpthread -DLsmtree

../../object/%_d.o: %.c ../../include/lsm_settigs.h ../../include/settings.h
	$(CC) $(CFLAGS_ALGO) -DDEBUG -c $< -o $@ -lm -DLsmtree

../../object/%_m.o: %.c ../../include/lsm_settings.h ../../include/settings.h
	$(CC) $(CFLAGS_ALGO) -DLEAKCHECK -c $< -o $@ -lm -DLsmtree

../../object/%.o: %.c  ../../include/lsm_settings.h ../../include/settings.h
	$(CC) $(CFLAGS) $(CFLAGS_ALGO) -c $< -o $@ -lm -DLsmtree

run_test:run_array.c
	$(CC) $(CFLAGS_ALGO) -o $@ $^

test_clean :
	@$(RM) ../../object/*

test : liblsm.a
	$(CC) $(CFLAGS_ALGO) -o ttt liblsm.a test.c -lpthread

clean : 
	@$(RM) liblsm*
	@$(RM) *.o
