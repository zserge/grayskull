const video = document.getElementById('video');
const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');
const startButton = document.getElementById('start');
const stopButton = document.getElementById('stop');
const cameraSelect = document.getElementById('cameraSelect');
const addStepButton = document.getElementById('add-step');
const pipelineStepsContainer = document.getElementById('pipeline-steps');

let wasm, memory;
let videoStream;
let animationFrameId;
let imageBuffers = [null, null, null]; // To hold pointers to WASM memory

const operations = {
    "blur": { params: [{ name: "radius", type: "range", default: 3, min: 1, max: 20 }] },
    "thresh": { params: [{ name: "threshold", type: "range", default: 128, min: 0, max: 255 }] },
    "adaptive": { params: [{ name: "block_size", type: "range", default: 15, min: 3, max: 51, step: 2 }] },
    "erode": { params: [] },
    "dilate": { params: [] },
    "sobel": { params: [] },
    "otsu": { params: [] },
};

// --- Grayscale Conversion in JavaScript ---
function rgbaToGray(rgba, gray) {
    for (let i = 0; i < gray.length; i++) {
        const r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
        gray[i] = 0.299 * r + 0.587 * g + 0.114 * b;
    }
}

function grayToRgba(gray, rgba) {
    for (let i = 0; i < gray.length; i++) {
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = gray[i];
        rgba[i * 4 + 3] = 255; // Alpha
    }
}

async function init() {
    try {
        const importObject = { env: {} };
        const { instance } = await WebAssembly.instantiateStreaming(fetch('grayskull.wasm'), importObject);
        wasm = instance.exports;
        memory = wasm.memory;
        console.log("WebAssembly module loaded.");
    } catch (e) {
        console.warn("WASM initialization failed.", e);
        alert("Failed to load grayskull.wasm. Make sure the file is present and the server is running correctly.");
        return;
    }
    await populateCameraList();
    addStepButton.onclick = () => addPipelineStep();
    addPipelineStep('blur'); // Add a default first step
    startButton.disabled = false;
}

async function populateCameraList() {
    try {
        if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) {
            throw new Error("Media device enumeration not supported.");
        }
        const devices = await navigator.mediaDevices.enumerateDevices();
        const videoDevices = devices.filter(d => d.kind === 'videoinput');
        cameraSelect.innerHTML = '';
        videoDevices.forEach((device, i) => {
            const option = document.createElement('option');
            option.value = device.deviceId;
            option.textContent = device.label || `Camera ${i + 1}`;
            cameraSelect.appendChild(option);
        });
    } catch (error) {
        console.error("Could not enumerate video devices:", error);
        alert("Could not access cameras. Please ensure you've granted permission.");
    }
}

startButton.onclick = async () => {
    if (videoStream) stopCamera();
    try {
        const constraints = { video: { deviceId: { exact: cameraSelect.value }, width: 320, height: 240 } };
        videoStream = await navigator.mediaDevices.getUserMedia(constraints);
        video.srcObject = videoStream;
        await video.play();

        canvas.width = video.videoWidth;
        canvas.height = video.videoHeight;

        if (wasm) {
            wasm.gs_reset_allocator();
            for (let i = 0; i < imageBuffers.length; i++) {
                wasm.gs_init_image(i, canvas.width, canvas.height);
                imageBuffers[i] = wasm.gs_get_image_data(i);
            }
        }

        animationFrameId = requestAnimationFrame(processFrame);
        startButton.disabled = true;
        stopButton.disabled = false;
    } catch (error) {
        console.error("Failed to start camera:", error);
        alert(`Could not start camera. Your camera might not support the requested resolution (320x240). Error: ${error.message}`);
    }
};

function stopCamera() {
    if (animationFrameId) cancelAnimationFrame(animationFrameId);
    if (videoStream) videoStream.getTracks().forEach(track => track.stop());
    video.srcObject = null;
    animationFrameId = null;
    startButton.disabled = false;
    stopButton.disabled = true;
}

stopButton.onclick = stopCamera;

