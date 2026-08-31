# Makefile for vdsrcswitch macOS port

SWIFTC = swiftc
SDK_PATH = $(shell xcrun --show-sdk-path)
SWIFT_FLAGS = -sdk $(SDK_PATH) -framework Cocoa -framework IOKit -framework CoreGraphics -O
TARGET_DIR = bin
APP_BUNDLE = $(TARGET_DIR)/vdsrcswitch.app
SOURCES = $(wildcard src_macos/*.swift)

.PHONY: all build run clean install uninstall

all: build

build: $(SOURCES) src_macos/Info.plist src_macos/Resources/AppIcon.icns
	@echo "Compiling and packaging vdsrcswitch.app..."
	@mkdir -p $(APP_BUNDLE)/Contents/MacOS
	@mkdir -p $(APP_BUNDLE)/Contents/Resources
	$(SWIFTC) $(SWIFT_FLAGS) -o $(APP_BUNDLE)/Contents/MacOS/vdsrcswitch $(SOURCES)
	@cp src_macos/Info.plist $(APP_BUNDLE)/Contents/Info.plist
	@cp src_macos/Resources/AppIcon.icns $(APP_BUNDLE)/Contents/Resources/AppIcon.icns
	@echo "Build successful! App Bundle created: $(APP_BUNDLE)"

run: build
	$(APP_BUNDLE)/Contents/MacOS/vdsrcswitch

clean:
	rm -rf $(TARGET_DIR)

install: build
	@echo "Installing launch agent..."
	@make uninstall
	@cp -R $(APP_BUNDLE) /Applications/
	@mkdir -p ~/Library/LaunchAgents
	@cp src_macos/com.vdsrcswitch.daemon.plist ~/Library/LaunchAgents/
	launchctl bootstrap gui/$(shell id -u) ~/Library/LaunchAgents/com.vdsrcswitch.daemon.plist
	@echo "Installation successful! The app is now running and registered for startup."

uninstall:
	@echo "Uninstalling launch agent and app..."
	@launchctl bootout gui/$(shell id -u) ~/Library/LaunchAgents/com.vdsrcswitch.daemon.plist 2>/dev/null || true
	@rm -f ~/Library/LaunchAgents/com.vdsrcswitch.daemon.plist
	@rm -rf /Applications/vdsrcswitch.app
	@rm -f /usr/local/bin/vdsrcswitch_macos
	@echo "Uninstall successful."

