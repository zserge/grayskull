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
let overlayCtx;  // For drawing overlays
let templateKeypoints = null;  // For ORB template matching

const operations = {
    "blur": { params: [{ name: "radius", type: "range", default: 3, min: 1, max: 20 }] },
    "sobel": { params: [] },
    "otsu": { params: [] },
    "thresh": { params: [{ name: "threshold", type: "range", default: 128, min: 0, max: 255 }] },
    "adaptive": { params: [{ name: "block_size", type: "range", default: 15, min: 3, max: 51, step: 2 }] },
    "erode": { params: [{ name: "iterations", type: "range", default: 1, min: 1, max: 10 }] },
    "dilate": { params: [{ name: "iterations", type: "range", default: 1, min: 1, max: 10 }] },
    "blobs": { params: [{ name: "max_blobs", type: "range", default: 10, min: 1, max: 100 }] },
    "contour": { params: [] },
    "keypoints": { params: [{ name: "threshold", type: "range", default: 10, min: 5, max: 100 }, { name: "max_points", type: "range", default: 300, min: 10, max: 2000 }] },
    "orb": { params: [{ name: "threshold", type: "range", default: 30, min: 5, max: 500 }, { name: "max_features", type: "range", default: 100, min: 10, max: 300 }] },
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

// Template capture functionality
function captureTemplate() {
    if (!wasm || !imageBuffers[0]) {
        alert('Camera not running');
        return;
    }

    const width = canvas.width;
    const height = canvas.height;
    const imageSize = width * height;

    // Capture current frame as template
    ctx.drawImage(video, 0, 0, width, height);
    const imageData = ctx.getImageData(0, 0, width, height);
    const gray = new Uint8Array(imageSize);
    rgbaToGray(imageData.data, gray);
    new Uint8Array(memory.buffer, imageBuffers[0], imageSize).set(gray);

    // Extract ORB features from template
    const templateFeatures = wasm.gs_extract_orb_features(0, 20, 200);
    if (templateFeatures > 0) {
        wasm.gs_store_template_keypoints(templateFeatures);
        templateKeypoints = { count: templateFeatures };
        document.getElementById('template-status').textContent = `Template captured: ${templateFeatures} features`;
        console.log(`Template captured with ${templateFeatures} ORB features`);
    } else {
        alert('No features detected in template. Try capturing a more textured area.');
    }
}

async function init() {
    try {
        const importObject = { env: {} };
        // Fallback for WASM loading if streaming fails (MIME type issues)
        let wasmModule;
        try {
            const { instance } = await WebAssembly.instantiateStreaming(fetch('grayskull.wasm'), importObject);
            wasmModule = instance;
        } catch (streamError) {
            console.warn("Streaming failed, trying fallback:", streamError);
            const response = await fetch('grayskull.wasm');
            const bytes = await response.arrayBuffer();
            const { instance } = await WebAssembly.instantiate(bytes, importObject);
            wasmModule = instance;
        }
        wasm = wasmModule.exports;
        memory = wasm.memory;
        console.log("WebAssembly module loaded.");
    } catch (e) {
        console.warn("WASM initialization failed.", e);
        alert("Failed to load grayskull.wasm. Make sure the file is present and the server is running correctly.");
        return;
    }
    await populateCameraList();
    addStepButton.onclick = () => addPipelineStep();
    document.getElementById('capture-template').onclick = captureTemplate;
    addPipelineStep('blur'); // Add a default first step
    startButton.disabled = false;
}

async function populateCameraList() {
    try {
        if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) {
            throw new Error("Media device enumeration not supported.");
        }

        // Request camera permission first to get device labels
        let permissionStream;
        try {
            permissionStream = await navigator.mediaDevices.getUserMedia({ video: true });
        } catch (permError) {
            console.warn("Could not get camera permission for labeling:", permError);
        }

        const devices = await navigator.mediaDevices.enumerateDevices();
        const videoDevices = devices.filter(d => d.kind === 'videoinput');

        // Stop the permission stream
        if (permissionStream) {
            permissionStream.getTracks().forEach(track => track.stop());
        }

        cameraSelect.innerHTML = '';
        if (videoDevices.length === 0) {
            const option = document.createElement('option');
            option.textContent = 'No cameras found';
            cameraSelect.appendChild(option);
            return;
        }

        videoDevices.forEach((device, i) => {
            const option = document.createElement('option');
            option.value = device.deviceId;
            option.textContent = device.label || `Camera ${i + 1}`;
            cameraSelect.appendChild(option);
        });
    } catch (error) {
        console.error("Could not enumerate video devices:", error);
        // Add a fallback option
        cameraSelect.innerHTML = '<option value="">Default Camera</option>';
    }
}