function addPipelineStep(opName = 'blur') {
    const stepDiv = document.createElement('div');
    stepDiv.className = 'pipeline-step';

    const select = document.createElement('select');
    for (const key in operations) {
        const option = document.createElement('option');
        option.value = key;
        option.textContent = key;
        if (key === opName) option.selected = true;
        select.appendChild(option);
    }
    stepDiv.appendChild(select);

    const controlsContainer = document.createElement('span');
    stepDiv.appendChild(controlsContainer);

    const removeBtn = document.createElement('button');
    removeBtn.textContent = 'Remove';
    removeBtn.onclick = () => stepDiv.remove();
    stepDiv.appendChild(removeBtn);

    select.onchange = () => updateStepControls(controlsContainer, select.value);
    updateStepControls(controlsContainer, opName);

    pipelineStepsContainer.appendChild(stepDiv);
}

function updateStepControls(container, opName) {
    container.innerHTML = '';
    const op = operations[opName];
    if (!op || !op.params) return;

    op.params.forEach(param => {
        const input = document.createElement('input');
        input.type = param.type;
        input.name = param.name;
        if (param.type === 'range') {
            input.min = param.min || 0;
            input.max = param.max || 255;
            input.step = param.step || 1;
            input.value = param.default || 0;
            container.appendChild(input);

            const valueSpan = document.createElement('span');
            valueSpan.textContent = `(${input.value})`;
            input.oninput = () => valueSpan.textContent = `(${input.value})`;
            container.appendChild(valueSpan);
        }
    });
}

function processFrame() {
    if (!videoStream || video.paused || video.ended || !wasm) {
        if (animationFrameId) requestAnimationFrame(processFrame);
        return;
    }

    const width = canvas.width;
    const height = canvas.height;
    const imageSize = width * height;

    // 1. Get video frame and convert to grayscale, placing it in buffer 0
    ctx.drawImage(video, 0, 0, width, height);
    const imageData = ctx.getImageData(0, 0, width, height);
    const gray = new Uint8Array(imageSize);
    rgbaToGray(imageData.data, gray);
    new Uint8Array(memory.buffer, imageBuffers[0], imageSize).set(gray);

    // 2. Execute pipeline
    let readIdx = 0;
    let writeIdx = 1;

    pipelineStepsContainer.querySelectorAll('.pipeline-step').forEach(stepDiv => {
        const op = stepDiv.querySelector('select').value;
        const inputs = stepDiv.querySelectorAll('input');
        const params = Array.from(inputs).map(i => parseInt(i.value, 10));

        switch (op) {
            case 'blur': wasm.gs_blur_image(writeIdx, readIdx, params[0]); break;
            case 'thresh':
                wasm.gs_copy_image(writeIdx, readIdx);
                wasm.gs_threshold_image(writeIdx, params[0]);
                break;
            case 'adaptive': wasm.gs_adaptive_threshold_image(writeIdx, readIdx, params[0]); break;
            case 'erode': wasm.gs_erode_image(writeIdx, readIdx); break;
            case 'dilate': wasm.gs_dilate_image(writeIdx, readIdx); break;
            case 'sobel': wasm.gs_sobel_image(writeIdx, readIdx); break;
            case 'otsu':
                const threshold = wasm.gs_otsu_threshold_image(readIdx);
                wasm.gs_copy_image(writeIdx, readIdx);
                wasm.gs_threshold_image(writeIdx, threshold);
                break;
        }
        // Swap buffers
        [readIdx, writeIdx] = [writeIdx, (writeIdx + 1) % 3 === readIdx ? (writeIdx + 2) % 3 : (writeIdx + 1) % 3];
    });

    // 3. Display result from the last read buffer
    const resultGray = new Uint8Array(memory.buffer, imageBuffers[readIdx], imageSize);
    const imageDataOut = ctx.createImageData(width, height);
    grayToRgba(resultGray, imageDataOut.data);
    ctx.putImageData(imageDataOut, 0, 0);

    animationFrameId = requestAnimationFrame(processFrame);
}

init();