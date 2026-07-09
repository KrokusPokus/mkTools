console.log("mkFolderWidgetTiler: Initializing");

// Declare thse globally. They will be updated with the actual values by loadConfig().
var WINDOW_CLASS = "mkFolderWidget";
var MAX_WINDOWS = 4;
var COLUMN_WIDTH = 250;
var COLUMN_WIDTH_MIN = COLUMN_WIDTH;
var STAGGER_STEPS = 8;  // reset after this many steps
var STAGGER_OFFSET = 32; // offset per step
var staggerIndex = 0;

// Unser Dictionary: Key = Desktop-ID, Value = Array of windowsw
var managedWindowsPerDesktop = {};

function loadConfig() {
	WINDOW_CLASS = readConfig("WindowClass", "mkFolderWidget").toString();
	MAX_WINDOWS = readConfig("MaxStack", 4);
	COLUMN_WIDTH = readConfig("ColumnWidth", 250);
	COLUMN_WIDTH_MIN = COLUMN_WIDTH;

	// Hier Logik einfügen, um bestehende Fenster ggf. neu zu berechnen
	console.log("mkFolderWidgetTiler: loadConfig() with MAX_WINDOWS=" + MAX_WINDOWS + " COLUMN_WIDTH=" + COLUMN_WIDTH + " WINDOW_CLASS=" + WINDOW_CLASS);
}

loadConfig();

function isWindowMatch(w) {
	if (!w || !w.resourceClass) return false;
	if (!w.caption || !w.caption.startsWith("/")) return false;
	if (w.resourceClass.toString().indexOf(WINDOW_CLASS) == -1) return false;
	if (w.minimized) return false;
	if (w.modal) return false;
	return true;
}

function layoutManagedWindows(desktopKey) {
	var managedWindows = managedWindowsPerDesktop[desktopKey] || [];

	//console.log("mkFolderWidgetTiler: Tiling " + managedWindows.length + " windows on desktop " + desktopKey + " an.");

	if (managedWindows.length === 0) return;

	var area = workspace.clientArea(KWin.MaximizeArea, managedWindows[0]);
	var xPos = area.x + area.width - COLUMN_WIDTH;
	var heightPerWindow = area.height / managedWindows.length;

	for (var j = 0; j < managedWindows.length; j++) {
		var d = managedWindows[j];
		d.frameGeometry = {
			x: xPos,
			y: area.y + (j * heightPerWindow),
			width: COLUMN_WIDTH,
			height: heightPerWindow
		};
	}
}

workspace.windowAdded.connect(function(w) {
	if (!isWindowMatch(w)) return;

	var dKey = getDesktopKey(w);
	//console.log("mkFolderWidgetTiler: New window '" + w.caption + "' created on desktop '" + dKey + "'");

	// Cleanup
	if (!managedWindowsPerDesktop[dKey]) {
		managedWindowsPerDesktop[dKey] = [];
	}

	if (managedWindowsPerDesktop[dKey].length < MAX_WINDOWS) {
		managedWindowsPerDesktop[dKey].push(w);
		layoutManagedWindows(dKey);
	} else {
			var area = workspace.clientArea(KWin.MaximizeArea, w);
			var tilingLimitX = area.x + area.width - COLUMN_WIDTH;

			var newW = 640;
			var newH = 480;

			var baseX = tilingLimitX - newW;

			// Berechnung der Kaskade
			var step = staggerIndex % STAGGER_STEPS;
			var cycle = Math.floor(staggerIndex / STAGGER_STEPS);

			var offsetX = step * STAGGER_OFFSET;
			var offsetY = (step * STAGGER_OFFSET) + (cycle * STAGGER_OFFSET);

			var newX = baseX - offsetX;
			var newY = area.y + offsetY; // Wir starten oben an der Workspace-Area


			// Sicherheitscheck: Nicht links aus dem Bildschirm schieben
			if (newX < area.x) newX = area.x;
			// Sicherheitscheck: Nicht unten aus dem Bildschirm schieben
			if (newY + newH > area.y + area.height) newH = area.height - newY;

			w.frameGeometry = {
				x: newX,
				y: newY,
				width: newW,
				height: newH
			};

			// Counter erhöhen und bei STAGGER_STEPS wieder auf 0 setzen
			staggerIndex = (staggerIndex + 1) % (STAGGER_STEPS * STAGGER_STEPS);

			//console.log("mkFolderWidgetTiler: Overflow stagger " + staggerIndex);
		}

	// ---------------------------------------
	// Additional signals for this window

	// Signal that gets triggered when this window is being moved to a different desktop
	w.desktopsChanged.connect(function() {
		handleDesktopChange(w);
	});

	// Signal that gets triggered when this window is being released after dragging/resize
	w.interactiveMoveResizeFinished.connect(function() {
		var area = workspace.clientArea(KWin.MaximizeArea, w);
		var targetXR   = area.x + area.width;
		var managed = isAlreadyManaged(w);

		// Case A: Window's right border touches screen edge
		if (Math.abs(w.x + w.width - targetXR) <= 4) {
			// Cleanup
			if (!managedWindowsPerDesktop[dKey]) {
				managedWindowsPerDesktop[dKey] = [];
			}

			// If unmanaged and there's enough space OR if already managed, handle its placement
			if ((!managed && managedWindowsPerDesktop[dKey].length < MAX_WINDOWS) || managed) {
				if (managed && w.width != COLUMN_WIDTH) {
					// already managed window has been resized in width -> adjust COLUMN_WIDTH for all windows
					if (w.width > COLUMN_WIDTH_MIN) {
						COLUMN_WIDTH = w.width;
					} else {
						COLUMN_WIDTH = COLUMN_WIDTH_MIN;
					}
				}
				insertWindowByY(w, dKey);
				layoutManagedWindows(dKey);
			}
		}
		// Case B: Window currently managed, but right border does not touch screen edge anymore -> probably been dragged out of tiling zone -> release
		else if (managed) {
			releaseWindow(w);
		}
	});

/*
	// Previous method of group window raising: Use this in case reaction to stackingOrderChanged signal causes issues.
	// Signal that gets triggered when a window is activated
	w.activeChanged.connect(function() {
		if (w.active) {
			var currentDKey = getDesktopKey(w);
			var managedGroup = managedWindowsPerDesktop[currentDKey];

			if (managedGroup && isAlreadyManaged(w)) {
				// Raise all other windows of the managed group
				for (var i = 0; i < managedGroup.length; i++) {
					var otherW = managedGroup[i];
					if (otherW && otherW.internalId !== w.internalId) {
						workspace.raiseWindow(otherW);
					}
				}
			}
		}
	});
*/

	var isRaisingGroup = false; // prevent rekursion
	w.stackingOrderChanged.connect(function() {
		if (isRaisingGroup) return;

		var topWindow = null;
		var stack = workspace.stackingOrder;

		for (var i = stack.length - 1; i >= 0; i--) {
			var win = stack[i];
			if (win.normalWindow) {
				topWindow=win;
				break
			}
		}
		//console.log("mkFolderWidgetTiler: Pos1 topWindow.caption=" + topWindow.caption + " w.caption=" + w.caption);
		if (topWindow.internalId === w.internalId) {
			var managedGroup = managedWindowsPerDesktop[getDesktopKey(w)];
			if (managedGroup) {
				isRaisingGroup = true;
				for (var i = 0; i < managedGroup.length; i++) {
					var otherW = managedGroup[i];
					if (otherW && otherW.internalId !== w.internalId) {
						workspace.raiseWindow(otherW);
					}
				}
				workspace.raiseWindow(w);
				isRaisingGroup = false;
			}
		}
	});
});

