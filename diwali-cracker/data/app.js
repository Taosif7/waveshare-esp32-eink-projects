(() => {
  // Rendering/physics approach adapted from BlastSky techniques:
  // https://blastsky.com/ — stroke trails, destination-out fade, HSLA cooling,
  // sphere shells, glitter/trail dust, multi-type bursts.
  const bgCanvas = document.getElementById("bg");
  const fxCanvas = document.getElementById("fx");
  const arm = document.getElementById("arm");
  const greet = document.getElementById("greet");
  const startEl = document.getElementById("start");
  const startBtn = document.getElementById("start-btn");
  const amoledToggle = document.getElementById("amoled");
  const bg = bgCanvas.getContext("2d", { alpha: false });
  const fx = fxCanvas.getContext("2d", { alpha: true });

  let W = 0;
  let H = 0;
  let dpr = 1;
  let S = 1; // scale like BlastSky's this.s
  let audioCtx = null;
  let masterGain = null;
  let armed = false;
  let started = false;
  let noiseCache = null;
  let amoledMode = localStorage.getItem("diwali_amoled") !== "0";
  let nextBurstId = 1;
  const BURST_SCALE = 1.45; // bigger shells to fill more of the screen
  // BlastSky boom samples (copied from https://blastsky.com/sounds/fireworks/)
  const boomBuffers = { boom1: null, boom2: null };
  let boomsReady = false;
  let boomFlip = false;
  if (amoledToggle) amoledToggle.checked = amoledMode;
  document.body.classList.toggle("mode-amoled", amoledMode);
  document.body.classList.toggle("mode-normal", !amoledMode);

  const rockets = [];
  const sparks = [];
  const delayed = [];
  const cores = [];
  const skyline = [];
  let flash = 0;
  let flashX = 0;
  let flashY = 0;
  let flashHue = 40;
  const maxSparks = 2800;

  const FUSE_MS = 57 * 28;
  const PALETTES = [
    [28, 100],
    [8, 100],
    [45, 100],
    [200, 90],
    [280, 90],
    [140, 90],
    [320, 95],
    [55, 100],
  ];

  const SHELL_WEIGHTS = [
    ["peony", 16],
    ["chrysanthemum", 11],
    ["willow", 7],
    ["ring", 6],
    ["palm", 6],
    ["crackle", 8],
    ["double", 8],
    ["crossette", 7],
    ["pistil", 9],
    ["kamuro", 8],
    ["brocade", 8],
    ["strobe", 4],
    ["comet", 4],
    ["spiral", 5],
    ["twocolor", 5],
    ["colorchange", 6],
    ["chryscrackle", 5],
    ["dahlia", 4],
    ["silver", 5],
  ];

  function rand(a, b) {
    return a + Math.random() * (b - a);
  }

  function pick(arr) {
    return arr[(Math.random() * arr.length) | 0];
  }

  function weightedShell() {
    let total = 0;
    for (const [, w] of SHELL_WEIGHTS) total += w;
    let t = Math.random() * total;
    for (const [name, w] of SHELL_WEIGHTS) {
      t -= w;
      if (t <= 0) return name;
    }
    return "peony";
  }

  function paletteHue() {
    return pick(PALETTES);
  }

  function resize() {
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    W = window.innerWidth;
    H = window.innerHeight;
    S = Math.max(0.85, Math.min(H / 820, 1.55));
    for (const c of [bgCanvas, fxCanvas]) {
      c.width = Math.floor(W * dpr);
      c.height = Math.floor(H * dpr);
    }
    bg.setTransform(dpr, 0, 0, dpr, 0, 0);
    fx.setTransform(dpr, 0, 0, dpr, 0, 0);
    paintBackground();
  }

  function paintBackground() {
    buildSkyline();
    if (amoledMode) {
      // Pure black = AMOLED pixels off until rockets/bursts light the scene.
      bg.fillStyle = "#000000";
      bg.fillRect(0, 0, W, H);
      return;
    }

    // Normal mode: always-visible dark sky + skyline.
    const g = bg.createLinearGradient(0, 0, 0, H);
    g.addColorStop(0, "#000000");
    g.addColorStop(0.5, "#05020a");
    g.addColorStop(1, "#120810");
    bg.fillStyle = g;
    bg.fillRect(0, 0, W, H);

    let seed = 42;
    const rnd = () => {
      seed = (seed * 16807) % 2147483647;
      return (seed - 1) / 2147483646;
    };
    const n = Math.floor((W * H) / 9000);
    for (let i = 0; i < n; i++) {
      bg.globalAlpha = 0.12 + 0.5 * rnd();
      bg.fillStyle = rnd() > 0.7 ? "#ffd7a0" : "#ffe8c8";
      const sz = 0.5 + rnd() * 1.1;
      bg.fillRect(rnd() * W, rnd() * H * 0.72, sz, sz);
    }
    bg.globalAlpha = 1;

    bg.fillStyle = "#08040c";
    bg.strokeStyle = "#0c0812";
    for (const b of skyline) b.draw(bg);

    bg.fillStyle = "rgba(255,140,60,0.22)";
    for (const b of skyline) {
      if (b.windows) {
        for (let i = 0; i < b.windows.length; i += 2) {
          bg.fillRect(b.windows[i], b.windows[i + 1], 2, 2);
        }
      }
      if (b.bands) {
        for (const yy of b.bands.ys) {
          bg.fillStyle = "rgba(255,120,40,0.16)";
          bg.fillRect(b.bands.x, yy, b.bands.w, 2);
        }
        bg.fillStyle = "rgba(255,140,60,0.22)";
      }
    }
    bg.fillStyle = "#020106";
    bg.fillRect(0, H - H * 0.025, W, H * 0.025);
  }

  function setAmoledMode(on) {
    amoledMode = !!on;
    localStorage.setItem("diwali_amoled", amoledMode ? "1" : "0");
    document.body.classList.toggle("mode-amoled", amoledMode);
    document.body.classList.toggle("mode-normal", !amoledMode);
    paintBackground();
    // Clear lingering FX so mode switch doesn't leave ghosts.
    fx.setTransform(dpr, 0, 0, dpr, 0, 0);
    fx.globalCompositeOperation = "source-over";
    fx.clearRect(0, 0, W, H);
  }

  function buildSkyline() {
    skyline.length = 0;
    let seed = 2026;
    const rnd = () => {
      seed = (seed * 16807) % 2147483647;
      return (seed - 1) / 2147483646;
    };
    let x = -20;
    while (x < W + 40) {
      const kind = rnd();
      const gap = 2 + rnd() * 8;
      if (kind < 0.38) {
        const ww = 36 + rnd() * 70;
        const hh = H * (0.05 + 0.08 * rnd());
        const bx = x;
        const windows = [];
        for (let yy = H - hh + 6; yy < H - 4; yy += 9) {
          for (let xx = bx + 5; xx < bx + ww - 5; xx += 8) {
            if (rnd() < 0.14) windows.push(xx, yy);
          }
        }
        skyline.push({
          draw(ctx) {
            ctx.fillRect(bx, H - hh, ww, hh);
          },
          windows,
        });
        x += ww + gap;
      } else if (kind < 0.62) {
        const ww = 18 + rnd() * 28;
        const hh = H * (0.16 + 0.24 * rnd());
        const bx = x;
        const ant = 8 + rnd() * 18;
        const bands = [];
        for (let yy = H - hh + 8; yy < H - 6; yy += 11) {
          if (rnd() < 0.4) bands.push(yy);
        }
        skyline.push({
          draw(ctx) {
            ctx.fillRect(bx, H - hh, ww, hh);
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(bx + ww * 0.5, H - hh);
            ctx.lineTo(bx + ww * 0.5, H - hh - ant);
            ctx.stroke();
          },
          bands: { x: bx + 3, w: ww - 6, ys: bands },
        });
        x += ww + gap;
      } else if (kind < 0.82) {
        const base = 22 + rnd() * 40;
        const hh = H * (0.09 + 0.13 * rnd());
        const bx = x;
        skyline.push({
          draw(ctx) {
            ctx.beginPath();
            ctx.moveTo(bx, H);
            ctx.lineTo(bx, H - hh * 0.55);
            ctx.lineTo(bx + base * 0.22, H - hh);
            ctx.lineTo(bx + base * 0.45, H - hh * 0.5);
            ctx.lineTo(bx + base * 0.62, H - hh * 1.05);
            ctx.lineTo(bx + base * 0.8, H - hh * 0.45);
            ctx.lineTo(bx + base, H - hh * 0.7);
            ctx.lineTo(bx + base, H);
            ctx.closePath();
            ctx.fill();
          },
        });
        x += base + gap;
      } else {
        const ww = 30 + rnd() * 50;
        const hh = H * (0.07 + 0.11 * rnd());
        const bx = x;
        skyline.push({
          draw(ctx) {
            ctx.fillRect(bx, H - hh * 0.45, ww, hh * 0.45);
            ctx.beginPath();
            ctx.ellipse(bx + ww * 0.5, H - hh * 0.45, ww * 0.42, hh * 0.55, 0, Math.PI, 0);
            ctx.fill();
            ctx.fillRect(bx + ww * 0.5 - 1, H - hh - 6, 2, 8);
          },
        });
        x += ww + gap;
      }
    }
  }

  function drawLitSkyline() {
    // Only silhouette that fireworks / rockets can light — otherwise fully off.
    const lights = [];
    if (flash > 0.03) lights.push({ x: flashX, y: flashY, p: flash, hue: flashHue });
    for (const r of rockets) {
      lights.push({ x: r.x, y: r.y, p: 0.35, hue: 14 });
    }
    // Nearby bright sparks also wash the horizon a little
    let sparkWash = 0;
    for (let i = 0; i < sparks.length; i += 12) {
      const s = sparks[i];
      if (s.y > H * 0.55) sparkWash = Math.min(0.25, sparkWash + 0.008);
    }
    if (!lights.length && sparkWash < 0.02) return;

    fx.save();
    fx.globalCompositeOperation = "lighter";
    for (const L of lights) {
      const reach = Math.hypot(L.x - W * 0.5, L.y - H) + H * 0.35;
      const a = Math.min(0.45, L.p * 0.55 * (0.35 + 0.65 * (1 - L.y / H)));
      if (a < 0.02) continue;
      fx.fillStyle = `hsla(${L.hue},55%,18%,${a})`;
      fx.strokeStyle = `hsla(${L.hue},50%,22%,${a * 0.8})`;
      for (const b of skyline) b.draw(fx);
      // Windows catch the burst
      fx.fillStyle = `hsla(${L.hue},100%,55%,${a * 0.9})`;
      for (const b of skyline) {
        if (b.windows) {
          for (let i = 0; i < b.windows.length; i += 2) {
            const wx = b.windows[i];
            const wy = b.windows[i + 1];
            const d = Math.hypot(wx - L.x, wy - L.y);
            if (d < reach * 0.85) fx.fillRect(wx, wy, 2, 2);
          }
        }
        if (b.bands) {
          for (const yy of b.bands.ys) {
            const d = Math.hypot(b.bands.x + b.bands.w * 0.5 - L.x, yy - L.y);
            if (d < reach) fx.fillRect(b.bands.x, yy, b.bands.w, 2);
          }
        }
      }
    }
    if (sparkWash > 0.02) {
      fx.fillStyle = `hsla(18,40%,14%,${sparkWash})`;
      for (const b of skyline) b.draw(fx);
    }
    fx.restore();
  }

  // --- audio (procedural; louder) ---
  function ensureAudio() {
    if (!audioCtx) {
      const AC = window.AudioContext || window.webkitAudioContext;
      audioCtx = new AC();
      masterGain = audioCtx.createGain();
      masterGain.gain.value = 1.4;
      const comp = audioCtx.createDynamicsCompressor();
      comp.threshold.value = -14;
      comp.ratio.value = 6;
      masterGain.connect(comp);
      comp.connect(audioCtx.destination);
      noiseCache = makeNoise(2);
    }
    if (audioCtx.state === "suspended") audioCtx.resume();
  }

  function armAudio() {
    ensureAudio();
    armed = true;
    arm.hidden = true;
  }

  async function loadBoomSamples() {
    ensureAudio();
    const names = ["boom1", "boom2"];
    await Promise.all(
      names.map(async (name) => {
        if (boomBuffers[name]) return;
        try {
          const res = await fetch(`/sounds/${name}.mp3`);
          const raw = await res.arrayBuffer();
          boomBuffers[name] = await audioCtx.decodeAudioData(raw.slice(0));
        } catch (err) {
          console.warn("boom sample load failed", name, err);
        }
      })
    );
    boomsReady = !!(boomBuffers.boom1 || boomBuffers.boom2);
  }

  function makeNoise(duration) {
    const rate = audioCtx.sampleRate;
    const len = Math.floor(rate * duration);
    const buf = audioCtx.createBuffer(1, len, rate);
    const data = buf.getChannelData(0);
    let last = 0;
    for (let i = 0; i < len; i++) {
      const white = Math.random() * 2 - 1;
      last = (last + 0.02 * white) / 1.02;
      data[i] = last * 3.5;
    }
    return buf;
  }

  function dest() {
    return masterGain || audioCtx.destination;
  }

  function playFuseTrail(duration = 1.6) {
    // Burning fuse: bright sizzle + sharp spark pops + thin rising edge.
    if (!armed || !audioCtx || !noiseCache) return;
    const t0 = audioCtx.currentTime;
    const dur = Math.max(0.45, duration);

    // 1) Sharp fuse sizzle — brighter, louder high band.
    {
      const src = audioCtx.createBufferSource();
      src.buffer = noiseCache;
      src.loop = true;
      const bp = audioCtx.createBiquadFilter();
      bp.type = "bandpass";
      bp.Q.value = 1.4;
      bp.frequency.setValueAtTime(1600, t0);
      bp.frequency.exponentialRampToValueAtTime(4200, t0 + dur);
      const hp = audioCtx.createBiquadFilter();
      hp.type = "highpass";
      hp.frequency.value = 1200;
      const g = audioCtx.createGain();
      g.gain.setValueAtTime(0.0001, t0);
      g.gain.exponentialRampToValueAtTime(0.55, t0 + 0.04);
      g.gain.setValueAtTime(0.5, t0 + dur * 0.7);
      g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
      src.connect(bp);
      bp.connect(hp);
      hp.connect(g);
      g.connect(dest());
      src.start(t0);
      src.stop(t0 + dur + 0.02);
    }

    // 2) Mid hiss layer for body (still bright, not muddy).
    {
      const src = audioCtx.createBufferSource();
      src.buffer = noiseCache;
      src.loop = true;
      const bp = audioCtx.createBiquadFilter();
      bp.type = "bandpass";
      bp.Q.value = 0.8;
      bp.frequency.setValueAtTime(900, t0);
      bp.frequency.exponentialRampToValueAtTime(2200, t0 + dur);
      const g = audioCtx.createGain();
      g.gain.setValueAtTime(0.0001, t0);
      g.gain.exponentialRampToValueAtTime(0.32, t0 + 0.05);
      g.gain.linearRampToValueAtTime(0.28, t0 + dur * 0.65);
      g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
      src.connect(bp);
      bp.connect(g);
      g.connect(dest());
      src.start(t0);
      src.stop(t0 + dur + 0.02);
    }

    // 3) Thin rising edge — present but not a siren.
    {
      const osc = audioCtx.createOscillator();
      const osc2 = audioCtx.createOscillator();
      const g = audioCtx.createGain();
      const bp = audioCtx.createBiquadFilter();
      osc.type = "triangle";
      osc2.type = "sine";
      osc.frequency.setValueAtTime(480, t0);
      osc.frequency.exponentialRampToValueAtTime(1400, t0 + dur);
      osc2.frequency.setValueAtTime(960, t0);
      osc2.frequency.exponentialRampToValueAtTime(2800, t0 + dur);
      bp.type = "bandpass";
      bp.frequency.setValueAtTime(700, t0);
      bp.frequency.exponentialRampToValueAtTime(1800, t0 + dur);
      bp.Q.value = 2.2;
      g.gain.setValueAtTime(0.0001, t0);
      g.gain.exponentialRampToValueAtTime(0.14, t0 + 0.08);
      g.gain.linearRampToValueAtTime(0.18, t0 + dur * 0.7);
      g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
      osc.connect(bp);
      osc2.connect(bp);
      bp.connect(g);
      g.connect(dest());
      osc.start(t0);
      osc2.start(t0);
      osc.stop(t0 + dur + 0.02);
      osc2.stop(t0 + dur + 0.02);
    }

    // 4) Crisp spark pops along the fuse.
    const pops = 14 + ((Math.random() * 10) | 0);
    for (let i = 0; i < pops; i++) {
      const when = t0 + 0.06 + Math.random() * (dur * 0.88);
      const src = audioCtx.createBufferSource();
      src.buffer = noiseCache;
      const bp = audioCtx.createBiquadFilter();
      bp.type = "bandpass";
      bp.frequency.value = 2800 + Math.random() * 5500;
      bp.Q.value = 8;
      const g = audioCtx.createGain();
      const peak = 0.16 + Math.random() * 0.22;
      g.gain.setValueAtTime(0.0001, when);
      g.gain.exponentialRampToValueAtTime(peak, when + 0.002);
      g.gain.exponentialRampToValueAtTime(0.0001, when + 0.02 + Math.random() * 0.025);
      src.connect(bp);
      bp.connect(g);
      g.connect(dest());
      src.start(when);
      src.stop(when + 0.06);
    }
  }

  function playBoomProcedural(power = 1, y01 = 0.3) {
    if (!armed || !audioCtx || !noiseCache) return;
    const t0 = audioCtx.currentTime + 0.04;
    const p = Math.max(0.35, power) * (0.75 + (1 - y01) * 0.35);

    const src = audioCtx.createBufferSource();
    src.buffer = noiseCache;
    const lp = audioCtx.createBiquadFilter();
    lp.type = "lowpass";
    lp.frequency.setValueAtTime(420 + 380 * p, t0);
    lp.frequency.exponentialRampToValueAtTime(70, t0 + 0.7);
    const gain = audioCtx.createGain();
    gain.gain.setValueAtTime(0.0001, t0);
    gain.gain.exponentialRampToValueAtTime(1.15 * p, t0 + 0.01);
    gain.gain.exponentialRampToValueAtTime(0.35 * p, t0 + 0.14);
    gain.gain.exponentialRampToValueAtTime(0.0001, t0 + 1.35);
    src.connect(lp);
    lp.connect(gain);
    gain.connect(dest());
    src.start(t0);
    src.stop(t0 + 1.4);

    const osc = audioCtx.createOscillator();
    const og = audioCtx.createGain();
    osc.type = "sine";
    osc.frequency.setValueAtTime(90, t0);
    osc.frequency.exponentialRampToValueAtTime(28, t0 + 0.45);
    og.gain.setValueAtTime(0.0001, t0);
    og.gain.exponentialRampToValueAtTime(0.95 * p, t0 + 0.008);
    og.gain.exponentialRampToValueAtTime(0.0001, t0 + 0.5);
    osc.connect(og);
    og.connect(dest());
    osc.start(t0);
    osc.stop(t0 + 0.52);
  }

  function playBoom(power = 1, y01 = 0.3, prefer) {
    // Playback matches BlastSky: sample + slight rate jitter + height delay.
    if (!armed || !audioCtx) return;
    const name =
      prefer ||
      (boomFlip ? "boom1" : "boom2");
    boomFlip = !boomFlip;
    const buf = boomBuffers[name] || boomBuffers.boom2 || boomBuffers.boom1;
    if (!buf) {
      playBoomProcedural(power, y01);
      return;
    }
    const delay = 0.06 + (1 - Math.max(0, Math.min(1, y01))) * 0.22;
    const t0 = audioCtx.currentTime + delay;
    const src = audioCtx.createBufferSource();
    src.buffer = buf;
    src.playbackRate.value = 0.95 + 0.1 * Math.random();
    const gain = audioCtx.createGain();
    const vol = Math.min(1.35, Math.max(0.35, power) * 1.15);
    gain.gain.value = vol * (name === "boom2" ? 1 : 0.85);
    src.connect(gain);
    gain.connect(dest());
    src.start(t0);
  }

  function playCrackle(count = 22) {
    if (!armed || !audioCtx) return;
    const t0 = audioCtx.currentTime;
    for (let i = 0; i < count; i++) {
      const when = t0 + 0.04 + Math.random() * 0.7;
      const src = audioCtx.createBufferSource();
      src.buffer = noiseCache;
      const filter = audioCtx.createBiquadFilter();
      filter.type = "bandpass";
      filter.frequency.value = 2200 + Math.random() * 4500;
      filter.Q.value = 4;
      const gain = audioCtx.createGain();
      const peak = 0.14 + Math.random() * 0.16;
      gain.gain.setValueAtTime(0.0001, when);
      gain.gain.exponentialRampToValueAtTime(peak, when + 0.003);
      gain.gain.exponentialRampToValueAtTime(0.0001, when + 0.045);
      src.connect(filter);
      filter.connect(gain);
      gain.connect(dest());
      src.start(when);
      src.stop(when + 0.06);
    }
  }

  function flashAt(x, y, hue, amount) {
    if (amount > 0.3) {
      cores.push({ x, y, age: 0 });
      const n = 22;
      for (let i = 0; i < n && sparks.length < maxSparks; i++) {
        const a = Math.random() * Math.PI * 2;
        const sp = rand(380, 620) * S;
        sparks.push({
          x, y,
          vx: Math.cos(a) * sp,
          vy: Math.sin(a) * sp,
          age: 0,
          life: rand(0.16, 0.34),
          h: 48, h2: 40,
          size: 2.2,
          drag: 0.9,
          g: 20 * S,
          glitter: false,
          trail: 0,
          dust: false,
          sat: 12,
          split: 0,
        });
      }
    }
    flash = Math.min(0.7, flash + amount);
    flashX = x;
    flashY = y;
    flashHue = hue;
  }

  function pushSpark(p) {
    if (sparks.length >= maxSparks) return;
    sparks.push(p);
  }

  function activeBurstCount() {
    const ids = new Set();
    for (const s of sparks) {
      if (s.burstId && !s.dust) ids.add(s.burstId);
    }
    return ids.size;
  }

  function updateGreeting() {
    if (!greet) return;
    const on = activeBurstCount() >= 5;
    greet.hidden = false;
    greet.classList.toggle("is-on", on);
  }

  function burst(x, y, r) {
    const shape = r.shape || "sphere";
    const ringish = shape === "ring" || shape === "spiral" || shape === "rose";
    const count = Math.max(8, Math.floor(r.count * BURST_SCALE));
    const burstId = r.burstId || 0;
    for (let i = 0; i < count; i++) {
      if (sparks.length >= maxSparks) return;
      let vx; let vy;
      const speed = r.speed * S * BURST_SCALE * (ringish ? 1 : 0.93 + 0.14 * Math.random());
      if (shape === "ring") {
        const a = (i / count) * Math.PI * 2;
        const ex = Math.cos(a) * speed;
        const ey = Math.sin(a) * speed * 0.42;
        // slight tilt like BlastSky
        vx = 0.8253356149096783 * ex - 0.5646424733950354 * ey;
        vy = 0.5646424733950354 * ex + 0.8253356149096783 * ey;
      } else if (shape === "spiral") {
        const t = (i >> 1) / (count / 2);
        const a = t * Math.PI * 5 + (i & 1) * Math.PI;
        const rad = 0.2 + 0.8 * t;
        vx = Math.cos(a) * speed * rad;
        vy = Math.sin(a) * speed * rad;
      } else if (shape === "rose") {
        const a = (i / count) * Math.PI * 2;
        const e = (0.25 + 0.75 * Math.abs(Math.cos(3 * a))) * (i % 3 === 0 ? rand(0.35, 0.9) : 1);
        vx = Math.cos(a) * e * speed;
        vy = Math.sin(a) * e * speed;
      } else if (shape === "cone") {
        const a = -Math.PI / 2 + rand(-0.6, 0.6);
        const u = speed * rand(0.55, 1);
        vx = Math.cos(a) * u;
        vy = Math.sin(a) * u;
      } else if (shape === "palm") {
        const a = ((i % 8) / 8) * Math.PI * 2 + rand(-0.04, 0.04);
        const u = speed * rand(0.45, 1.05);
        vx = Math.cos(a) * u;
        vy = Math.sin(a) * u - 30 * S;
      } else {
        const a = Math.random() * Math.PI * 2;
        const e = Math.sqrt(1 - Math.pow(rand(-1, 1), 2));
        vx = Math.cos(a) * e * speed;
        vy = Math.sin(a) * e * speed;
      }
      const hAlt = r.hAlt;
      pushSpark({
        x, y, vx, vy,
        age: 0,
        life: ringish ? r.life : r.life * rand(0.85, 1.12),
        h: hAlt !== undefined && vx < 0 ? hAlt : r.h,
        h2: hAlt !== undefined && vx < 0 ? hAlt : r.h2,
        size: (r.size ?? 1.7) * BURST_SCALE * (ringish ? 1 : rand(0.85, 1.25)),
        drag: ringish ? r.drag : r.drag * (1 + rand(-0.002, 0.002)),
        g: r.g * S * (ringish ? 0.55 : 1),
        glitter: !!r.glitter,
        trail: r.trail ?? 0,
        dust: false,
        sat: r.sat ?? 100,
        split: r.split ?? 0,
        wob: r.wob,
        blink: r.blink,
        ph: 6.28 * Math.random(),
        sw: r.sw,
        burstId,
      });
    }
  }

  function explode(x, y) {
    const type = weightedShell();
    const [h0, sat0] = paletteHue();
    const h2 = h0 + rand(8, 40);
    const burstId = nextBurstId++;
    const fire = (opts) => burst(x, y, { ...opts, burstId });
    flashAt(x, y, h0, 0.55);
    playBoom(1, y / H);
    if (type === "crackle" || type === "chryscrackle" || Math.random() > 0.6) {
      playCrackle(type === "crackle" ? 30 : 14);
    }

    switch (type) {
      case "peony":
        fire({ count: 190, speed: 300, life: 1.9, drag: 0.975, g: 45, h: h0, h2: h2 });
        break;
      case "chrysanthemum":
        fire({ count: 160, speed: 320, life: 2.4, drag: 0.978, g: 45, h: h0, h2: h2, trail: 0.22 });
        break;
      case "willow":
        fire({ count: 120, speed: 250, life: 3.6, drag: 0.968, g: 75, h: 40, h2: 22, sat: 100, trail: 0.5, size: 1.5 });
        break;
      case "ring":
        fire({ count: 110, speed: 260, life: 1.9, drag: 0.976, g: 40, h: h0, h2: h2, shape: "ring" });
        break;
      case "palm":
        fire({ count: 120, speed: 380, life: 2.6, drag: 0.972, g: 55, h: 42, h2: 18, sat: 100, trail: 0.7, shape: "palm" });
        break;
      case "crackle":
        fire({ count: 200, speed: 285, life: 2, drag: 0.974, g: 42, h: 48, h2: 30, sat: 100, glitter: true, size: 1.3 });
        break;
      case "crossette":
        fire({ count: 16, speed: 260, life: 2, drag: 0.978, g: 40, h: h0, h2: h2, size: 2.4, split: 0.8, trail: 0.3 });
        break;
      case "spiral":
        fire({ count: 130, speed: 330, life: 2.2, drag: 0.976, g: 38, h: h0, h2: h2, shape: "spiral" });
        break;
      case "pistil":
        fire({ count: 140, speed: 300, life: 2, drag: 0.976, g: 42, h: h0, h2: h2 });
        fire({ count: 70, speed: 130, life: 1.7, drag: 0.972, g: 38, h: h0 + pick([120, 150, 180, 210]), h2: h0 + 150, sat: 100, size: 2 });
        break;
      case "kamuro":
        fire({ count: 180, speed: 300, life: 4.8, drag: 0.972, g: 82, h: 40, h2: 22, sat: sat0, trail: 0.85, size: 1.5 });
        break;
      case "brocade":
        fire({ count: 200, speed: 290, life: 3.8, drag: 0.97, g: 72, h: 38, h2: 24, sat: sat0, trail: 0.7, glitter: true });
        break;
      case "strobe":
        fire({ count: 150, speed: 260, life: 2.8, drag: 0.976, g: 36, h: 210, h2: 200, sat: 10, size: 1.9, blink: 16 });
        break;
      case "comet":
        fire({ count: 14, speed: 210, life: 2.7, drag: 0.985, g: 26, h: h0, h2: h2, size: 2.8, trail: 1.3 });
        break;
      case "twocolor":
        fire({ count: 190, speed: 300, life: 2, drag: 0.975, g: 45, h: h0, h2: h0, hAlt: h0 + 150 });
        break;
      case "colorchange":
        fire({ count: 190, speed: 300, life: 2.7, drag: 0.976, g: 42, h: h0, h2: h0 + pick([100, 140, 200]), sw: 0.5 });
        break;
      case "chryscrackle":
        fire({ count: 150, speed: 320, life: 2.4, drag: 0.978, g: 45, h: h0, h2: h2, trail: 0.22 });
        fire({ count: 90, speed: 170, life: 2.1, drag: 0.974, g: 40, h: 48, h2: 44, sat: 70, glitter: true, size: 1.3 });
        break;
      case "dahlia":
        fire({ count: 150, speed: 300, life: 2.2, drag: 0.975, g: 38, h: h0, h2: h2, shape: "rose" });
        break;
      case "silver":
        fire({ count: 190, speed: 280, life: 4.4, drag: 0.968, g: 80, h: 210, h2: 200, sat: 12, trail: 0.9, glitter: true, size: 1.6 });
        break;
      case "double":
        fire({ count: 140, speed: 290, life: 1.9, drag: 0.975, g: 45, h: h0, h2: h2 });
        delayed.push({
          t: 0.42,
          fn: () => {
            const nx = x + rand(-50, 50);
            const ny = y + rand(-35, 35);
            const nh = h0 + pick([50, 90, 180]);
            flashAt(nx, ny, nh, 0.5);
            playBoom(0.85, ny / H);
            burst(nx, ny, { count: 130, speed: 260, life: 1.8, drag: 0.975, g: 45, h: nh, h2: nh, burstId });
          },
        });
        break;
      default:
        fire({ count: 180, speed: 300, life: 1.9, drag: 0.975, g: 45, h: h0, h2: h2 });
    }
    updateGreeting();
  }

  function spawnRocket() {
    // Exact bottom-center; mild left/right drift to the burst (earlier feel).
    const startX = W * 0.5;
    const targetX = W * 0.5 + rand(-W * 0.1, W * 0.1);
    const targetY = rand(H * 0.18, H * 0.38);
    const climb = rand(0.95, 1.2);
    const vy = -rand(10.5, 13.5) * (H / 900 + 0.55) * climb * 60; // px/s
    const startY = H + 8;
    const g = 45 * S;
    let simVy = vy;
    let simY = startY;
    let tAcc = 0;
    const step = 1 / 60;
    while (simY > targetY && simVy < -40 && tAcc < 3) {
      simY += simVy * step;
      simVy += g * step;
      tAcc += step;
    }
    rockets.push({
      x: startX,
      y: startY,
      vx: tAcc > 0 ? (targetX - startX) / tAcc : 0,
      vy,
      ax: 0,
      g,
      targetY,
      hist: [],
      t: 0,
    });
  }

  function onFuse() {
    playFuseTrail(FUSE_MS / 1000 + rand(0.05, 0.15));
  }

  function step(dt) {
    // Delayed secondary bursts
    for (let i = delayed.length - 1; i >= 0; i--) {
      delayed[i].t -= dt;
      if (delayed[i].t <= 0) {
        const fn = delayed[i].fn;
        delayed.splice(i, 1);
        fn();
      }
    }

    for (let i = rockets.length - 1; i >= 0; i--) {
      const r = rockets[i];
      r.t += dt;
      r.hist.push(r.x, r.y, r.t);
      if (r.hist.length > 90) r.hist.splice(0, 3);
      r.x += r.vx * dt;
      r.y += r.vy * dt;
      r.vy += (r.g || 45 * S) * dt;
      // Climb sparklets — reddish to match LED fuse
      if (sparks.length < maxSparks - 20 && Math.random() < 0.7) {
        for (let k = 0; k < 2; k++) {
          pushSpark({
            x: r.x + rand(-2, 2),
            y: r.y + rand(0, 8),
            vx: rand(-20, 20),
            vy: rand(20, 70),
            age: 0,
            life: rand(0.2, 0.45),
            h: rand(6, 22),
            h2: rand(4, 14),
            size: 1.4,
            drag: 0.94,
            g: 90 * S,
            glitter: false,
            trail: 0,
            dust: true,
            sat: 100,
            split: 0,
          });
        }
      }
      if (r.y <= r.targetY || r.vy >= -50) {
        explode(r.x, r.y);
        rockets.splice(i, 1);
      }
    }

    const born = [];
    for (let e = sparks.length - 1; e >= 0; e--) {
      const s = sparks[e];
      s.age += dt;
      if (s.age >= s.life) {
        const last = sparks.pop();
        if (last && e < sparks.length) sparks[e] = last;
        continue;
      }
      if (s.split > 0 && s.age >= s.split) {
        const base = Math.random() * Math.PI;
        for (let k = 0; k < 4; k++) {
          const a = base + (k * Math.PI) / 2;
          born.push({
            x: s.x, y: s.y,
            vx: 0.3 * s.vx + 110 * Math.cos(a) * S,
            vy: 0.3 * s.vy + 110 * Math.sin(a) * S,
            age: 0, life: 0.9,
            h: s.h, h2: s.h2,
            size: 1.5, drag: 0.965, g: 40 * S,
            glitter: false, trail: 0, dust: false, sat: s.sat, split: 0,
            burstId: s.burstId,
          });
        }
        const last = sparks.pop();
        if (last && e < sparks.length) sparks[e] = last;
        continue;
      }
      if (s.wob) {
        s.vx += Math.sin(20 * s.age + (s.ph || 0)) * s.wob * dt * 60;
        s.vy += Math.cos(17 * s.age + (s.ph || 0)) * s.wob * dt * 60;
      }
      const damp = Math.pow(s.drag, 60 * dt);
      const ox = s.x;
      const oy = s.y;
      s.vx *= damp;
      s.vy = s.vy * damp + s.g * dt;
      s.x += s.vx * dt;
      s.y += s.vy * dt;
      if (s.trail > 0 && sparks.length + born.length < maxSparks) {
        const dist = Math.hypot(s.x - ox, s.y - oy) * s.trail / 5;
        let n = Math.floor(dist);
        if (Math.random() < dist - n) n++;
        for (let t = 0; t < n && t < 4; t++) {
          const u = Math.random();
          born.push({
            x: ox + (s.x - ox) * u,
            y: oy + (s.y - oy) * u,
            vx: 0.04 * s.vx + rand(-7, 7),
            vy: 0.04 * s.vy + rand(-3, 10),
            age: 0,
            life: rand(0.25, 0.55),
            h: s.h, h2: s.h2,
            size: 1, drag: 0.95, g: 26 * S,
            glitter: false, trail: 0, dust: true, sat: s.sat, split: 0,
            burstId: s.burstId,
          });
        }
      }
    }
    for (const p of born) pushSpark(p);
    updateGreeting();
  }

  function draw(dt) {
    const idle =
      !sparks.length && !rockets.length && !cores.length && flash < 0.02;
    if (idle) {
      // Hard clear so trail/smoke never leaves a permanent haze.
      fx.setTransform(dpr, 0, 0, dpr, 0, 0);
      fx.globalCompositeOperation = "source-over";
      fx.clearRect(0, 0, W, H);
    } else {
      // Faster fade than BlastSky's default — trails die cleanly.
      fx.globalCompositeOperation = "destination-out";
      fx.fillStyle = `rgba(0,0,0,${Math.min(0.55, 0.18 + dt * 8)})`;
      fx.fillRect(0, 0, W, H);
      fx.globalCompositeOperation = "lighter";
    }
    if (!idle) fx.globalCompositeOperation = "lighter";

    if (flash > 0.02) {
      const r = 0.7 * Math.min(W, H);
      const g = fx.createRadialGradient(flashX, flashY, 0, flashX, flashY, r);
      g.addColorStop(0, `hsla(${flashHue},90%,65%,${0.16 * flash})`);
      g.addColorStop(1, `hsla(${flashHue},90%,50%,0)`);
      fx.fillStyle = g;
      fx.fillRect(flashX - r, flashY - r, 2 * r, 2 * r);
    }

    // AMOLED: city only when lit. Normal: city already on the bg canvas.
    if (amoledMode) drawLitSkyline();
    if (flash > 0.02) flash *= Math.pow(0.02, dt);

    for (let i = cores.length - 1; i >= 0; i--) {
      const c = cores[i];
      c.age += dt;
      if (c.age > 0.16) {
        cores.splice(i, 1);
        continue;
      }
      const a = (40 + 420 * c.age) * S;
      const life = 1 - c.age / 0.16;
      const g = fx.createRadialGradient(c.x, c.y, 0, c.x, c.y, a);
      g.addColorStop(0, `rgba(255,250,225,${0.9 * life})`);
      g.addColorStop(0.35, `rgba(255,214,140,${0.35 * life})`);
      g.addColorStop(1, "rgba(255,180,90,0)");
      fx.fillStyle = g;
      fx.fillRect(c.x - a, c.y - a, 2 * a, 2 * a);
    }

    // Batch stroke segments by style (BlastSky)
    fx.lineCap = "round";
    const batches = new Map();
    const trailDt = Math.max(0.026, 1.1 * dt);

    for (const e of sparks) {
      const s = e.age / e.life;
      let hue;
      if (e.sw !== undefined) hue = s < e.sw ? e.h : e.h2;
      else hue = s < 0.55 ? e.h : e.h + (e.h2 - e.h) * ((s - 0.55) / 0.45);
      const light = s < 0.1 ? 92 - (s / 0.1) * 32 : 60 - (s - 0.1) * 22;
      const sat = Math.min(e.sat, s < 0.1 ? 35 + 650 * s : 100);
      let alpha = s < 0.6 ? 1 : 1 - (s - 0.6) / 0.4;
      if (e.glitter && s > 0.25) alpha *= Math.random() > 0.5 ? 0.12 : 1;
      if (e.dust) alpha *= 0.55;
      else if (!e.glitter) alpha *= 0.85 + 0.15 * Math.random();
      if (e.blink && Math.sin(e.age * e.blink + (e.ph || 0)) < 0.25) alpha *= 0.04;
      const oa = Math.round(6 * alpha);
      if (oa < 1) continue;
      const size = e.size * (1 - 0.45 * s) * (s < 0.08 ? 1.7 : 1);
      const c = ((6 * Math.round(hue / 6) % 360) + 360) % 360;
      const f = 20 * Math.round(sat / 20);
      const d = Math.max(0, 8 * Math.round(light / 8));
      const p = Math.max(2, Math.round(2 * size));
      const key = (((101 * c + f) * 101 + d) * 8 + oa) * 20 + p;
      let m = batches.get(key);
      if (!m) {
        m = { style: `hsla(${c},${f}%,${d}%,${oa / 6})`, w: p / 2, seg: [] };
        batches.set(key, m);
      }
      m.seg.push(e.x - e.vx * trailDt, e.y - e.vy * trailDt, e.x, e.y);

      if (!e.dust && e.size >= 1.6 && s < 0.6) {
        // Soft glow pass
        let glow = batches.get(-key);
        if (!glow) {
          glow = {
            style: `hsla(${c},${f}%,${Math.min(88, d + 12)}%,${(oa / 6) * 0.2})`,
            w: (p / 2) * 3.4,
            seg: [],
          };
          batches.set(-key, glow);
        }
        glow.seg.push(e.x - e.vx * trailDt * 0.8, e.y - e.vy * trailDt * 0.8, e.x, e.y);
        // Hot core
        let core = batches.get(-(key + 1e11));
        if (!core) {
          core = {
            style: `hsla(${c},${Math.max(0, f - 45)}%,${Math.min(97, d + 30)}%,${Math.min(1, oa / 6)})`,
            w: Math.max(1, p / 2),
            seg: [],
          };
          batches.set(-(key + 1e11), core);
        }
        core.seg.push(e.x - e.vx * trailDt * 0.35, e.y - e.vy * trailDt * 0.35, e.x, e.y);
      }
    }

    for (const t of batches.values()) {
      fx.strokeStyle = t.style;
      fx.lineWidth = t.w;
      fx.beginPath();
      for (let i = 0; i < t.seg.length; i += 4) {
        fx.moveTo(t.seg[i], t.seg[i + 1]);
        fx.lineTo(t.seg[i + 2], t.seg[i + 3]);
      }
      fx.stroke();
    }

    // Rockets — reddish/orange like the WS2812 fuse trail
    for (const rocket of rockets) {
      const hist = rocket.hist;
      const n = hist.length / 3;
      if (n < 2) continue;
      for (let pass = 0; pass < 3; pass++) {
        const h = (pass + 1) / 3;
        const light = 42 + 28 * h;
        fx.strokeStyle = `hsla(14,100%,${light}%,${0.14 + 0.5 * h * h})`;
        fx.lineWidth = 1.4 + 3.4 * h;
        fx.beginPath();
        fx.moveTo(hist[0], hist[1]);
        for (let i = 1; i < n; i++) fx.lineTo(hist[i * 3], hist[i * 3 + 1]);
        fx.lineTo(rocket.x, rocket.y);
        fx.stroke();
      }
      const glow = fx.createRadialGradient(rocket.x, rocket.y, 0, rocket.x, rocket.y, 18);
      glow.addColorStop(0, "rgba(255,210,160,0.95)");
      glow.addColorStop(0.35, "rgba(255,90,30,0.55)");
      glow.addColorStop(1, "rgba(180,20,0,0)");
      fx.fillStyle = glow;
      fx.beginPath();
      fx.arc(rocket.x, rocket.y, 18, 0, Math.PI * 2);
      fx.fill();
    }

    fx.globalCompositeOperation = "source-over";
  }

  let last = performance.now();
  function frame(now) {
    const dt = Math.min(0.033, (now - last) / 1000);
    last = now;
    step(dt);
    draw(dt);
    requestAnimationFrame(frame);
  }

  function connectWs() {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    const ws = new WebSocket(`${proto}://${location.host}/ws`);
    ws.addEventListener("message", (ev) => {
      if (ev.data === "fuse") onFuse();
      else if (ev.data === "blast") spawnRocket();
    });
    ws.addEventListener("close", () => setTimeout(connectWs, 1500));
  }

  async function beginShow() {
    if (started) return;
    started = true;
    setAmoledMode(amoledToggle ? amoledToggle.checked : amoledMode);
    armAudio();
    if (startBtn) {
      startBtn.disabled = true;
      startBtn.textContent = "Loading…";
    }
    await loadBoomSamples();
    if (startEl) startEl.hidden = true;
    arm.hidden = true;
    connectWs();
  }

  window.addEventListener("resize", resize);
  if (amoledToggle) {
    amoledToggle.addEventListener("change", () => {
      setAmoledMode(amoledToggle.checked);
    });
  }
  if (startBtn) {
    startBtn.addEventListener("click", beginShow);
  }
  // Allow tapping the card background / Enter — but not before Start for audio unlock.
  window.addEventListener("keydown", (ev) => {
    if (!started && (ev.key === "Enter" || ev.key === " ")) {
      ev.preventDefault();
      beginShow();
    }
  });

  resize();
  requestAnimationFrame(frame);
})();
