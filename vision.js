// vision.js — webcam + MediaPipe FaceLandmarker (vendored, runs fully in the browser) + signal extraction.
// Nothing leaves the page: frames go from <video> straight into the local wasm/WebGL model.
import { FaceLandmarker, FilesetResolver } from './vendor/tasks-vision/vision_bundle.mjs';

const WASM_DIR = new URL('./vendor/tasks-vision/wasm', import.meta.url).href;
const MODEL_URL = new URL('./vendor/face_landmarker.task', import.meta.url).href;
const RAD2DEG = 180 / Math.PI;

export class Vision {
  constructor() {
    this.landmarker = null;
    this.delegate = null;
    this.stream = null;
    this._fileset = null;
    this._lastTs = 0;
  }

  /** Load wasm + model. Tries the GPU delegate first and falls back to CPU. */
  async init() {
    this._fileset = await FilesetResolver.forVisionTasks(WASM_DIR);
    try {
      await this._create('GPU');
    } catch (e) {
      console.warn('[vision] GPU delegate unavailable, using CPU:', e?.message || e);
      await this._create('CPU');
    }
    return this.delegate;
  }

  async _create(delegate) {
    this.landmarker = await FaceLandmarker.createFromOptions(this._fileset, {
      baseOptions: { modelAssetPath: MODEL_URL, delegate },
      runningMode: 'VIDEO',
      numFaces: 1,
      outputFaceBlendshapes: true,
      outputFacialTransformationMatrixes: true,
    });
    this.delegate = delegate;
  }

  async startCamera(video) {
    this.stream = await navigator.mediaDevices.getUserMedia({
      video: { width: { ideal: 640 }, height: { ideal: 480 }, facingMode: 'user' },
      audio: false,
    });
    video.srcObject = this.stream;
    await video.play();
  }

  stopCamera(video) {
    this.stream?.getTracks().forEach((t) => t.stop());
    this.stream = null;
    if (video) video.srcObject = null;
  }

  /**
   * Run the model on the current video frame. The video is passed UNmirrored (the preview is
   * mirrored with CSS only), so blendshape Left/Right refer to the user's own left/right.
   * @returns {{signals: Signals, landmarks: Array|null}}
   */
  detect(video, nowMs) {
    const ts = Math.max(nowMs, this._lastTs + 1); // MediaPipe needs strictly increasing timestamps
    this._lastTs = ts;
    let result;
    try {
      result = this.landmarker.detectForVideo(video, ts);
    } catch (e) {
      if (this.delegate === 'GPU') { // some GPUs fail only at first inference
        console.warn('[vision] GPU inference failed, switching to CPU:', e?.message || e);
        const old = this.landmarker;
        this.landmarker = null; // `ready` is false until the CPU instance exists
        try { old.close(); } catch {}
        this._create('CPU').catch((err) => console.error(err));
      }
      throw e;
    }
    return { signals: extractSignals(result), landmarks: result.faceLandmarks?.[0] || null };
  }

  get ready() { return !!this.landmarker; }
}

/**
 * @typedef {{face:boolean, yaw:number, pitch:number, roll:number, gazeDown:number, gazeUp:number,
 *            gazeSide:number, blink:number, cx:number, cy:number}} Signals
 */

/** Turn a FaceLandmarkerResult into the scalar signals the classifier uses. */
export function extractSignals(result) {
  const lm = result?.faceLandmarks?.[0];
  if (!lm || !lm.length) {
    return { face: false, yaw: 0, pitch: 0, roll: 0, gazeDown: 0, gazeUp: 0, gazeSide: 0, blink: 0, cx: 0.5, cy: 0.5 };
  }

  // Face centre in normalized coordinates of the RAW (unmirrored) camera image: mean of all landmarks.
  let cx = 0, cy = 0;
  for (const p of lm) { cx += p.x; cy += p.y; }
  cx /= lm.length; cy /= lm.length;

  const pose = headPose(result.facialTransformationMatrixes?.[0]);

  const bs = {};
  for (const c of result.faceBlendshapes?.[0]?.categories || []) bs[c.categoryName] = c.score;
  const g = (k) => bs[k] ?? 0;

  // Blendshape names are from the SUBJECT's point of view (eyeLookOutLeft = user's left eye
  // looking toward the user's left). Signed horizontal gaze: + = user looks to THEIR left.
  const towardUserLeft = Math.max(g('eyeLookOutLeft'), g('eyeLookInRight'));
  const towardUserRight = Math.max(g('eyeLookInLeft'), g('eyeLookOutRight'));

  return {
    face: true,
    ...pose,
    gazeDown: (g('eyeLookDownLeft') + g('eyeLookDownRight')) / 2,
    gazeUp: (g('eyeLookUpLeft') + g('eyeLookUpRight')) / 2,
    gazeSide: towardUserLeft - towardUserRight,
    blink: (g('eyeBlinkLeft') + g('eyeBlinkRight')) / 2,
    cx, cy,
  };
}

/**
 * Head yaw/pitch/roll in degrees from MediaPipe's facial transformation matrix.
 *
 * Convention (MediaPipe face geometry "metric space"): right-handed, camera at the origin looking
 * down -Z, +X = right side of the raw camera image, +Y = UP. The matrix maps the canonical face
 * model (+Y toward the forehead, +Z out of the nose toward the viewer) into that space, so a
 * face looking straight at the camera has its forward vector f = R * (0,0,1) ≈ (0,0,+1).
 *   pitch = atan2(f.y, |f.xz|): nose tilted DOWN -> f.y < 0 -> pitch NEGATIVE (up = positive).
 *   yaw   = atan2(f.x, f.z):    positive = nose toward image +X = the user's own LEFT.
 *   roll  = in-plane tilt from the face's up vector u = R * (0,1,0): atan2(u.x, u.y), + = clockwise in the raw image.
 * Verified with a test portrait (README "Tuning" -> sign checks): frontal face gives f.z ≈ +1 and
 * a clockwise-rotated image gives u.x > 0 (so +X right / +Y up / column-major holds).
 *
 * The flattened data is column-major (OpenGL style: translation in [12..14]); we detect the
 * layout from where the (large, negative-Z) translation lives so a row-major build would also work.
 */
export function headPose(matrix) {
  const d = matrix?.data;
  if (!d || d.length < 16) return { yaw: 0, pitch: 0, roll: 0 };
  const colMajor = Math.abs(d[12]) + Math.abs(d[13]) + Math.abs(d[14]) >= Math.abs(d[3]) + Math.abs(d[7]) + Math.abs(d[11]);
  const m = colMajor ? (r, c) => d[c * 4 + r] : (r, c) => d[r * 4 + c];
  let fx = m(0, 2), fy = m(1, 2), fz = m(2, 2);
  const n = Math.hypot(fx, fy, fz) || 1;
  fx /= n; fy /= n; fz /= n;
  const ux = m(0, 1), uy = m(1, 1);
  return {
    yaw: Math.atan2(fx, fz) * RAD2DEG,
    pitch: Math.atan2(fy, Math.hypot(fx, fz)) * RAD2DEG,
    roll: Math.atan2(ux, uy) * RAD2DEG,
  };
}