// Window closed event. Remove from internal list and update remaining tiled windows's position.
workspace.windowRemoved.connect(function(w) {
	for (var dKey in managedWindowsPerDesktop) {
		var arr = managedWindowsPerDesktop[dKey];
		var index = arr.indexOf(w);

		if (index !== -1) {
			arr.splice(index, 1);
			layoutManagedWindows(dKey);
			return;
		}
	}
});

// Handle movement of windows between desktops
function handleDesktopChange(w) {
	// Remove from previous desktop's tiling list and update positions
	for (var oldKey in managedWindowsPerDesktop) {
		var arr = managedWindowsPerDesktop[oldKey];
		var index = arr.indexOf(w);
		if (index !== -1) {
			arr.splice(index, 1);
			layoutManagedWindows(oldKey);
		}
	}

	// Insert into new desktop's tiling list (if there is space)
	if (isWindowMatch(w)) {
		var newKey = getDesktopKey(w);
		if (!managedWindowsPerDesktop[newKey]) {
			managedWindowsPerDesktop[newKey] = [];
		}

		if (managedWindowsPerDesktop[newKey].length < MAX_WINDOWS) {
			managedWindowsPerDesktop[newKey].push(w);
			layoutManagedWindows(newKey);
		}
	}
}

// Helper: Remove window from being managed
function releaseWindow(w) {
	for (var dKey in managedWindowsPerDesktop) {
		var arr = managedWindowsPerDesktop[dKey];
		var index = arr.indexOf(w);

		if (index !== -1) {
			arr.splice(index, 1);
			layoutManagedWindows(dKey);
			return true;
		}
	}
	return false;
}

// Helper: Checks if window is already being handled by us
function isAlreadyManaged(w) {
	for (var dKey in managedWindowsPerDesktop) {
		if (managedWindowsPerDesktop[dKey].indexOf(w) !== -1) return true;
	}
	return false;
}

// Helper: Adds a window to our list based on its y-position.
function insertWindowByY(w, dKey) {
	var arr = managedWindowsPerDesktop[dKey];
	if (!arr) return;

	// If window is already being managed (when reordering via mouse drag), temporarily remove from list
	var currentIndex = arr.indexOf(w);
	if (currentIndex !== -1) {
		arr.splice(currentIndex, 1);
	}

	// Calculate vertical center of window
	var wCenterY = w.y + (w.height / 2);

	insertIndex = 0;
	if (w.y == 0) {
		// Exception: If the window is dragged to the top edge of the screen, assume it's supposed to go into the top slot
	} else {
		// Find best place to insert
		insertIndex = arr.length; // Default insertion point: at the bottom

		for (var i = 0; i < arr.length; i++) {
			var target = arr[i];
			var targetCenterY = target.y + (target.height / 2);

			// If center point of new window has smaller y-coordinate than reference window, insert before that one
			if (wCenterY < targetCenterY) {
				insertIndex = i;
				break;
			}
		}
	}

	// Insert at that postion
	arr.splice(insertIndex, 0, w);
}

// Helper: Retrieve desktop name as string
function getDesktopKey(w) {
	if (w.onAllDesktops) return "all_desktops";

	// On Plasma 6, "w.desktops" is an array of desktop-objects
	if (w.desktops && w.desktops.length > 0) {
		return w.desktops[0].id.toString();
	}
	return "unknown_desktop";
}


// Dieses Signal wird gefeuert, wenn du in der UI auf "Anwenden" klickst
options.configChanged.connect(function() {
	loadConfig();
});
