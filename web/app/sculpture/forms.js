// Procedural Three.js / TSL sculpture forms shared across the 6 Human Being Audio plugins.

export function registerSculptureForms({ THREE, TSL, defineForm, U, stage, helpers }) {
  const {
    Fn,
    float,
    vec3,
    mix,
    step,
    smoothstep,
    floor,
    fract,
    sin,
    cos,
    abs,
    clamp,
    pow,
    exp,
    dot,
    atan,
    hash,
    mx_noise_float,
    rotate,
    mod,
    instanceIndex,
    positionLocal,
    positionGeometry,
    normalLocal,
    normalView,
    positionViewDirection,
    PI,
    TWO_PI,
  } = TSL;
  const { spectrum, fresnel, seedInt, deform, crystal, filmThickness, glassMaterial, wireMaterial } = helpers;

  /* ---- Monoliths: blocks of cuts. Once per bar the blocks glide to a new re-edit and hold. ---- */
  defineForm("monoliths", ({ group, solid, wireOpacity, accent }) => {
    const SL = 16;
    const inner = 5;
    const outer = 7;
    const cols = inner + outer;
    const count = cols * SL;
    const geo = new THREE.CylinderGeometry(1, 1, 1, 6, 1);
    const cutAt = (col, grp, phrase) => {
      const s = col.mul(97).add(grp.mul(13)).add(phrase.mul(7919)).add(seedInt);
      const cut = step(float(0.84).sub(U.chaos.mul(0.45)), hash(s));
      return {
        cut,
        slide: hash(s.add(1)).sub(0.5).mul(float(0.2).add(U.chaos.mul(0.5))).mul(cut),
        push: hash(s.add(2)).mul(float(0.04).add(U.chaos.mul(0.16))).mul(cut),
      };
    };
    const i = instanceIndex.toFloat();
    const col = floor(i.div(SL));
    const sl = i.sub(col.mul(SL));
    const ring = step(inner - 0.5, col);
    const k = col.sub(ring.mul(inner));
    const a = k.div(mix(float(inner), float(outer), ring)).mul(TWO_PI).add(ring.mul(PI.div(outer)));
    const gs = pow(float(2), floor(mix(float(3.99), float(0), U.slice)));
    const grp = floor(sl.div(gs));
    const phrase = floor(U.bars);
    const glide = smoothstep(0.0, 0.42, fract(U.bars));
    const A0 = cutAt(col, grp, phrase);
    const A1 = cutAt(col, grp, phrase.add(1));
    const cutAmt = mix(A0.cut, A1.cut, glide);
    const openK = mix(float(0.55), float(1.6), U.open);

    const pos = Fn(() => {
      const dir = vec3(cos(a), 0, sin(a));
      const tan = vec3(sin(a).negate(), 0, cos(a));
      const slide = mix(A0.slide, A1.slide, glide).mul(openK);
      const push = mix(A0.push, A1.push, glide).mul(openK).add(U.hit.mul(0.22));
      const p = positionLocal.add(tan.mul(slide)).add(dir.mul(push));
      const ang = U.bars.mul(TWO_PI.div(32)).mul(mix(float(1), float(-0.6), ring));
      return deform(rotate(p, vec3(0, ang, 0)));
    })();

    const { mask, edge } = crystal(solid, vec3(i.mul(1.31)));
    const mat = glassMaterial({ flatShading: true });
    mat.positionNode = pos;
    mat.opacityNode = mask;
    mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(240, 820);
    mat.emissiveNode = accent.mul(cutAmt.mul(0.24).add(edge.mul(3)).add(U.hit.mul(0.8)));
    const mesh = new THREE.InstancedMesh(geo, mat, count);
    const wire = new THREE.InstancedMesh(geo, wireMaterial(accent, wireOpacity), count);
    wire.material.positionNode = pos;
    const m = new THREE.Matrix4();
    const q = new THREE.Quaternion();
    const s = new THREE.Vector3();
    const v = new THREE.Vector3();
    const Y = new THREE.Vector3(0, 1, 0);
    for (let c = 0; c < cols; c++) {
      const outerRing = c >= inner;
      const n = outerRing ? outer : inner;
      const kk = outerRing ? c - inner : c;
      const ang = (kk / n) * Math.PI * 2 + (outerRing ? Math.PI / outer : 0);
      const R = outerRing ? 1.3 : 0.55;
      const H = outerRing ? 1.35 : 2.05;
      const w = outerRing ? 0.15 : 0.2;
      for (let j = 0; j < SL; j++) {
        const h = H / SL;
        v.set(Math.cos(ang) * R, -H / 2 + (j + 0.5) * h, Math.sin(ang) * R);
        q.setFromAxisAngle(Y, -ang);
        s.set(w, h * 0.84, w);
        m.compose(v, q, s);
        mesh.setMatrixAt(c * SL + j, m);
      }
    }
    wire.instanceMatrix = mesh.instanceMatrix;
    group.add(mesh, wire);
    group.rotation.x = 0.22;
  });

  /* ---- Tube: a vowel-rippled tube around a ring that never moves ---- */
  defineForm("tube", ({ group, solid, wireOpacity, accent }) => {
    const amber = vec3(0.96, 0.62, 0.2);
    const FRONT = [1.1, 9.2, 12, 14, 18];
    const BACK = [1.3, 3.2, 9.4, 13, 18];
    const AMP = [0.075, 0.055, 0.04, 0.03, 0.022];
    const ripple = Fn(() => {
      const u = atan(positionGeometry.y, positionGeometry.x);
      let r = float(0);
      for (let j = 0; j < 5; j++) {
        const kf = mix(float(FRONT[j]), float(BACK[j]), U.formant);
        const k0 = floor(kf);
        const w = fract(kf);
        const ph = U.time.mul(0.45 + j * 0.25);
        r = r.add(mix(sin(u.mul(k0).add(ph)), sin(u.mul(k0.add(1)).add(ph)), w).mul(AMP[j]));
      }
      r = r.mul(mix(float(0.55), float(1.7), U.open));
      r = r.add(sin(u.mul(floor(mix(float(16), float(40), U.slice))).sub(U.time.mul(1.2))).mul(0.018).mul(U.slice.add(0.2)));
      r = r.add(mx_noise_float(positionGeometry.mul(2.2).add(vec3(U.seed.mul(9), 0, U.time.mul(0.5)))).mul(U.chaos.mul(0.17)));
      return r.mul(float(1).add(U.hit.mul(0.9)));
    })();
    const pos = deform(positionLocal.add(normalLocal.mul(ripple)));
    const tubeGeo = new THREE.TorusGeometry(0.95, 0.22, 40, 260);
    const { mask, edge } = crystal(solid, vec3(0.0));
    const mat = glassMaterial({ color: 0xf3e9dc, metalness: 0.7, roughness: 0.14 });
    mat.positionNode = pos;
    mat.opacityNode = mask;
    mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(300, 860);
    mat.emissiveNode = amber.mul(smoothstep(0.06, 0.16, ripple).mul(1.2)).add(accent.mul(edge.mul(3)));
    const tube = new THREE.Mesh(tubeGeo, mat);
    const tubeWire = new THREE.Mesh(tubeGeo, wireMaterial(amber, wireOpacity));
    tubeWire.material.positionNode = pos;
    const subGeo = new THREE.TorusGeometry(0.46, 0.03, 12, 160);
    const subMask = crystal(solid, vec3(3.0));
    const subMat = new THREE.MeshStandardNodeMaterial({ color: 0x0a0d12, roughness: 0.4, metalness: 0.2 });
    subMat.emissiveNode = accent.mul(1.8);
    subMat.opacityNode = subMask.mask;
    subMat.alphaTest = 0.5;
    const sub = new THREE.Mesh(subGeo, subMat);
    group.add(tube, tubeWire, sub);
    group.rotation.x = -1.05;
    return (dt, moving) => {
      if (moving) tube.rotation.z = tubeWire.rotation.z += dt * 0.1;
    };
  });

  /* ---- Rings: interlocking Euclidean rings; a soft light travels them, never strobes ---- */
  defineForm("rings", ({ group, solid, wireOpacity, accent }) => {
    const rings = [
      { n: 8, k: 3, r: 0.62, tilt: [0.95, 0.1, 0.2] },
      { n: 12, k: 5, r: 0.98, tilt: [-0.45, 0.65, 0] },
      { n: 16, k: 7, r: 1.34, tilt: [0.25, -0.3, 0.85] },
    ];
    const groups = rings.map(({ n, k, r, tilt }, ri) => {
      const g = new THREE.Group();
      g.rotation.set(...tilt);
      const ringGeo = new THREE.TorusGeometry(r, 0.022, 6, n);
      const { mask, edge } = crystal(solid, vec3(ri * 2.1));
      const ringMat = glassMaterial({ metalness: 0.85, roughness: 0.18, color: 0xdedcf0 });
      ringMat.positionNode = deform(positionLocal);
      ringMat.opacityNode = mask;
      ringMat.alphaTest = 0.5;
      ringMat.emissiveNode = accent.mul(edge.mul(3));
      const wire = new THREE.Mesh(ringGeo, wireMaterial(accent, wireOpacity));
      wire.material.positionNode = deform(positionLocal);
      const nodeGeo = new THREE.IcosahedronGeometry(1, 0);
      const nodeMat = glassMaterial({ flatShading: true, color: 0xffffff });
      const s = instanceIndex.toFloat();
      const kEff = clamp(floor(mix(float(k * 0.5), float(Math.min(n, k * 1.7)), U.slice)), 1, n);
      const onset = step(mod(s.mul(kEff), float(n)), kEff.sub(0.5));
      const head = fract(U.bars.mul(ri % 2 ? -0.5 : 0.5).add(ri * 0.13));
      const dist = abs(fract(s.div(n).sub(head).add(0.5)).sub(0.5)).mul(n);
      const glow = exp(dist.mul(dist).mul(-0.7)).mul(onset);
      nodeMat.positionNode = deform(
        positionLocal.add(normalLocal.mul(onset.mul(mix(float(0.015), float(0.045), U.open)).add(glow.mul(0.03)))),
      );
      nodeMat.emissiveNode = accent.mul(glow.mul(1.4).add(onset.mul(0.25)).add(U.hit.mul(0.8)));
      const nodeMask = crystal(solid, vec3(ri * 5.0));
      nodeMat.opacityNode = nodeMask.mask;
      nodeMat.alphaTest = 0.5;
      const nodes = new THREE.InstancedMesh(nodeGeo, nodeMat, n);
      const m = new THREE.Matrix4();
      for (let j = 0; j < n; j++) {
        const ang = (j / n) * Math.PI * 2;
        m.makeScale(0.055, 0.055, 0.055).setPosition(Math.cos(ang) * r, Math.sin(ang) * r, 0);
        nodes.setMatrixAt(j, m);
      }
      g.add(new THREE.Mesh(ringGeo, ringMat), wire, nodes);
      group.add(g);
      return { g, tilt };
    });
    const core = new THREE.Mesh(new THREE.IcosahedronGeometry(0.2, 1), glassMaterial({ flatShading: true }));
    const coreMask = crystal(solid, vec3(9.0));
    core.material.opacityNode = coreMask.mask;
    core.material.alphaTest = 0.5;
    core.material.emissiveNode = accent.mul(U.swell.mul(0.35).add(U.hit.mul(1.2)));
    group.add(core);
    let spin = 0;
    return (dt, moving) => {
      if (moving) spin += dt;
      const open = 0.6 + stage.macros.open * 0.8;
      groups.forEach(({ g, tilt }, i) => {
        g.rotation.x = tilt[0] * open;
        g.rotation.y = tilt[1] * open + spin * 0.03 * (i + 1);
        g.rotation.z = tilt[2] + spin * (0.12 + i * 0.05) * (i % 2 ? -1 : 1);
      });
    };
  });

  /* ---- Prisms: an octahedron cluster with slow glints and a spectral fan ---- */
  defineForm("prisms", ({ group, solid, wireOpacity, accent }) => {
    const geo = new THREE.OctahedronGeometry(1, 0);
    let seedState = 7;
    const rand = () => (seedState = (seedState * 16807) % 2147483647) / 2147483647;
    const base = [{ p: new THREE.Vector3(0, 0, 0), s: 0.6, r: new THREE.Euler(0.2, 0.4, 0) }];
    for (let i = 0; i < 8; i++) {
      const ang = (i / 8) * Math.PI * 2 + rand() * 0.4;
      const y = (rand() - 0.5) * 1.2;
      const R = 0.75 + rand() * 0.3;
      base.push({
        p: new THREE.Vector3(Math.cos(ang) * R, y, Math.sin(ang) * R),
        s: 0.18 + rand() * 0.2,
        r: new THREE.Euler(rand() * 3, rand() * 3, rand() * 3),
      });
    }
    const { mask, edge } = crystal(solid, vec3(instanceIndex.toFloat().mul(2.7)));
    const mat = new THREE.MeshPhysicalNodeMaterial({
      color: 0x1b1a26,
      metalness: 0,
      roughness: 0.03,
      clearcoat: 1,
      clearcoatRoughness: 0.02,
      iridescence: 1,
      iridescenceIOR: 2.0,
      ior: 2.3,
      specularIntensity: 1,
      flatShading: true,
    });
    const ii = instanceIndex.toFloat();
    const glint = pow(
      sin(U.time.mul(float(0.7).add(hash(ii.add(seedInt)).mul(0.35))).add(ii.mul(2.4)))
        .mul(0.5)
        .add(0.5),
      6,
    );
    mat.positionNode = deform(positionLocal);
    mat.opacityNode = mask;
    mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(200, 900);
    mat.emissiveNode = spectrum(dot(normalView, positionViewDirection).mul(1.6).add(U.formant).add(U.time.mul(0.03)))
      .mul(fresnel().mul(0.7))
      .add(accent.mul(glint.mul(0.9).add(edge.mul(3)).add(U.hit.mul(0.8))));
    const mesh = new THREE.InstancedMesh(geo, mat, base.length);
    const wire = new THREE.InstancedMesh(geo, wireMaterial(accent, wireOpacity), base.length);
    wire.material.positionNode = deform(positionLocal);
    wire.instanceMatrix = mesh.instanceMatrix;

    const RAYS = 11;
    const rayGeo = new THREE.BoxGeometry(1, 1, 1).translate(0, 0, 0.5);
    const rayMat = new THREE.MeshStandardNodeMaterial({ color: 0x000000, transparent: true, depthWrite: false });
    const ri = instanceIndex.toFloat();
    const shimmer = sin(U.time.mul(0.8).add(ri.mul(0.9))).mul(0.2).add(0.8);
    const along = float(1).sub(positionGeometry.z.add(0.5).mul(0.5).add(0.25));
    rayMat.colorNode = vec3(0);
    rayMat.emissiveNode = spectrum(ri.div(RAYS).mul(0.85)).mul(shimmer).mul(1.4);
    rayMat.opacityNode = solid.mul(along).mul(float(0.3).add(U.slice.mul(0.5)));
    rayMat.positionNode = deform(positionLocal);
    const rays = new THREE.InstancedMesh(rayGeo, rayMat, RAYS);
    const rm = new THREE.Matrix4();
    const rq = new THREE.Quaternion();
    const rs = new THREE.Vector3();
    for (let i = 0; i < RAYS; i++) {
      rq.setFromEuler(new THREE.Euler(-0.55 + i * 0.11, 1.15, 0));
      rs.set(0.012, 0.012, 3.2);
      rm.compose(new THREE.Vector3(0.2, 0, 0), rq, rs);
      rays.setMatrixAt(i, rm);
    }
    group.add(mesh, wire, rays);
    const m = new THREE.Matrix4();
    const q = new THREE.Quaternion();
    const sv = new THREE.Vector3();
    const pv = new THREE.Vector3();
    const e = new THREE.Euler();
    let t = 0;
    return (dt, moving) => {
      if (moving) t += dt;
      const spread = 0.85 + stage.macros.open * 0.55 + U.hit.value * 0.3;
      base.forEach((b, i) => {
        pv.copy(b.p).multiplyScalar(spread);
        pv.y += Math.sin(t * 0.7 + i) * 0.04;
        e.set(b.r.x + t * 0.12 * ((i % 3) - 1), b.r.y + t * 0.16, b.r.z);
        q.setFromEuler(e);
        sv.setScalar(b.s);
        m.compose(pv, q, sv);
        mesh.setMatrixAt(i, m);
      });
      mesh.instanceMatrix.needsUpdate = true;
      group.rotation.y += moving ? dt * 0.08 : 0;
    };
  });

  /* ---- Helix: a twisted torus knot around a locked sub-anchor ring (Torus Glide) ---- */
  defineForm("helix", ({ group, solid, wireOpacity, accent }) => {
    const cyan = vec3(0.28, 0.78, 0.96);
    const knotGeo = new THREE.TorusKnotGeometry(0.72, 0.16, 220, 32, 2, 3);
    const pos = deform(positionLocal);
    const { mask, edge } = crystal(solid, vec3(1.5));
    const mat = glassMaterial({ color: 0xe6f2f8, metalness: 0.74, roughness: 0.12 });
    mat.positionNode = pos;
    mat.opacityNode = mask;
    mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(260, 880);
    mat.emissiveNode = cyan.mul(0.22).add(accent.mul(edge.mul(3.2)));
    const knot = new THREE.Mesh(knotGeo, mat);
    const knotWire = new THREE.Mesh(knotGeo, wireMaterial(cyan, wireOpacity));
    knotWire.material.positionNode = pos;
    const anchorGeo = new THREE.TorusGeometry(0.44, 0.028, 12, 160);
    const anchorMask = crystal(solid, vec3(3.2));
    const anchorMat = new THREE.MeshStandardNodeMaterial({ color: 0x0a0d12, roughness: 0.35, metalness: 0.2 });
    anchorMat.emissiveNode = vec3(0.3, 0.88, 0.65).mul(1.7);
    anchorMat.opacityNode = anchorMask.mask;
    anchorMat.alphaTest = 0.5;
    const anchor = new THREE.Mesh(anchorGeo, anchorMat);
    anchor.rotation.x = Math.PI * 0.5;
    group.add(knot, knotWire, anchor);
    return (dt, moving) => {
      if (moving) {
        knot.rotation.y = knotWire.rotation.y += dt * 0.14;
        knot.rotation.x = knotWire.rotation.x += dt * 0.05;
      }
    };
  });

  /* ---- Membrane: tensioned Chladni drumhead with standing-wave Bessel ripples (Strike Body) ---- */
  defineForm("membrane", ({ group, solid, wireOpacity, accent }) => {
    const amber = vec3(1.0, 0.42, 0.16);
    const cyan = vec3(0.11, 0.91, 1.0);
    const headGeo = new THREE.CylinderGeometry(1.18, 1.18, 0.12, 96, 1, false);
    const chladni = Fn(() => {
      const r = TSL.length(positionGeometry.xz);
      const a = atan(positionGeometry.z, positionGeometry.x);
      const kr = mix(float(5.5), float(13.5), U.formant);
      const m = floor(mix(float(2.0), float(6.0), U.slice));
      const wave = cos(r.mul(kr).sub(U.time.mul(1.8))).mul(cos(a.mul(m).add(U.time.mul(0.35))));
      const shock = sin(r.mul(14.0).sub(U.hit.mul(10.0))).mul(U.hit).mul(0.28);
      const rimFade = smoothstep(float(1.18), float(0.15), r);
      return wave.mul(mix(float(0.05), float(0.19), U.open)).add(shock).mul(rimFade);
    })();
    const pos = deform(positionLocal.add(vec3(0, chladni, 0)));
    const { mask, edge } = crystal(solid, vec3(2.4));
    const mat = glassMaterial({ color: 0x1a1e2e, metalness: 0.86, roughness: 0.12 });
    mat.positionNode = pos;
    mat.opacityNode = mask;
    mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(220, 840);
    mat.emissiveNode = amber
      .mul(abs(chladni).mul(4.5))
      .add(accent.mul(edge.mul(3.2)))
      .add(cyan.mul(U.hit.mul(0.9)));
    const head = new THREE.Mesh(headGeo, mat);
    const headWire = new THREE.Mesh(headGeo, wireMaterial(amber, wireOpacity));
    headWire.material.positionNode = pos;
    group.add(head, headWire);

    const ringDefs = [
      { r: 1.24, tube: 0.032, col: amber },
      { r: 0.8, tube: 0.018, col: vec3(1.0, 0.74, 0.18) },
      { r: 0.38, tube: 0.02, col: cyan },
    ];
    for (let idx = 0; idx < ringDefs.length; idx++) {
      const rd = ringDefs[idx];
      const rg = new THREE.TorusGeometry(rd.r, rd.tube, 14, 128);
      const rm = new THREE.MeshStandardNodeMaterial({ color: 0x0a0d14, roughness: 0.3, metalness: 0.3 });
      const rMask = crystal(solid, vec3(4.0 + idx * 1.7));
      rm.emissiveNode = rd.col.mul(float(1.3).add(U.hit.mul(1.2)));
      rm.opacityNode = rMask.mask;
      rm.alphaTest = 0.5;
      const ringMesh = new THREE.Mesh(rg, rm);
      ringMesh.rotation.x = Math.PI * 0.5;
      ringMesh.position.y = 0.08 + idx * 0.03;
      group.add(ringMesh);
    }
    group.rotation.x = 0.52;
    return (dt, moving) => {
      if (moving) group.rotation.y += dt * 0.12;
    };
  });

  /* ---- Strata: stacked obsidian-glass geological slabs with counter-shearing resonance veins (Lineage) ---- */
  defineForm("strata", ({ group, solid, wireOpacity, accent }) => {
    const emerald = vec3(0.08, 0.92, 0.68);
    const amber = vec3(0.98, 0.68, 0.22);
    const layers = 7;
    const slabGeo = new THREE.BoxGeometry(1.9, 0.11, 1.35, 36, 1, 24);
    const idx = instanceIndex.toFloat();
    const normIdx = idx.div(float(layers - 1)).sub(0.5);
    const wave = Fn(() => {
      const k = mix(float(2.4), float(7.0), U.formant);
      const ridge = sin(positionGeometry.x.mul(k).add(U.time.mul(1.1)).add(idx.mul(0.85)))
        .mul(cos(positionGeometry.z.mul(k.mul(0.75)).sub(U.time.mul(0.65))))
        .mul(mix(float(0.03), float(0.14), U.open));
      const shear = sin(U.bars.mul(TWO_PI.div(4)).add(idx.mul(0.9))).mul(normIdx).mul(mix(float(0.08), float(0.42), U.slice));
      const pulse = U.hit.mul(0.22).mul(normIdx);
      return vec3(shear, ridge.add(pulse), cos(U.time.mul(0.7).add(idx)).mul(U.chaos).mul(0.18));
    })();
    const pos = deform(positionLocal.add(wave));
    const { mask, edge } = crystal(solid, vec3(idx.mul(1.7)));
    const mat = glassMaterial({ color: 0x131d22, metalness: 0.84, roughness: 0.14 });
    mat.positionNode = pos;
    mat.opacityNode = mask;
    mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(240, 880);
    const veinMix = smoothstep(float(-0.4), float(0.4), normIdx);
    mat.emissiveNode = mix(emerald, amber, veinMix)
      .mul(abs(wave.y).mul(5.0).add(0.12))
      .add(accent.mul(edge.mul(3.0)))
      .add(emerald.mul(U.hit.mul(0.85)));

    const slabs = new THREE.InstancedMesh(slabGeo, mat, layers);
    const wire = new THREE.InstancedMesh(slabGeo, wireMaterial(emerald, wireOpacity), layers);
    wire.material.positionNode = pos;
    const m = new THREE.Matrix4();
    const q = new THREE.Quaternion();
    const s = new THREE.Vector3();
    const v = new THREE.Vector3();
    for (let j = 0; j < layers; j++) {
      const t = j / (layers - 1) - 0.5;
      const scale = 1.0 - Math.abs(t) * 0.34;
      v.set(0, t * 1.48, 0);
      q.setFromAxisAngle(new THREE.Vector3(0, 1, 0), t * 0.36);
      s.set(scale, 1, scale);
      m.compose(v, q, s);
      slabs.setMatrixAt(j, m);
    }
    wire.instanceMatrix = slabs.instanceMatrix;

    const spineGeo = new THREE.CylinderGeometry(0.035, 0.035, 2.05, 16);
    const spineMat = new THREE.MeshStandardNodeMaterial({ color: 0x091316, roughness: 0.25, metalness: 0.3 });
    const spineMask = crystal(solid, vec3(9.0));
    spineMat.emissiveNode = emerald.mul(float(1.5).add(U.hit.mul(1.2)));
    spineMat.opacityNode = spineMask.mask;
    spineMat.alphaTest = 0.5;
    const spine = new THREE.Mesh(spineGeo, spineMat);

    group.add(slabs, wire, spine);
    group.rotation.x = 0.34;
    return (dt, moving) => {
      if (moving) group.rotation.y += dt * 0.11;
    };
  });

  /* ---- Piston: phase-locked acoustic drive piston & damped zero-crossing shock rings (Datum) ---- */
  defineForm("piston", ({ group, solid, wireOpacity, accent }) => {
    const emerald = vec3(0.24, 0.9, 0.63);
    const cyan = vec3(0.22, 0.91, 1.0);
    const amber = vec3(0.96, 0.65, 0.22);

    const coreGeo = new THREE.CylinderGeometry(0.46, 0.52, 1.32, 48, 20);
    const stroke = Fn(() => {
      const yNorm = positionGeometry.y.mul(1.5);
      const pulse = sin(U.bars.mul(TWO_PI).sub(yNorm.mul(2.2))).mul(0.06).mul(U.open.add(0.35));
      const slam = U.hit.mul(-0.34);
      const radial = sin(yNorm.mul(mix(float(4.0), float(12.0), U.formant)).sub(U.time.mul(2.4)))
        .mul(0.035)
        .mul(U.slice.add(0.25));
      return vec3(normalLocal.x.mul(radial), pulse.add(slam), normalLocal.z.mul(radial));
    })();
    const corePos = deform(positionLocal.add(stroke));
    const coreMask = crystal(solid, vec3(1.1));
    const coreMat = glassMaterial({ color: 0x101820, metalness: 0.88, roughness: 0.11 });
    coreMat.positionNode = corePos;
    coreMat.opacityNode = coreMask.mask;
    coreMat.alphaTest = 0.5;
    coreMat.iridescenceThicknessNode = filmThickness(200, 860);
    coreMat.emissiveNode = emerald
      .mul(float(0.18).add(U.hit.mul(1.3)))
      .add(accent.mul(coreMask.edge.mul(3.2)));
    const core = new THREE.Mesh(coreGeo, coreMat);
    const coreWire = new THREE.Mesh(coreGeo, wireMaterial(emerald, wireOpacity));
    coreWire.material.positionNode = corePos;

    const ringCount = 5;
    const ringGeo = new THREE.TorusGeometry(1.0, 0.026, 14, 128);
    const ri = instanceIndex.toFloat();
    const waveY = sin(U.bars.mul(TWO_PI).sub(ri.mul(0.85)))
      .mul(mix(float(0.06), float(0.24), U.open))
      .add(U.hit.mul(0.28).mul(float(1).sub(ri.div(float(ringCount)))));
    const ringPos = deform(positionLocal.add(vec3(0, waveY, 0)));
    const ringMask = crystal(solid, vec3(ri.mul(2.3)));
    const ringMat = new THREE.MeshStandardNodeMaterial({ color: 0x0a0d12, roughness: 0.28, metalness: 0.3 });
    ringMat.positionNode = ringPos;
    ringMat.opacityNode = ringMask.mask;
    ringMat.alphaTest = 0.5;
    const ringCol = mix(cyan, amber, ri.div(float(ringCount - 1)));
    ringMat.emissiveNode = mix(emerald, ringCol, float(0.55)).mul(float(1.15).add(U.hit.mul(1.2)));
    const rings = new THREE.InstancedMesh(ringGeo, ringMat, ringCount);
    const m = new THREE.Matrix4();
    const q = new THREE.Quaternion().setFromEuler(new THREE.Euler(Math.PI * 0.5, 0, 0));
    const s = new THREE.Vector3();
    const v = new THREE.Vector3();
    for (let j = 0; j < ringCount; j++) {
      const r = 0.68 + j * 0.17;
      v.set(0, -0.42 + j * 0.21, 0);
      s.set(r, r, 1);
      m.compose(v, q, s);
      rings.setMatrixAt(j, m);
    }

    group.add(core, coreWire, rings);
    group.rotation.x = 0.42;
    return (dt, moving) => {
      if (moving) group.rotation.y += dt * 0.13;
    };
  });

  /* ---- Lattice: refracting spectral crystal prism & dispersed harmonic light veins (Prism Vein) ---- */
  defineForm("lattice", ({ group, solid, wireOpacity, accent }) => {
    const cyan = vec3(0.22, 0.91, 1.0);
    const violet = vec3(0.66, 0.34, 0.97);
    const amber = vec3(0.98, 0.72, 0.25);

    const prismGeo = new THREE.CylinderGeometry(0.66, 0.66, 1.52, 3, 18);
    const refract = Fn(() => {
      const y = positionGeometry.y;
      const twist = sin(y.mul(mix(float(2.4), float(7.0), U.formant)).sub(U.bars.mul(TWO_PI)))
        .mul(0.055)
        .mul(U.open.add(0.35));
      const pulse = U.hit.mul(0.14);
      return vec3(normalLocal.x.mul(twist.add(pulse)), sin(U.time.mul(1.4).add(y.mul(2.0))).mul(0.03), normalLocal.z.mul(twist.add(pulse)));
    })();
    const prismPos = deform(positionLocal.add(refract));
    const prismMask = crystal(solid, vec3(1.4));
    const prismMat = glassMaterial({ color: 0x0d1522, metalness: 0.88, roughness: 0.08, flatShading: true });
    prismMat.positionNode = prismPos;
    prismMat.opacityNode = prismMask.mask;
    prismMat.alphaTest = 0.5;
    prismMat.iridescenceThicknessNode = filmThickness(180, 920);
    prismMat.emissiveNode = mix(cyan, violet, smoothstep(float(-0.6), float(0.6), positionGeometry.y))
      .mul(float(0.22).add(U.hit.mul(1.1)))
      .add(accent.mul(prismMask.edge.mul(3.4)));
    const prism = new THREE.Mesh(prismGeo, prismMat);
    const prismWire = new THREE.Mesh(prismGeo, wireMaterial(cyan, wireOpacity));
    prismWire.material.positionNode = prismPos;

    const veinCount = 7;
    const veinGeo = new THREE.BoxGeometry(0.045, 0.045, 1.55);
    const vi = instanceIndex.toFloat();
    const normV = vi.div(float(veinCount - 1));
    const shimmer = sin(U.bars.mul(TWO_PI).mul(float(1.0).add(normV.mul(2.0))).sub(vi.mul(0.7)))
      .mul(mix(float(0.05), float(0.18), U.slice))
      .add(U.hit.mul(0.22));
    const veinPos = deform(positionLocal.add(vec3(0, shimmer, 0)));
    const veinMask = crystal(solid, vec3(vi.mul(2.1)));
    const veinMat = new THREE.MeshStandardNodeMaterial({ color: 0x09101a, roughness: 0.2, metalness: 0.45 });
    veinMat.positionNode = veinPos;
    veinMat.opacityNode = veinMask.mask;
    veinMat.alphaTest = 0.5;
    const lowCol = mix(cyan, violet, clamp(normV.mul(1.6), float(0), float(1)));
    const veinCol = mix(lowCol, amber, smoothstep(float(0.55), float(1.0), normV));
    veinMat.emissiveNode = veinCol.mul(float(1.25).add(U.hit.mul(1.35)));
    const veins = new THREE.InstancedMesh(veinGeo, veinMat, veinCount);
    const m = new THREE.Matrix4();
    const q = new THREE.Quaternion();
    const s = new THREE.Vector3();
    const v = new THREE.Vector3();
    for (let j = 0; j < veinCount; j++) {
      const t = j / (veinCount - 1) - 0.5;
      const fanAngle = t * 0.92;
      const yOff = t * 0.88;
      v.set(Math.sin(fanAngle) * 0.62, yOff, Math.cos(fanAngle) * 0.62);
      q.setFromEuler(new THREE.Euler(t * 0.28, fanAngle, 0));
      s.set(1.0, 1.0, 0.85 + (1.0 - Math.abs(t)) * 0.45);
      m.compose(v, q, s);
      veins.setMatrixAt(j, m);
    }

    group.add(prism, prismWire, veins);
    group.rotation.x = 0.32;
    return (dt, moving) => {
      if (moving) group.rotation.y += dt * 0.14;
    };
  });

  /* ---- Knot: a voice-led trefoil carrying key A to key B, ringed by its four section orbits (Ligature) ---- */
  defineForm("knot", ({ group, solid, wireOpacity, accent }) => {
    const violet = vec3(0.66, 0.52, 1.0);
    const cyan = vec3(0.22, 0.91, 1.0);
    const amber = vec3(0.96, 0.65, 0.22);

    const knotGeo = new THREE.TorusKnotGeometry(0.62, 0.115, 256, 24, 2, 3);
    // Angle around the knot's centre; whole-number multiples keep the wave seamless where atan wraps.
    const along = atan(positionGeometry.y, positionGeometry.x);
    const weave = Fn(() => {
      const lobes = floor(mix(float(3), float(9.99), U.formant));
      const swell = sin(along.mul(lobes).sub(U.bars.mul(TWO_PI))).mul(0.026).mul(U.open.add(0.35));
      const ripple = sin(along.mul(floor(mix(float(12), float(36.99), U.slice))).add(U.time.mul(2.2))).mul(0.008);
      return normalLocal.mul(swell.add(ripple).add(U.hit.mul(0.08)));
    })();
    const knotPos = deform(positionLocal.add(weave));
    const knotMask = crystal(solid, vec3(1.7));
    const knotMat = glassMaterial({ color: 0x120f1e, metalness: 0.88, roughness: 0.1 });
    knotMat.positionNode = knotPos;
    knotMat.opacityNode = knotMask.mask;
    knotMat.alphaTest = 0.5;
    knotMat.iridescenceThicknessNode = filmThickness(220, 900);
    // Key A (cyan) on one side, key B (amber) on the other; a bright bridge band travels the strand through violet.
    const keyCol = mix(cyan, amber, smoothstep(float(-0.8), float(0.8), cos(along)));
    const band = pow(sin(along.mul(3).sub(U.bars.mul(PI))).mul(0.5).add(0.5), float(6));
    knotMat.emissiveNode = mix(violet, keyCol, band)
      .mul(float(0.2).add(band.mul(0.6)).add(U.hit.mul(1.2)))
      .add(accent.mul(knotMask.edge.mul(3.2)));
    const knot = new THREE.Mesh(knotGeo, knotMat);
    const knotWire = new THREE.Mesh(knotGeo, wireMaterial(violet, wireOpacity));
    knotWire.material.positionNode = knotPos;

    const ringCount = 4;
    const ringGeo = new THREE.TorusGeometry(1.0, 0.02, 12, 160);
    const ri = instanceIndex.toFloat();
    const breathe = sin(U.bars.mul(TWO_PI.div(4)).sub(ri.mul(PI.div(2))))
      .mul(mix(float(0.02), float(0.08), U.open))
      .add(U.hit.mul(0.12).mul(float(1).sub(ri.div(float(ringCount)))));
    const ringPos = deform(positionLocal.mul(float(1).add(breathe)));
    const ringMask = crystal(solid, vec3(ri.mul(2.9).add(5.0)));
    const ringMat = new THREE.MeshStandardNodeMaterial({ color: 0x0c0a14, roughness: 0.28, metalness: 0.3 });
    ringMat.positionNode = ringPos;
    ringMat.opacityNode = ringMask.mask;
    ringMat.alphaTest = 0.5;
    const ringT = ri.div(float(ringCount - 1));
    const ringCol = mix(mix(cyan, violet, smoothstep(float(0), float(0.5), ringT)), amber, smoothstep(float(0.5), float(1), ringT));
    ringMat.emissiveNode = ringCol.mul(float(1.15).add(U.hit.mul(1.2)));
    const rings = new THREE.InstancedMesh(ringGeo, ringMat, ringCount);
    const m = new THREE.Matrix4();
    const q = new THREE.Quaternion();
    const s = new THREE.Vector3();
    const v = new THREE.Vector3();
    for (let j = 0; j < ringCount; j++) {
      const r = 1.08 + j * 0.06;
      q.setFromEuler(new THREE.Euler(Math.PI * 0.5 + (j - 1.5) * 0.42, (j * Math.PI) / 4, 0));
      s.set(r, r, 1);
      m.compose(v, q, s);
      rings.setMatrixAt(j, m);
    }

    group.add(knot, knotWire, rings);
    group.rotation.x = 0.36;
    return (dt, moving) => {
      if (moving) group.rotation.y += dt * 0.12;
    };
  });
}
