release	:= $(PACKAGE)-$(VERSION)
tmpdir	:= /tmp
tarball	:= $(DISTDIR)/$(release).tar.bz2

release: extra cmus.spec
	@dir=$(tmpdir)/$(release); \
	if [[ -e $$dir ]]; \
	then \
		echo "$$dir exists" >&2; \
		exit 1; \
	fi; \
	if [[ -e $(tarball) ]]; \
	then \
		echo -n "\`$(tarball)' already exists. overwrite? [n] "; \
		read key; \
		case $$key in y|Y) ;; *) exit 0; ;; esac; \
	fi; \
	echo "   DIST   $(tarball)"; \
	mkdir -p $$dir || exit 1; \
	export GIT_INDEX_FILE=$$dir/.git-index; \
	git-read-tree HEAD || exit 1; \
	git-checkout-cache --prefix=$$dir/ -a || exit 1; \
	rm $$GIT_INDEX_FILE; \
	cd $(tmpdir) || exit 1; \
	cp $(top_srcdir)/doc/cmus.html $(release)/doc || exit 1; \
	cp $(top_builddir)/cmus.spec $(release) || exit 1; \
	tar -c $(release) | bzip2 -9 > $(tarball) || rm -f $(tarball); \
	rm -rf $(release)

install-data:
	$(SRCDIR_INSTALL) --fmode=0755 $(pkgdatadir)/example example/cmus-status-display
	$(SRCDIR_INSTALL) --fmode=0644 $(pkgdatadir) keybindings

subdirs	:= common cmus ip op remote doc
