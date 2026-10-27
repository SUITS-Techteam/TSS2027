// GLOBAL VARIABLES
let connectionFails = 0; // number of consecutive connection failures, resets on successful fetch
const pendingSwitchChanges = {};
const GROUP_NAMES = ["A", "B"];
const NUMBER_OF_TEAMS = 10;
let fetchDataWaiting = false;
let fetchGroupsWaiting = false;

/**
 * Called when the index.html page is loaded, sets up the periodic data fetching
 */
function onload() {
  updateClock(); // set clock immediately on load (will be updated every second later)
  setupEventListeners();  // Set up event listeners for switches and buttons
  pollGroups();

  // Fetch fresh data from the backend every one second
  setInterval(() => {
    pollData();
    updateClock();
    updateTelemetryStatus();
  }, 1000);

  setInterval(() => {
  	pollGroups();
  }, 3000);

}

// DATA MANAGEMENT

// fixes an issue with fetchData calls slowly taking longer as the frontend is open
async function pollData() {
	if (fetchDataWaiting) return;
	fetchDataWaiting = true;
	try {
		await fetchData();
	}
	finally {
		fetchDataWaiting = false;
	}
}

async function pollGroups() {
	if (fetchGroupsWaiting) return;
	fetchGroupsWaiting = true;
	try {
		await fetchGroups();
	}
	finally {
		fetchGroupsWaiting = false;
	}
}


/**
 * Fetches the latest EVA  data and updates the DOM elements accordingly.
 * In the html, there is a data-path attribute on the elements that basically registers that field as needing to be updated with data from the server
 * The path specifies where in the JSON data the value can be found, e.g. "eva.telemetry.temperature"
 *
 * Note: this is a bit of a hodgepodge to maintain backwards compatibility with the old system e.g. timers, boolean switches, etc
 */
