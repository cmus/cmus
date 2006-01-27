all: man html

include ../config.mk
include ../scripts/lib.mk

MAN_XSL	:= /usr/share/asciidoc/docbook-xsl/manpages.xsl

man1	:= cmus-remote.1
html	:= cmus-remote.html
xml	:= cmus-remote.xml

man: $(man1)
html: $(html)

ASCIIDOC_FLAGS	:= -d manpage -f asciidoc.conf

XSLTPROC_FLAGS	+= --stringparam callout.graphics 0 --stringparam navig.graphics 0
XSLTPROC_FLAGS	+= --stringparam admon.textlabel 1 --stringparam admon.graphics 0
XSLTPROC_FLAGS	+= --nonet $(MAN_XSL)

$(html): ASCIIDOC_FLAGS += -b xhtml11
$(man1): ASCIIDOC_FLAGS += -b docbook

%.html: %.txt
	$(call cmd,asciidoc)

%.1: %.xml
	$(call cmd,xsltproc)

%.xml: %.txt
	$(call cmd,asciidoc)

install: install-man install-html

install-man: man
	$(INSTALL) -m644 $(datadir)/man/man1 $(man1)

install-html: html
	$(INSTALL) -m644 $(datadir)/doc/cmus $(html)

clean	+= *.html *.xml *.1

.PHONY: all man html

quiet_cmd_xsltproc = XSLTPROC $@
      cmd_xsltproc = xsltproc -o $@ $(XSLTPROC_FLAGS) $<

quiet_cmd_asciidoc = ASCIIDOC $@
      cmd_asciidoc = asciidoc $(ASCIIDOC_FLAGS) $<
