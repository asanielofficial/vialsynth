ifndef CONFIG
  CONFIG=Release
endif

all: binary ttl_generator lv2

binary:
	$(MAKE) -f Makefile.binary CONFIG=$(CONFIG) DEBCXXFLAGS="$(DEBCXXFLAGS)" DEBLDFLAGS="$(DEBLDFLAGS)"

ttl_generator:
	$(MAKE) -f Makefile.ttl_generator CONFIG=$(CONFIG)

lv2: ttl_generator binary
	./lv2_ttl_generator build/VialSynth.so
	mkdir -p VialSynth.lv2
	cp build/VialSynth.so VialSynth.lv2
	mv *.ttl VialSynth.lv2

clean:
	$(MAKE) clean CONFIG=$(CONFIG) -f Makefile.binary
	$(MAKE) clean CONFIG=$(CONFIG) -f Makefile.ttl_generator
	rm -rf build
	rm -rf VialSynth.lv2
