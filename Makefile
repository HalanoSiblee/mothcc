# ----------------------------------------------------------------------
#  Makefile for Mothcc — C/C++ comment stripper + prettifier
# ----------------------------------------------------------------------

CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O3 -march=native -flto -fno-plt \
            -fno-exceptions -fno-rtti -DNDEBUG \
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion
LDFLAGS  ?= -flto -Wl,-O1,--as-needed
LDLIBS   ?=

# Fall back to g++ if clang++ isn't installed
ifeq (, $(shell command -v $(CXX) 2>/dev/null))
  CXX := g++
endif

BIN      := mothcc
SRC      := main.cpp
OBJ      := $(SRC:.cpp=.o)
DEP      := $(OBJ:.o=.d)

# Install location: $HOME/.local/bin (XDG-style, no sudo needed)
PREFIX   ?= $(HOME)/.local
BINDIR   ?= $(PREFIX)/bin

# ----------------------------------------------------------------------
#  Targets
# ----------------------------------------------------------------------

.PHONY: all
all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@strip $(BIN) 2>/dev/null || true

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

# ----------------------------------------------------------------------
#  Debug / static / portable builds
# ----------------------------------------------------------------------

.PHONY: debug
debug: CXXFLAGS := -std=c++17 -O0 -g3 -fsanitize=address,undefined \
                   -fno-omit-frame-pointer -Wall -Wextra -Wpedantic
debug: LDFLAGS  := -fsanitize=address,undefined
debug: clean $(BIN)

.PHONY: static
static: CXXFLAGS += -static
static: LDFLAGS  += -static
static: clean $(BIN)

# Portable binary: no -march=native, runs on any x86-64 from ~2012
.PHONY: portable
portable: CXXFLAGS := -std=c++17 -O3 -march=x86-64-v2 -flto \
                     -fno-exceptions -fno-rtti -DNDEBUG \
                     -Wall -Wextra -Wpedantic
portable: clean $(BIN)

# ----------------------------------------------------------------------
#  Install / uninstall
# ----------------------------------------------------------------------

.PHONY: install
install: $(BIN)
	@mkdir -p $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	@echo "installed: $(DESTDIR)$(BINDIR)/$(BIN)"
	@case ":$$PATH:" in \
	  *:"$(BINDIR)":*) ;; \
	  *) echo "note: $(BINDIR) is not in your PATH"; \
	     echo "      add to ~/.bashrc or ~/.zshrc:"; \
	     echo "      export PATH=\"$(BINDIR):\$$PATH\"" ;; \
	esac

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	@echo "removed: $(DESTDIR)$(BINDIR)/$(BIN)"

# ----------------------------------------------------------------------
#  Housekeeping
# ----------------------------------------------------------------------

.PHONY: clean
clean:
	rm -f $(OBJ) $(DEP) $(BIN)

.PHONY: rebuild
rebuild: clean all

.PHONY: help
help:
	@echo "Targets:"
	@echo "  all        build $(BIN) (default)"
	@echo "  debug      -O0 + ASan/UBSan build"
	@echo "  static     fully static binary"
	@echo "  portable   -march=x86-64-v2, runs on older CPUs"
	@echo "  install    install to \$$(BINDIR) (default $(BINDIR))"
	@echo "  uninstall  remove installed binary"
	@echo "  clean      remove objects + binary"
	@echo "  rebuild    clean then all"
	@echo ""
	@echo "Variables:"
	@echo "  CXX=$(CXX)  CXXFLAGS=...  PREFIX=$(PREFIX)"