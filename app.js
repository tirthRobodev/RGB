// No imports needed, we are using the global 'firebase' object from the compat scripts loaded in HTML
// No auth required as per user provided rules up to 2026-10-22

const firebaseConfig = {
  apiKey: "AIzaSyAxOirNX5r_-dT6Ojj-W_kpWJoUFp8j38c",
  authDomain: "rgb-chamber.firebaseapp.com",
  databaseURL: "https://rgb-chamber-default-rtdb.firebaseio.com",
  projectId: "rgb-chamber",
  storageBucket: "rgb-chamber.firebasestorage.app",
  messagingSenderId: "358619265853",
  appId: "1:358619265853:web:2a6d8470a2cf5975cce354",
  measurementId: "G-9HGG81KTER"
};

const app = firebase.initializeApp(firebaseConfig);
const db = firebase.database(app);

// DOM Elements
const statusBadge = document.getElementById('connection-status');
const timerDisplay = document.getElementById('timer-display');
const stateLabel = document.getElementById('state-label');
const wlInput = document.getElementById('wavelength');
const wlValDisplay = document.getElementById('wavelength-val');
const minInput = document.getElementById('minutes');
const secInput = document.getElementById('seconds');
const trayInputs = document.querySelectorAll('input[name="tray"]');

const btnStart = document.getElementById('btn-start');
const btnPause = document.getElementById('btn-pause');
const btnStop = document.getElementById('btn-stop');
const configSection = document.getElementById('config-section');

let lastRequestId = 0;
let lastKnownState = "IDLE";

// Initialize Dashboard
function init() {
    // Listen to device status
    const statusRef = db.ref('rgb_chamber/status');
    statusRef.on('value', (snapshot) => {
        const data = snapshot.val();
        if (data) {
            updateDashboard(data);
        }
    });

    // Device connection watchdog
    setInterval(checkDeviceConnection, 2000);
    
    // UI Event Listeners
    wlInput.addEventListener('input', (e) => {
        wlValDisplay.textContent = e.target.value;
    });

    // Auto-save config changes when idle
    [wlInput, minInput, secInput, ...trayInputs].forEach(el => {
        el.addEventListener('change', () => {
            if (lastKnownState === 'IDLE' || lastKnownState === 'COMPLETED') {
                saveConfig();
            }
        });
    });

    btnStart.addEventListener('click', () => sendCommand('start'));
    btnPause.addEventListener('click', () => sendCommand('pause'));
    btnStop.addEventListener('click', () => sendCommand('stop'));
    
    // Fetch initial config
    db.ref('rgb_chamber/config').once('value').then((snapshot) => {
        if(snapshot.exists()) {
            const c = snapshot.val();
            wlInput.value = c.wavelength;
            wlValDisplay.textContent = c.wavelength;
            minInput.value = c.minutes;
            secInput.value = c.seconds;
            const targetTrayInput = document.querySelector(`input[name="tray"][value="${c.tray}"]`);
            if (targetTrayInput) targetTrayInput.checked = true;
        }
    });
}

// -----------------------------------------
// UI Updates
// -----------------------------------------
let lastUpdateTimestamp = 0;

function updateDashboard(data) {
    lastUpdateTimestamp = data.lastUpdated;
    lastKnownState = data.state;
    
    statusBadge.textContent = "Online";
    statusBadge.className = "status-badge online";

    // Update Timer display
    let totalSeconds = Math.floor(data.remaining / 1000);
    let m = Math.floor(totalSeconds / 60);
    let s = totalSeconds % 60;
    timerDisplay.textContent = `${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
    
    stateLabel.textContent = data.state;

    // Button states
    if (data.state === 'RUNNING') {
        btnStart.disabled = true;
        btnPause.disabled = false;
        btnStop.disabled = false;
        configSection.style.opacity = '0.5';
        configSection.style.pointerEvents = 'none';
    } else if (data.state === 'PAUSED') {
        btnStart.disabled = false;
        btnStart.textContent = "Resume";
        btnPause.disabled = true;
        btnStop.disabled = false;
        configSection.style.opacity = '0.5';
        configSection.style.pointerEvents = 'none';
    } else {
        btnStart.disabled = false;
        btnStart.textContent = "Start";
        btnPause.disabled = true;
        btnStop.disabled = true;
        configSection.style.opacity = '1';
        configSection.style.pointerEvents = 'auto';
    }
}

function checkDeviceConnection() {
    // If we haven't received a status update in 5 seconds, consider it offline
    if (Date.now() - lastUpdateTimestamp > 5000 && lastUpdateTimestamp !== 0) {
        statusBadge.textContent = "Offline";
        statusBadge.className = "status-badge offline";
    }
}

// -----------------------------------------
// Firebase Writes
// -----------------------------------------
function saveConfig() {
    const configData = {
        tray: document.querySelector('input[name="tray"]:checked').value,
        wavelength: parseInt(wlInput.value),
        minutes: parseInt(minInput.value),
        seconds: parseInt(secInput.value)
    };
    
    db.ref('rgb_chamber/config').set(configData);
}

function sendCommand(action) {
    // If starting from IDLE, ensure we save the latest config first
    if (action === 'start' && (lastKnownState === 'IDLE' || lastKnownState === 'COMPLETED')) {
        saveConfig();
    }
    
    // For 'start' when PAUSED, we send 'resume'
    let finalAction = action;
    if (action === 'start' && lastKnownState === 'PAUSED') {
        finalAction = 'resume';
    }
    
    lastRequestId++;
    
    db.ref('rgb_chamber/command').set({
        action: finalAction,
        requestId: lastRequestId
    });
}

// Start
init();
