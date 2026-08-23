# Makefile for vdsrcswitch macOS port

SWIFTC = swiftc
SDK_PATH = $(shell xcrun --show-sdk-path)
SWIFT_FLAGS = -sdk $(SDK_PATH) -framework Cocoa -framework IOKit -framework CoreGraphics -O
TARGET_DIR = bin
TARGET = $(TARGET_DIR)/vdsrcswitch_macos
SOURCES = $(wildcard src_macos/*.swift)

.PHONY: all build run clean install uninstall

all: build

build: $(TARGET)

$(TARGET): $(SOURCES)
	@mkdir -p $(TARGET_DIR)
	$(SWIFTC) $(SWIFT_FLAGS) -o $(TARGET) $(SOURCES)

run: build
	sudo $(TARGET)

clean:
	rm -rf $(TARGET_DIR)/vdsrcswitch_macos*

install: build
	@echo "Installing launch agent..."
	@mkdir -p ~/Library/LaunchAgents
	@cp $(TARGET) /usr/local/bin/vdsrcswitch_macos || sudo cp $(TARGET) /usr/local/bin/vdsrcswitch_macos
	@cp src_macos/com.vdsrcswitch.daemon.plist ~/Library/LaunchAgents/
	launchctl bootstrap gui/$(shell id -u) ~/Library/LaunchAgents/com.vdsrcswitch.daemon.plist

uninstall:
	@echo "Uninstalling launch agent..."
	launchctl bootout gui/$(shell id -u) ~/Library/LaunchAgents/com.vdsrcswitch.daemon.plist || true
	rm -f ~/Library/LaunchAgents/com.vdsrcswitch.daemon.plist
	rm -f /usr/local/bin/vdsrcswitch_macos
