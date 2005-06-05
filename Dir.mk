install-data:
	$(SRCDIR_INSTALL) --fmode=0755 $(pkgdatadir)/example example/cmus-status-display

subdirs	:= common cmus remote doc
