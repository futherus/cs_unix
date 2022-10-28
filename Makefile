
DIRS = $(wildcard */)

.SILENT: distclean

all:
	$(foreach dir, $(DIRS), cd $(dir) && make && cd -;)

distclean:
	$(foreach dir, $(DIRS), cd $(dir) && make distclean && cd -;)
