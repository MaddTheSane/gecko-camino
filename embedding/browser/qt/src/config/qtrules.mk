ui_%.h: %.ui Makefile Makefile.in
	$(UICEX) $< -o $@

ui_%.cpp: %.ui ui_%.h Makefile Makefile.in
	$(UICEX) $< -i $(<:%.ui=ui_%.h) -o $@

uiimg_%.cpp: $(IMAGES) Makefile Makefile.in
	$(UICEX) -embed $(PROGRAM) $(IMAGES) -o $@

libs::
ifdef IS_QTDESIGNERPLUGIN
	@if test ! -d $(DIST_QTDESIGNERPLUGINS); then echo Creating $(DIST_QTDESIGNERPLUGINS); rm -rf $(DIST_QTDESIGNERPLUGINS); $(NSINSTALL) -D $(DIST_QTDESIGNERPLUGINS); else true; fi 
	$(INSTALL) $(SHARED_LIBRARY) $(DIST_QTDESIGNERPLUGINS)
endif

