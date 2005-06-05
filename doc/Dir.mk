docdir		:= $(pkgdatadir)/doc

HTML		:= manual.html
CSS		:= $(srcdir)/default.css

RST2HTML_FLAGS	:= --strict --no-toc-backlinks --generator --date --stylesheet-path=$(CSS) --embed-stylesheet

manual.html: manual.rst $(CSS)
	$(call cmd,rst)
	cp manual.html $(srcdir)/cmus.html

install-data:
	$(SRCDIR_INSTALL) $(docdir) cmus.html

extra-targets	:= $(HTML)