async function fetchData() {
  let evaData;

  try {
    // Create abort controllers with 1 second timeout
    const evaController = new AbortController();

    // Fetch EVA data
    const teamSelect = document.getElementById('team');
    const teamIndex = teamSelect.value;

    const [evaResponse] = await Promise.all([
      fetch(`/data/instances/${teamIndex}/EVA.json`, { signal: evaController.signal })
    ]);

    [evaData] = await Promise.all([
      evaResponse.json()
    ]);

    //check if telemetry component has been started by seeing if EVA.status.started is true
    evaStarted = evaData?.status?.started === true;

    connectionFails = 0;
  } catch (error) {
    console.error("Fatal error fetching data:", error);
    connectionFails++;
    return;
  }

  // Update the EVA fields in the DOM
  const elements = document.querySelectorAll("[data-path]");
  elements.forEach((el) => {
    const path = el.getAttribute("data-path");

    let value;

    if (path.startsWith("eva.")) {
      value = getNestedValue(evaData, path.slice(4));
    }

	if (path === "eva.ssu.status"){
		const status = getNestedValue(evaData, "ssu.status");
		const status_el = document.getElementById("ssu-status");

		if (status === "off") {
			status_el.textContent = "OFF";
			status_el.className = "off";
		}
		else if (status === "booting") {
			status_el.textContent = "BOOTING";
			status_el.className = "booting";
		}
		else {
			status_el.textContent = "READY";
			status_el.className = "ready";
		}
	}
	if (path === "eva.ssu.sp_sensor") {
		const sp = getNestedValue(evaData, "ssu.sp_sensor");
		const sp_el = document.getElementById("ssu-sp-sensor");

		if(sp === "not ready"){
			sp_el.textContent = "NOT READY";
			sp_el.className = "off"
			return;
		}
		if (sp === "deployed"){
			sp_el.textContent = "DEPLOYED";
			sp_el.className = "deployed";
		}
		else {
			sp_el.textContent = "PRIMED";
			sp_el.className = "ready";
		}
		return;
	}
	if (path === "eva.ssu.bb_sensor") {
		const bb = getNestedValue(evaData, "ssu.bb_sensor");
		const bb_el = document.getElementById("ssu-bb-sensor");

		if(bb === "not ready"){
			bb_el.textContent = "NOT READY";
			bb_el.className = "off"
			return;
		}
		if (bb === "deployed"){
			bb_el.textContent = "DEPLOYED";
			bb_el.className = "deployed";
		}
		else {
			bb_el.textContent = "PRIMED";
			bb_el.className = "ready";
		}
		return;
	}
	if (path === "eva.ssu.sp_state" || path === "eva.ssu.bb_state"){
		const sp_state = getNestedValue(evaData, "ssu.sp_state");
		const bb_state = getNestedValue(evaData, "ssu.bb_state");
		const sp_el = document.getElementById("ssu-sp-state");
		const bb_el = document.getElementById("ssu-bb-state");
		const mode = getNestedValue(evaData, "ssu.mode");

		if(mode === 0) {
			bb_el.style.display = "none";
			sp_el.style.display = "inline";
			switch (sp_state){
				case "idle":
					sp_el.textContent = "IDLE";
					sp_el.className = "off";
					break;
				case "drilling":
					sp_el.textContent = "DRILLING";
					sp_el.className = "ready";
					break;
				case "overheated":
					sp_el.textContent = "OVERHEATED";
					sp_el.className = "off";
					break;
				case "retracting":
					sp_el.textContent = "RETRACTING";
					sp_el.className = "deployed";
					break;
				case "retracted":
					sp_el.textContent = "RETRACTED";
					sp_el.className = "ready";
					break;
				case "deployed":
					sp_el.textContent = "DEPLOYED";
					sp_el.className = "deployed";
					break;
				default:
			}
		}
		if(mode === 1) {
			bb_el.style.display = "inline";
			sp_el.style.display = "none";
			switch (bb_state){
				case "idle":
					bb_el.textContent = "IDLE";
					bb_el.className = "off";
					break;
				case "drilling":
					bb_el.textContent = "DRILLING";
					bb_el.className = "ready";
					break;
				case "overheated":
					bb_el.textContent = "OVERHEATED";
					bb_el.className = "off";
					break;
				case "retracting":
					bb_el.textContent = "RETRACTING";
					bb_el.className = "deployed";
					break;
				case "retracted":
					bb_el.textContent = "RETRACTED";
					bb_el.className = "ready";
					break;
				case "deployed":
					bb_el.textContent = "DEPLOYED";
					bb_el.className = "deployed";
					break;
				default:
			}
		}
		return;
	}
	if (path === "eva.spm.status") {
		const spm_status = getNestedValue(evaData, "spm.status");
		const spm_el = document.getElementById("spm-status");

		switch (spm_status) {
			case "off":
				spm_el.textContent = "OFF";
				spm_el.className = "off";
				break;
			case "booting":
				spm_el.textContent = "BOOTING";
				spm_el.className = "booting";
				break;
			case "leveling":
				spm_el.textContent = "LEVELING";
				spm_el.className = "ready";
				break;
			case "deploying":
				spm_el.textContent = "DEPLOYING";
				spm_el.className = "ready";
				break;
			case "orienting":
				spm_el.textContent = "ORIENTING";
				spm_el.className = "ready";
				break;
			case "complete":
				spm_el.textContent = "COMPLETE";
				spm_el.className = "ready";
				break;
			default:
		}
		return;
	}
	if (path === "eva.spm.level") {
		const spm_level = getNestedValue(evaData, "spm.level");
		const spm_el = document.getElementById("spm-level");

		switch (spm_level) {
			case true:
				spm_el.textContent = "YES";
				spm_el.className = "ready";
				break;
			case false:
				spm_el.textContent = "NO";
				spm_el.className = "off";
				break;
			default:
		}
		return;
	}
	if (path === "eva.spm.truss") {
		const spm_truss = getNestedValue(evaData, "spm.truss");
		const spm_el = document.getElementById("spm-truss");

		switch (spm_truss) {
			case "stowed":
				spm_el.textContent = "STOWED";
				spm_el.className = "off";
				break;
			case "unlocked":
				spm_el.textContent = "UNLOCKED";
				spm_el.className = "ready";
				break;
			case "raising":
				spm_el.textContent = "RAISING";
				spm_el.className = "deployed";
				break;
			case "deployed":
				spm_el.textContent = "DEPLOYED";
				spm_el.className = "deployed";
				break;
			case "locked":
				spm_el.textContent = "LOCKED";
				spm_el.className = "ready";
				break;
			default:
		}
		return;
	}
	if (path === "eva.spm.azimuth_aligned") {
		const spm_aligned = getNestedValue(evaData, "spm.azimuth_aligned");
		const spm_el = document.getElementById("spm-oriented");

		switch (spm_aligned) {
			case true:
				spm_el.textContent = "YES";
				spm_el.className = "ready";
				break;
			case false:
				spm_el.textContent = "NO";
				spm_el.className = "off";
				break;
			default:
		}
		return;
	}

    // Handle checkboxes/switches (set checked property for boolean values)
    if (el.type === "checkbox") {
	  if (!isSwitchChangePending(path, value)) {
	  	el.checked = Boolean(value);
	  }
      return; // don't set textContent for checkboxes
    }

    // Handle action buttons (START/RESET/DEPLOY/RETRACT)
    if (el.tagName === "BUTTON" && el.hasAttribute("data-action")) {
      const action = el.getAttribute("data-action");
      const is_running = Boolean(value);

	  const sp = getNestedValue(evaData, "ssu.sp_sensor");
	  const sp_state = getNestedValue(evaData, "ssu.sp_state");
	  const sp_depth = getNestedValue(evaData, "ssu.sp_depth");

	  const bb = getNestedValue(evaData, "ssu.bb_sensor");
	  const bb_state = getNestedValue(evaData, "ssu.bb_state");
	  const bb_depth = getNestedValue(evaData, "ssu.bb_depth");

	  const ssu_mode = getNestedValue(evaData, "ssu.mode");

	  const level = getNestedValue(evaData, "spm.level");
	  const spm_status = getNestedValue(evaData, "spm.status");
	  const truss = getNestedValue(evaData, "spm.truss");
	  const locked = getNestedValue(evaData, "spm.orientation_locked");
	  const aligned = getNestedValue(evaData, "spm.azimuth_aligned");
	  const spm_mode = getNestedValue(evaData, "spm.mode");

      if (action === "start") {
        // START button: enabled when NOT running
        el.disabled = is_running;
        el.style.opacity = is_running ? "0.5" : "1";
      }
      else if (action === "reset") {
        // RESET button: enabled when IS running
        el.disabled = !is_running;
        el.style.opacity = !is_running ? "0.5" : "1";
      }
      else if (action === "deploy") {
		// DEPLOY button - enabled when NOT deployed and state is retracted and depth is 0.0 for respective mode
			if(ssu_mode === 0) {
				if (sp_state === "retracted" && sp_depth === 0 && sp === "primed"){
					el.disabled = false;
					el.style.opacity = "1";
			    }
			    else {
					el.disabled = true;
					el.style.opacity = "0.5";
			    }
			}
			else if(ssu_mode === 1) {
				if (bb_state === "retracted" && bb_depth === 0 && bb === "primed"){
					el.disabled = false;
					el.style.opacity = "1";
			    }
			    else {
					el.disabled = true;
					el.style.opacity = "0.5";
			    }
			}
      }
      else if (action === "retract") {
		// RETRACT button - enabled when depth is at target (50) and state is not overheated or sensor already deployed
			if(ssu_mode === 0) {
				if (sp_state !== "drilling" && sp_state !== "deployed" && sp_state !== "overheated" && sp_depth >= 50){
					el.disabled = false;
					el.style.opacity = "1";
			    }
			    else {
					el.disabled = true;
					el.style.opacity = "0.5";
			    }
			}
			else if(ssu_mode === 1) {
				if (bb_state !== "drilling" && bb_state !== "deployed" && bb_state !== "overheated" && bb_depth >= 100){
					el.disabled = false;
					el.style.opacity = "1";
			    }
			    else {
					el.disabled = true;
					el.style.opacity = "0.5";
			    }
			}
      }
      else if (action === "unlock") {
		// UNLOCK button - enabled when platform is level and truss needs to be unlocked or orientation needs to be unlocked
			if(spm_mode === 0) {
				if(spm_status === "deploying" && level && truss === "stowed") {
					el.disabled = false;
					el.style.opacity = "1";
				}
				else {
					el.disabled = true;
					el.style.opacity = "0.5";
				}
			}
			else if(spm_mode === 1) {
				if(locked && !aligned) {
					el.disabled = false;
					el.style.opacity = "1";
				}
				else {
					el.disabled = true;
					el.style.opacity = "0.5";
				}
			}
      }
      else if (action === "raise") {
		// RAISE button - enabled when spm is in deploying status and truss is unlocked
			if(spm_mode === 0) {
				if(spm_status === "deploying" && level && truss === "unlocked") {
					el.disabled = false;
					el.style.opacity = "1";
				}
				else {
					el.disabled = true;
					el.style.opacity = "0.5";
				}
			}
			else {
				el.disabled = true;
				el.style.opacity = "0.5";
			}
	  }
	  else if (action === "lock") {
		// LOCK button - enabled when truss is raised or during orientation when not locked
			if(spm_mode === 0) {
				if(spm_status === "leveling" || truss === "deployed") {
					el.disabled = false;
					el.style.opacity = "1";
				}
				else {
					el.disabled = true;
					el.style.opacity = "0.5";
				}
			}
			else if (spm_mode === 1) {
				if(!locked && !aligned) {
					el.disabled = false;
					el.style.opacity = "1";
				}
				else {
					el.disabled = true;
					el.style.opacity = "0.5";
				}
			}
	  }
	  else if (action === "verify") {
		// RAISE button - enabled during orientation
			if(spm_mode === 1) {
				if(spm_status === "orienting" && locked && !aligned) {
					el.disabled = false;
					el.style.opacity = "1";
				}
				else {
					el.disabled = true;
					el.style.opacity = "0.5";
				}
			}
			else {
				el.disabled = true;
				el.style.opacity = "0.5";
			}
	  }
      return; // don't set textContent for action buttons
    }

	// change slider input for drills
	if((el.id != "ssu-sp_rpm" && el.id != "ssu-bb_rpm") && (path === "eva.ssu.sp_rpm" || path === "eva.ssu.bb_rpm")){
		const mode = getNestedValue(evaData, "ssu.mode");
		const spSlider = document.getElementById("mock-sp-rpm-slider");
		const spLabel = document.getElementById("mock-sp-rpm-label");
		const bbSlider = document.getElementById("mock-bb-rpm-slider");
		const bbLabel = document.getElementById("mock-bb-rpm-label");
		if(mode === 0 || mode === -1) {
			bbSlider.style.display = "none";
			bbLabel.style.display = "none";
			spSlider.style.display = "inline";
			spLabel.style.display = "inline";

		}
		else {
			bbSlider.style.display = "inline";
			bbLabel.style.display = "inline";
			spSlider.style.display = "none";
			spLabel.style.display = "none";
		}
		return;
	}


    // Handle time formatting if data-format="time" is specified
    const format = el.getAttribute("data-format");
    if (format === "time" && typeof value === "number") {
      value = formatTime(value);
    } else if (format === "status" && typeof value === "boolean") {
      value = value ? "Complete" : "Incomplete";
    } else if (typeof value === "number") {
      // Handle other number formatting for text elements
      value = value.toFixed(2);
    }

    // Append units if specified
    const units = el.getAttribute("data-units");
    if (units) {
      value = `${value} ${units}`;
    }

    el.textContent = value;
  });
}

