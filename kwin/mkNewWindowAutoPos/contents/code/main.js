console.log("mknewwindowautopos: Initializing");

// Variable zum Speichern des "echten" vorherigen Fensters
var lastRealActiveWindow = null;

// Wir tracken JEDE Fokus-Änderung im System
workspace.windowActivated.connect(function(window) {
	// Nur speichern, wenn das aktivierte Fenster NICHT mkFileSearch ist
	// So bleibt das "alte" Fenster in der Variable, während mkFileSearch startet
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

	//console.log("mknewwindowautopos: Active window '" + window.resourceClass + "' at " + actX + "/" + actY + " " + actW + "x" + actH);

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
				//console.log("mknewwindowautopos: Fits left");
			} else {				// else put right
				renX = actX + actW;
				//console.log("mknewwindowautopos: Fits right");
			}

			// Vertical positioning: Preferably like active window, but make sure we stay inside workArea
			if (actY + winH > workArea.height) {
				renY = workArea.height - winH;
				//console.log("mknewwindowautopos: Moving higher to stay inside workArea");
			}
		}
		else if (missingT <= 0 || missingB <= 0) {	// else if fits above or below
			if (missingT <= 0) {	// if fits above
				renY = actY - winH;
				//console.log("mknewwindowautopos: Fits above");
			} else {				// else put below
				renY = actY + actH;
				//console.log("mknewwindowautopos: Fits below");
			}

			// Horizontal positioning: Preferably like active window, but make sure we stay inside workArea
			if (actX + winW > workArea.width) {
				renX = workArea.width - winW;
				//console.log("mknewwindowautopos: Moving left to stay inside workArea");
			}
		}
	}
	else {
		// Can't fit without overlapping, so minimize overlapping
		if (missingL <= missingR) {
			renX = 0;
			//console.log("mknewwindowautopos: Can't fit / prefer left");
		} else {
			renX = workArea.width - winW;
			//console.log("mknewwindowautopos: Can't fit / prefer right");
		}

		if (missingT <= missingB) {
			renY = 0;
			//console.log("mknewwindowautopos: Can't fit / prefer top");
		} else {
			renY = workArea.height - winH;
			//console.log("mknewwindowautopos: Can't fit / prefer bottom");
		}
	}

	if (renX != actX || renY != actY) {
		//console.log("mknewwindowautopos: move to " + renX + "/" + renY + " " + winW + "x" + winH);
		window.frameGeometry = {
			x: renX,
			y: renY,
			width: winW,
			height: winH
		};
	}
});
