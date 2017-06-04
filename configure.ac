dnl -*- mode: m4 -*-
AC_PREREQ([2.69])
AC_INIT([gnome-todo],[3.25.1],[https://bugzilla.gnome.org/enter_bug.cgi?product=gnome-todo],[gnome-todo])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([1.11.1 foreign -Wno-portability dist-xz no-dist-gzip tar-ustar subdir-objects])
AM_MAINTAINER_MODE([enable])
AM_SILENT_RULES([yes])

# Releases do not have git directories
AX_IS_RELEASE([git-directory])

## don't rerun to this point if we abort
AC_CACHE_SAVE

# We require additional macros later on
AX_REQUIRE_DEFINED([APPSTREAM_XML])

# Add debug symbol
AX_CHECK_ENABLE_DEBUG([yes], [GNOME_TODO_ENABLE_DEBUG])

AC_ARG_ENABLE([tests],
              AS_HELP_STRING([--enable-tests],
                             [Enable always building tests during 'make all' [default=no]]),
              [enable_tests=$enableval], [enable_tests=yes])
AM_CONDITIONAL([ENABLE_TESTS], test "$enable_tests" = "yes")

AC_ARG_ENABLE(installed-tests,
              AS_HELP_STRING([--enable-installed-tests],
                             [Enable installation of some test cases]),
              [enable_installed_tests=$enableval], [enable_installed_tests=yes])
AM_CONDITIONAL([ENABLE_INSTALLED_TESTS], test "$enable_installed_tests" = "yes")

if test "$enable_installed_tests" = "yes"; then
  AC_SUBST(installed_test_metadir, [${datadir}/installed-tests/]AC_PACKAGE_NAME)
  AC_SUBST(installed_testdir, [${libexecdir}/installed-tests/]AC_PACKAGE_NAME)
fi

# Enable compiler warnings
AX_COMPILER_FLAGS([GNOME_TODO_WARN_CFLAGS], [GNOME_TODO_WARN_LDFLAGS])

AC_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_SED

# enable libtool
LT_PREREQ([2.2.6])
LT_INIT([disable-static])

## don't rerun to this point if we abort
AC_CACHE_SAVE

PKG_PROG_PKG_CONFIG

GLIB_GSETTINGS

dnl ***************************************************************************
dnl Internationalization
dnl ***************************************************************************
GETTEXT_PACKAGE=AC_PACKAGE_TARNAME
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [GETTEXT package name])

AS_IF([test "x$USE_NLS" != "xno"],
      [AC_DEFINE([ENABLE_NLS], [1], [Native language support is enabled])])

AM_GNU_GETTEXT_VERSION([0.19.8])
AM_GNU_GETTEXT([external])


dnl ================================================================
dnl GObject introspection support
dnl ================================================================
GOBJECT_INTROSPECTION_CHECK([1.42.0])


dnl ================================================================
dnl Misc
dnl ================================================================
AC_PATH_PROG(GLIB_MKENUMS, glib-mkenums)

PKG_CHECK_VAR([GLIB_COMPILE_RESOURCES], [gio-2.0], [glib_compile_resources])
AC_SUBST([GLIB_COMPILE_RESOURCES])

PKG_CHECK_MODULES(GNOME_TODO,
                  gmodule-export-2.0
                  gio-2.0 >= 2.43.4
                  glib-2.0 >= 2.43.4
                  goa-1.0 >= 3.2.0
                  gtk+-3.0 >= 3.22.0
                  libecal-1.2 >= 3.13.90
                  libedataserver-1.2 >= 3.17.1
                  libedataserverui-1.2 >= 3.17.1
                  libical >= 0.43
                  libpeas-1.0 >= 1.17)

APPSTREAM_XML


dnl ================================================================
dnl Plugins
dnl ================================================================
GNOME_TODO_ADD_PLUGIN([eds], [Evolution-Data-Server], [yes])

GNOME_TODO_ADD_PLUGIN([background], [Run In Background], [yes])
GNOME_TODO_ADD_PLUGIN([dark-theme], [Dark theme], [yes])
GNOME_TODO_ADD_PLUGIN([scheduled-panel], [Scheduled Panel], [yes])
GNOME_TODO_ADD_PLUGIN([score], [Score], [yes])
GNOME_TODO_ADD_PLUGIN([today-panel], [Today Panel], [yes])
GNOME_TODO_ADD_PLUGIN([unscheduled-panel], [Unscheduled Tasks Panel], [yes])
GNOME_TODO_ADD_PLUGIN([todo-txt], [Todo.Txt], [yes])


dnl ***********************************************************************
dnl Support for gtk-doc Documentation Engine
dnl ***********************************************************************
GTK_DOC_CHECK([1.14],[--flavour no-tmpl])
AM_CONDITIONAL(ENABLE_GTK_DOC, test "x$enable_gtk_doc" = "xyes")
AC_ARG_ENABLE(doc-cross-references,
              AS_HELP_STRING([--disable-doc-cross-references],
                             [cross reference symbols from other libraries @<:@default=yes@:>@]),
              enable_doc_cross_references=$enableval,
              enable_doc_cross_references=yes)
AM_CONDITIONAL(ENABLE_DOC_CROSS_REFERENCES, test x$enable_doc_cross_references != xno)


AC_CONFIG_FILES([
      Makefile
      src/Makefile
      tests/Makefile
      data/Makefile
      data/icons/Makefile
      data/gnome-todo.pc
      doc/Makefile
      doc/reference/Makefile
      doc/reference/version.xml
      plugins/Makefile
      po/Makefile.in
])

AC_OUTPUT

dnl ==========================================================================
echo "

        gnome-todo $VERSION
        ==================

        prefix:          ${prefix}
        compiler:        ${CC}
        global flags:    ${CFLAGS} ${CPPFLAGS} ${LDFLAGS}
        release:         ${ax_is_release}
        documentation:   ${enable_gtk_doc}
        build Test Suite ${enable_tests}
        Installed tests  ${enable_installed_tests}


    Plugins:
        Dark theme .............. ${enable_dark_theme_plugin}
        Run in Background ....... ${enable_background_plugin}
        Scheduled panel ......... ${enable_scheduled_panel_plugin}
        Score ................... ${enable_score_plugin}
        Today panel ............. ${enable_today_panel_plugin}
        Unscheduled panel ....... ${enable_unscheduled_panel_plugin}
        Todo.txt ................ ${enable_todo_txt_plugin}

        Now type 'make' to build $PACKAGE
"