/**
 * Updates the server with the new value for a specific field using data-path format
 *
 * @param path Data path (e.g., "eva.dcu.batt")
 * @param value New value for the field
 */
async function updateServerData(path, value) {
  try {
    const teamSelect = document.getElementById('team');

    const params = new URLSearchParams();
    params.append("team", teamSelect.value);
    params.append(path, value);
    const response = await fetch(`/`, {
      method: "POST",
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
      },
      body: params,
    });
  } catch (error) {
    console.error("Fatal error updating server data:", error);
  }
}

// EVENT LISTENERS

async function setupEventListeners() {

  const switches = document.querySelectorAll(
    'input[type="checkbox"][data-path]'
  );

  for (const switchEl of switches) {
    const path = switchEl.getAttribute("data-path");

    // Listen for changes
    switchEl.addEventListener("change", (event) => {
      const value = event.target.checked;
	  rememberSwitchChange(path, value);
	  if(path === "eva.ssu.mode"){
		if(value === false) {
			updateServerData(path, 0);
		}
		else {
			updateServerData(path, 1);
		}
		return;
	  }

      updateServerData(path, value);
    });
  }
  // When any of the buttons with data-path are clicked, send the value to the server
  const buttons = document.querySelectorAll("button[data-path]");

  buttons.forEach((button) => {
    button.addEventListener("click", (event) => {
      const path = event.target.getAttribute("data-path");
      const action = event.target.getAttribute("data-action");

      // For action buttons, directly update the status field
      if (action === "start") {
        // Start: set the field to true
        updateServerData(path, true);
      } else if (action === "reset") {
        // Reset: set the field to false
        updateServerData(path, false);
      } else if (action === "deploy" || action === "retract" || action == "unlock"
      	|| action === "raise" || action === "lock" || action === "verify") {
		updateServerData(path, true);
      }
      	else {
        // Fallback to old data-value system for backward compatibility
        const value = event.target.getAttribute("data-value") === "true";
        updateServerData(path, value);
      }
    });
  });

  const sliders = document.querySelectorAll('input[type="range"][data-path]');
  sliders.forEach((slider) => {
    const path = slider.getAttribute("data-path");

    slider.addEventListener("input", (event) => {
      const value = event.target.value;
      updateServerData(path, value);

      const valueEl = document.getElementById(slider.id + "-value");
      if (valueEl) {
        valueEl.textContent = value;
      }
    });
  });

}

