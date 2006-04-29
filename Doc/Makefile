all: html

include ../config.mk
include ../scripts/lib.mk

MAN_XSL	:= /etc/asciidoc/docbook-xsl/manpage.xsl \
	/usr/share/asciidoc/docbook-xsl/manpage.xsl \
	/usr/local/share/asciidoc/docbook-xsl/manpage.xsl
MAN_XSL	:= $(word 1,$(wildcard $(MAN_XSL)))

man1	:= cmus.1 cmus-remote.1
html	:= cmus.html cmus-remote.html

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
	sed 's/\(.\)\.sp$$/\1\n.sp/g' < $@ > $@.tmp && mv $@.tmp $@

%.xml: %.txt
	$(call cmd,asciidoc)

install-man:
	$(INSTALL) -m644 $(mandir)/man1 $(man1)

install-html:
	$(INSTALL) -m644 $(datadir)/doc/cmus $(html)

clean	+= *.xml

realclean: clean
	rm -f *.html *.1

.PHONY: all man html realclean

quiet_cmd_xsltproc = XSLTPROC $@
      cmd_xsltproc = xsltproc -o $@ $(XSLTPROC_FLAGS) $<

quiet_cmd_asciidoc = ASCIIDOC $@
      cmd_asciidoc = asciidoc $(ASCIIDOC_FLAGS) $<