startButton.onclick = async () => {
    if (videoStream) stopCamera();
    try {
        // More flexible constraints - prefer 320x240 but allow fallback
        const constraints = {
            video: {
                width: { ideal: 320, min: 160, max: 640 },
                height: { ideal: 240, min: 120, max: 480 },
                frameRate: { ideal: 30, max: 60 }
            }
        };

        // Add device constraint if a specific camera is selected
        const selectedDeviceId = cameraSelect.value;
        if (selectedDeviceId) {
            constraints.video.deviceId = { ideal: selectedDeviceId };
        }

        videoStream = await navigator.mediaDevices.getUserMedia(constraints);
        video.srcObject = videoStream;
        await video.play();

        canvas.width = video.videoWidth;
        canvas.height = video.videoHeight;

        console.log(`Video resolution: ${canvas.width}x${canvas.height}`);

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
        alert(`Could not start camera. Error: ${error.message}`);
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
    let overlayData = [];  // Store data for overlays

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
            case 'erode': wasm.gs_erode_image_iterations(writeIdx, readIdx, params[0] || 1); break;
            case 'dilate': wasm.gs_dilate_image_iterations(writeIdx, readIdx, params[0] || 1); break;
            case 'sobel': wasm.gs_sobel_image(writeIdx, readIdx); break;
            case 'otsu':
                const threshold = wasm.gs_otsu_threshold_image(readIdx);
                wasm.gs_copy_image(writeIdx, readIdx);
                wasm.gs_threshold_image(writeIdx, threshold);
                break;
            case 'blobs':
                wasm.gs_copy_image(writeIdx, readIdx);
                const numBlobs = wasm.gs_detect_blobs(readIdx, params[0] || 10);
                overlayData.push({ type: 'blobs', count: numBlobs });
                break;
            case 'contour':
                wasm.gs_copy_image(writeIdx, readIdx);
                const hasContour = wasm.gs_detect_largest_blob_contour(readIdx, 50);
                if (hasContour) {
                    overlayData.push({ type: 'contour', hasContour: true });
                }
                break;
            case 'keypoints':
                wasm.gs_copy_image(writeIdx, readIdx);
                const numKeypoints = wasm.gs_detect_fast_keypoints(readIdx, params[0] || 20, params[1] || 100);
                overlayData.push({ type: 'keypoints', count: numKeypoints });
                break;
            case 'orb':
                wasm.gs_copy_image(writeIdx, readIdx);
                const numOrbFeatures = wasm.gs_extract_orb_features(readIdx, params[0] || 20, params[1] || 100);
                overlayData.push({ type: 'orb', count: numOrbFeatures });

                // If we have template keypoints, try to match
                if (templateKeypoints && templateKeypoints.count > 0) {
                    const numMatches = wasm.gs_match_orb_features(templateKeypoints.count, numOrbFeatures, 60.0);
                    overlayData.push({ type: 'matches', count: numMatches });
                }
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

    // 4. Draw overlays
    drawOverlays(overlayData, width, height);

    animationFrameId = requestAnimationFrame(processFrame);
}

function drawOverlays(overlayData, width, height) {
    if (!overlayData.length) return;

    // Create overlay canvas for drawing annotations
    ctx.strokeStyle = '#ff0000';
    ctx.fillStyle = '#ff0000';
    ctx.lineWidth = 2;

    overlayData.forEach(overlay => {
        switch (overlay.type) {
            case 'blobs':
                drawBlobs(overlay.count);
                break;
            case 'contour':
                if (overlay.hasContour) {
                    drawContour();
                }
                break;
            case 'keypoints':
                drawKeypoints(overlay.count, '#00ff00');
                break;
            case 'orb':
                drawKeypoints(overlay.count, '#0080ff', true);
                break;
            case 'matches':
                drawMatches(overlay.count);
                break;
        }
    });
}

function drawBlobs(count) {
    ctx.strokeStyle = '#ff0000';
    ctx.lineWidth = 2;

    for (let i = 0; i < count; i++) {
        const blobPtr = wasm.gs_get_blob(i);
        if (!blobPtr) continue;

        // Read blob data from WASM memory
        const blobData = new Uint32Array(memory.buffer, blobPtr, 8); // gs_blob struct
        const area = blobData[1];
        if (area < 50) continue; // Skip very small blobs

        const x = blobData[2];
        const y = blobData[3];
        const w = blobData[4];
        const h = blobData[5];
        const centroidX = blobData[6];
        const centroidY = blobData[7];

        // Draw bounding box (rectangle)
        ctx.strokeStyle = '#ff0000';
        ctx.strokeRect(x, y, w, h);

        // Draw centroid
        ctx.fillStyle = '#ff0000';
        ctx.beginPath();
        ctx.arc(centroidX, centroidY, 3, 0, 2 * Math.PI);
        ctx.fill();

        // Draw area text
        ctx.fillStyle = '#ffffff';
        ctx.fillText(`${area}`, x, y - 5);

        // Draw corners
        const cornersPtr = wasm.gs_get_blob_corners(i);
        if (cornersPtr) {
            ctx.fillStyle = '#00ff00';
            const cornersData = new Uint32Array(memory.buffer, cornersPtr, 8); // 4 points, 2 coords each
            for (let j = 0; j < 4; j++) {
                const cornerX = cornersData[j * 2];
                const cornerY = cornersData[j * 2 + 1];
                ctx.beginPath();
                ctx.arc(cornerX, cornerY, 2, 0, 2 * Math.PI);
                ctx.fill();
            }
        }
    }
}

function drawContour() {
    const contourPtr = wasm.gs_get_contour();
    if (!contourPtr) return;

    // Read contour data
    const contourData = new Uint32Array(memory.buffer, contourPtr, 6); // gs_contour struct
    const startX = contourData[4];
    const startY = contourData[5];
    const length = contourData[6];

    if (length === 0) return;

    // Draw contour as a highlighted outline
    ctx.strokeStyle = '#ffff00';
    ctx.lineWidth = 2;

    // For simplicity, draw a circle at the start point to indicate contour detection
    ctx.beginPath();
    ctx.arc(startX, startY, 8, 0, 2 * Math.PI);
    ctx.stroke();

    // Draw length text
    ctx.fillStyle = '#ffff00';
    ctx.fillText(`Contour: ${length}px`, startX + 10, startY - 10);
}

function drawKeypoints(count, color, withOrientation = false) {
    ctx.strokeStyle = color;
    ctx.fillStyle = color;
    ctx.lineWidth = 1;

    for (let i = 0; i < count; i++) {
        const kpPtr = withOrientation ? wasm.gs_get_orb_keypoint(i) : wasm.gs_get_keypoint(i);
        if (!kpPtr) continue;

        // Read keypoint data (x, y, response, angle)
        const kpData = new Uint32Array(memory.buffer, kpPtr, 3);
        const x = kpData[0];
        const y = kpData[1];
        const response = kpData[2];

        if (response < 10) continue; // Skip weak keypoints

        // Draw circle
        ctx.beginPath();
        ctx.arc(x, y, 3, 0, 2 * Math.PI);
        ctx.stroke();

        // Draw orientation line for ORB features
        if (withOrientation) {
            const angleData = new Float32Array(memory.buffer, kpPtr + 12, 1);
            const angle = angleData[0];
            const length = 8;
            const endX = x + Math.cos(angle) * length;
            const endY = y + Math.sin(angle) * length;

            ctx.beginPath();
            ctx.moveTo(x, y);
            ctx.lineTo(endX, endY);
            ctx.stroke();
        }
    }
}

function drawMatches(count) {
    // Yellow circles show ORB feature matches between the captured template and current scene
    // Lower distance numbers indicate better matches (more similar features)
    // Use "Capture Template" button to set a reference image for matching
    if (!templateKeypoints || count === 0) return;

    ctx.strokeStyle = '#ffff00';
    ctx.lineWidth = 1;

    for (let i = 0; i < count; i++) {
        const matchPtr = wasm.gs_get_match(i);
        if (!matchPtr) continue;

        // Read match data (idx1, idx2, distance)
        const matchData = new Uint32Array(memory.buffer, matchPtr, 3);
        const templateIdx = matchData[0];
        const sceneIdx = matchData[1];
        const distance = matchData[2];

        if (distance > 40) continue; // Skip poor matches

        // Get scene keypoint
        const sceneKpPtr = wasm.gs_get_orb_keypoint(sceneIdx);
        if (!sceneKpPtr) continue;

        const sceneKpData = new Uint32Array(memory.buffer, sceneKpPtr, 2);
        const sceneX = sceneKpData[0];
        const sceneY = sceneKpData[1];

        // Draw match indicator (yellow circle = matched feature)
        ctx.fillStyle = '#ffff00';
        ctx.beginPath();
        ctx.arc(sceneX, sceneY, 5, 0, 2 * Math.PI);
        ctx.fill();

        // Draw match distance (lower = better match)
        ctx.fillStyle = '#000000';
        ctx.fillText(`${distance}`, sceneX + 6, sceneY - 6);
        ctx.fillStyle = '#ffffff';
        ctx.fillText(`${distance}`, sceneX + 6, sceneY - 6);
    }
}

init();