// HELPER FUNCTIONS


/**
 * Records a switch the user just flipped so polls don't undo it before the server catches up
 *
 * @param path Data path of the switch
 * @param checked New checked state
 */
function rememberSwitchChange(path, checked) {
  pendingSwitchChanges[path] = { checked: checked, expiresAt: Date.now() + 4000 };
}

/**
 * True while the server still reports the old value for a switch the user just flipped
 *
 * @param path Data path of the switch
 * @param serverValue Value from the latest EVA.json
 */
function isSwitchChangePending(path, serverValue) {
  const pending = pendingSwitchChanges[path];
  if (!pending) return false;

  if (Boolean(serverValue) === pending.checked || Date.now() > pending.expiresAt) {
    delete pendingSwitchChanges[path];
    return false;
  }
  return true;
}

/**
 * Retrieves a nested value from an object/json using a dot-separated path
 *
 * @param obj Object to retrieve the value from
 * @param path Dot-separated path to the desired value
 * @returns The value at the specified path or undefined if not found
 */
function getNestedValue(obj, path) {
  if (!path) return undefined;

  return path.split(".").reduce((current, key) => {
    return current && current[key] !== undefined ? current[key] : undefined;
  }, obj);
}

/**
 * Formats seconds into HH:MM:SS time format
 * @param {number} seconds - Total seconds to format
 * @returns {string} Formatted time string (HH:MM:SS)
 */
