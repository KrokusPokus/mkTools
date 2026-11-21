console.log("mknewwindowautopos: Initializing");

// Variable to store the "real" previous window
var lastRealActiveWindow = null;

// We track EVERY focus change in the system
workspace.windowActivated.connect(function(window) {
	// Only store it if the activated window is NOT mkFileSearch
	// That way the "old" window stays in the variable while mkFileSearch starts
	if (window && window.resourceClass !== "mkFileSearch") {
		lastRealActiveWindow = window;
	}
});

workspace.windowAdded.connect(function(window) {
	if (window.resourceClass !== "mkFileSearch" || !window.normalWindow) return;

	var active = lastRealActiveWindow;

	if (!active || active.minimized || active.maximizedMode === 3) {
		return;
	}

	var workArea = workspace.clientArea(0, window); // 0 = PlacementArea

	var winW = window.width;
	var winH = window.height;
	var actX = active.x;
	var actY = active.y;
	var actW = active.width;
	var actH = active.height;

	var renX = actX;
	var renY = actY;

	var missingL = winW - actX;
	var missingR = (actX + actW + winW) - workArea.width;
	var missingT = winH - actY;
	var missingB = (actY + actH + winH) - workArea.height;

	if (missingL <= 0 || missingR <= 0 || missingT <= 0 || missingB <= 0) {
		if (missingL <= 0 || missingR <= 0) {	// if fits left or right
			if (missingL <= 0) {	// if fits left
				renX = actX - winW;
			} else {				// else put right
				renX = actX + actW;
			}

			// Vertical positioning: Preferably like active window, but make sure we stay inside workArea
			if (actY + winH > workArea.height) {
				renY = workArea.height - winH;
			}
		}
		else if (missingT <= 0 || missingB <= 0) {	// else if fits above or below
			if (missingT <= 0) {	// if fits above
				renY = actY - winH;
			} else {				// else put below
				renY = actY + actH;
			}

			// Horizontal positioning: Preferably like active window, but make sure we stay inside workArea
			if (actX + winW > workArea.width) {
				renX = workArea.width - winW;
			}
		}
	}
	else {
		// Can't fit without overlapping, so minimize overlapping
		if (missingL <= missingR) {
			renX = 0;
		} else {
			renX = workArea.width - winW;
		}

		if (missingT <= missingB) {
			renY = 0;
		} else {
			renY = workArea.height - winH;
		}
	}

	if (renX != actX || renY != actY) {
		window.frameGeometry = {
			x: renX,
			y: renY,
			width: winW,
			height: winH
		};
	}
});
