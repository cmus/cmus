install-data:
	$(SRCDIR_INSTALL) --fmode=0755 $(pkgdatadir)/example example/cmus-status-display

subdirs	:= common cmus ip op remote doc