function formatTime(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  return `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}:${String(
    s
  ).padStart(2, "0")}`;
}

/**
 * Sets a cookie in the browser
 *
 * @param cname Cookie name
 * @param cvalue Cookie value
 */
function setCookie(cname, cvalue) {
  document.cookie = `${cname}=${cvalue};path=/`;
}

/**
 * Retrieves a cookie from the browser
 *
 * @param cname Cookie name
 * @returns Cookie value or null if not found
 */
function getCookie(cname) {
  const name = `${cname}=`;
  const decodedCookie = decodeURIComponent(document.cookie);
  const ca = decodedCookie.split(";");

  for (let i = 0; i < ca.length; i++) {
    let c = ca[i];
    while (c.charAt(0) === " ") {
      c = c.substring(1);
    }
    if (c.indexOf(name) === 0) {
      return c.substring(name.length, c.length);
    }
  }

  return null;
}

/**
 * Updates the clock in the navigation bar based on military time format
 */
function updateClock() {
  const now = new Date();
  const hours = String(now.getHours()).padStart(2, "0");
  const minutes = String(now.getMinutes()).padStart(2, "0");
  const seconds = String(now.getSeconds()).padStart(2, "0");
  const timeString = `${hours}:${minutes}:${seconds}`;

  const clockElement = document.getElementById("nav-clock");
  if (clockElement) {
    clockElement.textContent = timeString;
  }
}

/**
 * Updates the telemetry status indicator in the navigation bar
 */
function updateTelemetryStatus() {
  const statusElement = document.getElementById("telemetry-status");
  if (statusElement) {
    const isConnected = connectionFails <= 2;
    const statusText = isConnected
      ? "Telemetry Connected"
      : "Telemetry Disconnected";
    statusElement.innerHTML = `● ${statusText}`;
    statusElement.style.color = isConnected ? "#28ae5f" : "#d82121ff";
  }
}


// PERIPHERAL GROUPS

/**
 * Fetches data/groups.json (written by groups.c) and redraws the Peripheral Groups card
 */
async function fetchGroups() {
  try {
    const response = await fetch("/data/groups.json", { cache: "no-store" });
    const groupsData = await response.json();
    renderGroups(groupsData);
  } catch (error) {
    console.error("Error fetching peripheral groups:", error);
  }
}


/**
 * Sends a group action to the server, e.g. { group: 0, team: 3 } or { group: 0, end: 1 }
 *
 * @param fields Form fields to send
 * @param rejectedMessage Shown if the server refuses the action
 */
async function sendGroupAction(fields, rejectedMessage) {
  try {
    const response = await fetch("/groups", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams(fields),
    });
    if (!response.ok && rejectedMessage) {
      alert(rejectedMessage);
    }
  } catch (error) {
    console.error("Error sending group action:", error);
  }
  fetchGroups();
}

/**
 * Builds one <select> from [value, label, disabled] options
 */
