test_programs = \
	test-gtd-simple-test \
	$(NULL)

TESTS = test-gtd-simple-test
test_gtd_simple_test_SOURCES = test-gtd-simple-test.c
test_gtd_simple_test_CFLAGS = $(GNOME_TODO_CFLAGS)
test_gtd_simple_test_LDADD = $(GNOME_TODO_LIBS)

if ENABLE_TESTS
noinst_PROGRAMS = $(TESTS)
endif

check_PROGRAMS = $(TESTS)

installed_test_programs = \
	test-gtd-simple-test \
	$(NULL)

if ENABLE_INSTALLED_TESTS
installed_test_PROGRAMS = \
	$(installed_test_programs) \
	$(NULL)

installed_test_meta_DATA = $(installed_test_programs:=.test)

%.test: %$(EXEEXT) Makefile
	$(AM_V_GEN) (echo '[Test]' > $@.tmp; \
	echo 'Type=session' >> $@.tmp; \
	echo 'Exec=$(installed_testdir)/$(notdir $<) --tap' >> $@.tmp; \
	echo 'Output=TAP' >> $@.tmp; \
	mv $@.tmp $@)

CLEANFILES = $(installed_test_meta_DATA)
endif
