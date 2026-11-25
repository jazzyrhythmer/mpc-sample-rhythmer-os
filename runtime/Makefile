# Rhythmer on the sample-base runtime: the base layers come from the base/
# submodule, the engine, model and files from the core/ submodule
# (ellic-core, shared with rhythmer-move), Rhythmer's screens and main.cpp
# from src/. The build is base/base.mk.
BASE_SRC := base/src
TARGET := build/rhythmer
APP_SOURCES := $(wildcard src/*.cpp src/*/*.cpp src/*/*/*.cpp)
include base/base.mk

# The core's objects sit beside Rhythmer's own in build/obj/rhythmer/, as
# when they lived in src/ (layout test: no file in both).
CORE := core
include core/core.mk
CORE_OBJECTS := $(patsubst core/src/%.cpp,build/obj/%.o,$(CORE_SOURCES))
OBJECTS += $(CORE_OBJECTS)
$(TARGET): $(CORE_OBJECTS)
CPPFLAGS += $(CORE_CPPFLAGS)
build/obj/%.o: core/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wall -Wextra -Werror -c $< -o $@
# An object built before the core moved to core/ names its source in src/:
# forgotten, so that it is built again from the core.
STALE := $(shell grep -lsE '(^|[ :])src/[^ ]*\.cpp' $(CORE_OBJECTS:.o=.d))
$(foreach d,$(STALE),$(shell rm -f $(d) $(d:.d=.o)))
-include $(CORE_OBJECTS:.o=.d)

# The version shown on the Credits page and used to compare updates
# (shell/update.cpp): ./release.sh passes its VERSION, build.sh passes git
# describe, plain make uses "dev". The stamp file changes only when the
# version changes, so update.cpp is recompiled only then.
RHYTHMER_VERSION ?= dev
build/gen/rhythmer_version: FORCE
	@mkdir -p build/gen
	@printf '%s\n' '$(RHYTHMER_VERSION)' > $@.new
	@if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi
build/obj/rhythmer/shell/update.o: build/gen/rhythmer_version
build/obj/rhythmer/shell/update.o: CPPFLAGS += -DRHYTHMER_VERSION='"$(RHYTHMER_VERSION)"'
.PHONY: FORCE
FORCE:

# The web page's files (src/rhythmer/web/web_bundle.h): its own pages, the
# quickstart and the user guide as HTML, and the images they show, in one
# file the binary carries.
WEB_INPUTS := tools/web_bundle.py $(wildcard src/rhythmer/web/static/*) docs/QUICKSTART.md docs/GUIDE.md \
	$(wildcard docs/guide/*.md) $(wildcard docs/images/*/*.png docs/images/*.png)
build/gen/web_bundle.bin: $(WEB_INPUTS)
	@mkdir -p build/gen
	python3 tools/web_bundle.py $@
build/obj/rhythmer/web/web_bundle.o: build/gen/web_bundle.bin

# The MPC 3 export's template (the core's tools/mpc3_template.py), carried in the binary.
build/obj/rhythmer/shell/mpc3_export.o: $(CORE_TEMPLATE)
build/obj/rhythmer/shell/mpc3_export.o: CXXFLAGS += $(CORE_ASFLAGS)

# JUCE's decoders, for an SFZ's FLAC, Ogg and AIFF samples and an MPC
# expansion's MP3 previews. Rhythmer's alone, so the base stays as it is.
FORMATS := $(JUCE)/juce_audio_formats
FORMAT_FLAGS := -DJUCE_USE_MP3AUDIOFORMAT=1 -DJUCE_USE_WINDOWS_MEDIA_FORMAT=0 -DJUCE_USE_OPUS=0
FORMAT_OBJECTS := build/obj/juce_audio_formats.o build/obj/juce_audio_formats_flac_1.o build/obj/juce_audio_formats_flac_2.o build/obj/juce_audio_formats_ogg.o
OBJECTS += $(FORMAT_OBJECTS)
$(TARGET): $(FORMAT_OBJECTS)
build/obj/juce_audio_formats.o: $(FORMATS)/juce_audio_formats.cpp
	@mkdir -p build/obj
	$(CXX) $(CPPFLAGS) $(FORMAT_FLAGS) $(CXXFLAGS) -w -c $< -o $@
build/obj/juce_audio_formats_%.o: $(FORMATS)/juce_audio_formats_%.c
	@mkdir -p build/obj
	$(CC) $(CPPFLAGS) $(FORMAT_FLAGS) $(CFLAGS) -w -c $< -o $@
build/obj/rhythmer/shell/sample_decode.o: CPPFLAGS += $(FORMAT_FLAGS)

# Plug-ins (docs/RHYTHMER.md, Plug-ins): JUCE's headless plug-in hosting with
# VST3 and LV2 (lilv and its libraries, bundled in JUCE; VST2, CLAP and the
# armv7 bridge are Rhythmer's own, src/rhythmer/plugin/; CLAP's headers from
# build/deps/clap, src/rhythmer/dependencies.py; the plug-in screen header,
# sdk/rhythmer-screen, MIT), and
# the support pack (GUI stubs, the bridge and its armhf runtime) linked into
# the binary. The pack needs Rhythmer's build image (tools/plugin_support/Dockerfile).
HOSTING := $(JUCE)/juce_audio_processors_headless
CPPFLAGS += -DJUCE_PLUGINHOST_VST3=1 -DJUCE_PLUGINHOST_LV2=1 -I$(HOSTING)/format_types/VST3_SDK -Ibuild/deps/clap/include -Isdk/rhythmer-screen
LV2_SDK := $(HOSTING)/format_types/LV2_SDK
LV2_INCLUDES := $(addprefix -I$(LV2_SDK),/ /lv2 /serd /sord /sord/src /sratom /lilv /lilv/src)
PLUGIN_OBJECTS := build/obj/juce_audio_processors_headless.o build/obj/juce_audio_processors_headless_lv2_libs.o build/obj/plugin_support_pack.o
OBJECTS += $(PLUGIN_OBJECTS)
$(TARGET): $(PLUGIN_OBJECTS)
# Makefile too: a hosting flag changed here builds them again.
build/obj/juce_audio_processors_headless.o build/obj/juce_audio_processors_headless_lv2_libs.o: build/obj/%.o: $(HOSTING)/%.cpp Makefile
	@mkdir -p build/obj
	$(CXX) $(CPPFLAGS) $(LV2_INCLUDES) $(CXXFLAGS) -w -c $< -o $@
build/plugin-support.pack: tools/plugin_support/build.py tools/plugin_support/make_stubs.py tools/plugin_support/Dockerfile \
        src/rhythmer/plugin/bridge32/vst2_bridge32.cpp src/rhythmer/plugin/vst2_abi.h src/rhythmer/plugin/vst2_bridge_protocol.h \
        sdk/rhythmer-screen/rhythmer_screen.h
	python3 tools/plugin_support/build.py build/plugin-support $@
build/obj/plugin_support_pack.o: src/rhythmer/plugin/bridge32/support_pack.S build/plugin-support.pack
	@mkdir -p build/obj
	$(CC) -c $< -o $@

LDLIBS += -Wl,--gc-sections
FM1_GEN_OBJ := build/obj/fm1-gen.o
OBJECTS += $(FM1_GEN_OBJ)
$(TARGET): $(FM1_GEN_OBJ)
$(FM1_GEN_OBJ): $(shell find fm1-gen -type f)
	@mkdir -p build/obj
	$(CC) -O1 -w -ffunction-sections -fdata-sections -DFM1_HOST_TEST=1 -Ifm1-gen/generated -Ifm1-gen/firmware/src -Ifm1-gen/tests -c fm1-gen/bridge.c -o $@
# Stems (src/rhythmer/shell/stems.h): the separating program and its model, files of their own in the release, not
# linked into Rhythmer (tools/stems/build.py: stems_engine.c on SpleeterRT's network code and OpenBLAS, and Deezer's
# 4-stem model, from build/deps). `make stems` builds them; the first time takes a few minutes.
STEMS := build/stems/rhythmer-stems
.PHONY: stems
stems: $(STEMS)
$(STEMS): tools/stems/build.py tools/stems/stems_engine.c tools/stems/convert_model.py
	python3 tools/stems/build.py build/stems

# CLI verification/preview helpers contain large assertion fixtures. Keep their
# compilation within the small build VM; real-time audio, shell and UI objects
# retain the production -O2 settings from base/base.mk.
build/obj/rhythmer/tools.o: CXXFLAGS += -O0