function buildSelect(options, selectedValue, disabled, onChange) {
  const select = document.createElement("select");
  for (const [value, label, optionDisabled] of options) {
    const option = document.createElement("option");
    option.value = value;
    option.textContent = label;
    option.disabled = Boolean(optionDisabled);
    option.selected = String(value) === String(selectedValue);
    select.appendChild(option);
  }
  select.disabled = disabled;
  select.addEventListener("change", (event) => {
    event.target.blur(); // let the next refresh redraw the card
    onChange(event.target.value);
  });
  return select;
}

function buildButton(label, disabled, onClick) {
  const button = document.createElement("button");
  button.className = "btn-group-action";
  button.textContent = label;
  button.disabled = disabled;
  button.addEventListener("click", onClick);
  return button;
}

function buildText(text, className) {
  const span = document.createElement("span");
  span.textContent = text;
  if (className) span.className = className;
  return span;
}

/**
 * Redraws the Peripheral Groups card from groups.json
 *
 * @param groupsData Parsed groups.json
 */
function renderGroups(groupsData) {
  const groupRows = document.getElementById("group-rows");
  const deviceRows = document.getElementById("device-rows");
  if (!groupRows || !deviceRows) return;

  // Don't redraw while someone has a dropdown open
  const focused = document.activeElement;
  if (focused && focused.tagName === "SELECT" &&
      (groupRows.contains(focused) || deviceRows.contains(focused))) {
    return;
  }

  const groups = groupsData.groups || [];
  const devices = groupsData.devices || [];

  // One row per group: team picker + End run
  groupRows.replaceChildren();
  groups.forEach((group, groupIndex) => {
    const groupName = GROUP_NAMES[groupIndex];
    const running = group.assigned_team !== -1;
    const teamsInOtherGroups = groups
      .filter((other, otherIndex) => otherIndex !== groupIndex)
      .map((other) => other.assigned_team);

    const teamOptions = [[-1, "No team", false]];
    for (let team = 0; team < NUMBER_OF_TEAMS; team++) {
      teamOptions.push([team, `Team ${team}`, teamsInOtherGroups.includes(team)]);
    }

    const groupDevices = devices.filter((device) => device.group === groupIndex);
    const row = document.createElement("div");
    row.className = "group-row";
    row.appendChild(buildText(`Group ${groupName}`, "group-name"));
	row.appendChild(buildSelect(teamOptions, group.assigned_team, false, (team) => {
	  if (Number(team) === -1) {
	    if (running && confirm(`End Group ${groupName}'s run for team ${group.assigned_team}?`)) {
	      sendGroupAction({ group: groupIndex, end: 1 });
	    } else {
	      fetchGroups();   // put the dropdown back
	    }
	    return;
	  }
	  if (running && !confirm(`Switch Group ${groupName} from team ${group.assigned_team} to team ${team}?`)) {
	    fetchGroups();   // put the dropdown back
	    return;
	  }
	  sendGroupAction({ group: groupIndex, team: team },
	    `Group ${groupName} couldn't take team ${team}. Check the team isn't in the other group.`);
	}));
    groupRows.appendChild(row);
  });

  // One row per device: status + group picker
  deviceRows.replaceChildren();
  if (devices.length === 0) {
    deviceRows.appendChild(buildText("No peripherals have sent anything to port 14141 yet", "device-quiet"));
  }
  devices.forEach((device, deviceIndex) => {
    let statusText;
    let statusClass;
    if (device.online) {
      statusText = "online";
      statusClass = "device-ready";
    } else if (device.last_seen) {
      statusText = `last seen ${device.last_seen}`;
      statusClass = "device-quiet";
    } else {
      statusText = "not seen since restart";
      statusClass = "device-quiet";
    }

    const groupOptions = [[-1, "No group"], [0, "Group A"], [1, "Group B"]];

    const row = document.createElement("div");
    row.className = "group-row";
    row.appendChild(buildText(device.type, "device-type"));
    row.appendChild(buildText(device.address, "device-address"));
    row.appendChild(buildText(statusText, statusClass));
    row.appendChild(buildSelect(groupOptions, device.group, false, (group) => {
      sendGroupAction({ device: deviceIndex, group: group });
    }));
    row.appendChild(buildButton("Forget", false, () => {
      if (confirm(`Remove ${device.type} at ${device.address} from the list? It will reappear if it sends again.`)) {
        sendGroupAction({ device: deviceIndex, forget: 1 });
      }
    }));
    deviceRows.appendChild(row);
  });
}
