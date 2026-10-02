(function () {
  "use strict";
  const M = window.CharacterEditorModel, $ = id => document.getElementById(id);
  const canvas = $("canvas"), context = canvas.getContext("2d");
  let animations = null, rectangles = Object.create(null), images = Object.create(null);
  let characterId = "", motionId = "", frameIndex = 0, dirty = false, busy = false;
  let tool = "select", drag = null, clipboard = null, undo = [], redo = [];
  let playing = false, lastTime = 0, elapsed = 0, transform = null, saveHandle = null;
  const MAX_JSON = 145 * 1024 * 1024, MAX_IMAGE = 15 * 1024 * 1024, MAX_IMAGES = 100 * 1024 * 1024;
  const tell = text => { $("status").textContent = text; };
  const changed = value => { dirty = value; $("dirty").textContent = dirty ? "● 미저장 변경" : ""; };
  const currentMotion = () => animations?.characters[characterId]?.motions[motionId];
  const currentFrame = () => currentMotion()?.frames.find(frame => frame.index === frameIndex);
  const currentKey = () => M.key(characterId, motionId, frameIndex);
  const currentRect = () => rectangles[currentKey()];
  const pause = () => { playing = false; elapsed = 0; $("play").textContent = "▶ 재생"; };
  const discard = () => !dirty || window.confirm("저장하지 않은 변경을 버리고 다른 문서를 열까요?");

  async function Task(action) {
    if (busy) return;
    EndDrag(true);
    pause(); busy = true; document.body.classList.add("busy");
    const inputs = [...document.querySelectorAll("button,input,select")];
    const disabled = inputs.map(input => input.disabled);
    inputs.forEach(input => { input.disabled = true; });
    try { await action(); }
    catch (error) { if (error.name !== "AbortError") tell("오류: " + error.message); }
    finally {
      inputs.forEach((input, index) => { input.disabled = disabled[index]; });
      busy = false; document.body.classList.remove("busy"); Refresh();
    }
  }

  async function ReadJson(file, limit = MAX_JSON) {
    if (file.size > limit) throw new Error("JSON 파일 크기 제한을 초과했습니다.");
    return JSON.parse((await file.text()).replace(/^\uFEFF/, ""));
  }

  function FilePath(file) { return (file.webkitRelativePath || file.name).replace(/\\/g, "/"); }

  function FindFile(files, path) {
    const matches = files.filter(file => FilePath(file) === path || FilePath(file).endsWith("/" + path));
    if (matches.length !== 1) throw new Error(path + ": 파일이 없거나 같은 경로 후보가 여러 개입니다.");
    return matches[0];
  }

  async function Decode(dataUrl) {
    return await new Promise((resolve, reject) => {
      const image = new Image();
      image.onload = () => {
        if (!image.naturalWidth || image.naturalWidth > 8192 || image.naturalHeight > 8192)
          reject(new Error("이미지 한 변은 8192px 이하로 제한됩니다."));
        else resolve({ image, dataUrl, width: image.naturalWidth, height: image.naturalHeight });
      };
      image.onerror = () => reject(new Error("이미지 파일을 해석할 수 없습니다."));
      image.src = dataUrl;
    });
  }

  async function ReadImage(file) {
    if (file.size > MAX_IMAGE) throw new Error(file.name + ": 이미지당 15MB 제한 초과");
    const type = /\.png$/i.test(file.name) ? "image/png" : /\.jpe?g$/i.test(file.name) ? "image/jpeg"
      : /\.webp$/i.test(file.name) ? "image/webp" : null;
    if (!type) throw new Error("PNG, JPEG, WebP 이미지가 필요합니다.");
    const dataUrl = await new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onerror = () => reject(new Error("이미지 파일 읽기 실패"));
      reader.onload = () => resolve(reader.result);
      reader.readAsDataURL(new Blob([file], { type }));
    });
    return await Decode(dataUrl);
  }

  function SetDocument(document, newRectangles, newImages, isDirty) {
    animations = document; rectangles = newRectangles; images = newImages;
    characterId = Object.keys(animations.characters)[0]; motionId = Object.keys(animations.characters[characterId].motions)[0];
    frameIndex = Math.min(...currentMotion().frames.map(frame => frame.index));
    undo = []; redo = []; clipboard = null; saveHandle = null; transform = null;
    $("zoom").value = "1"; $("flip").checked = false;
    $("report").textContent = "원본을 불러왔습니다. 미지정 프레임에 피격 사각형을 그리세요.";
    changed(isDirty); Refresh();
  }

  function AddAnimations(document, loadedImages) {
    if (!animations) { SetDocument(document, Object.create(null), loadedImages, true); return; }
    const combined = M.clone(animations), combinedImages = { ...images };
    for (const [id, character] of Object.entries(document.characters)) {
      if (M.own(combined.characters, id)) throw new Error("이미 불러온 캐릭터 ID입니다: " + id + ". 교체하려면 작업을 저장하고 새 문서에서 여세요.");
      combined.characters[id] = M.clone(character);
    }
    for (const [path, image] of Object.entries(loadedImages)) {
      if (combinedImages[path] && combinedImages[path].dataUrl !== image.dataUrl) throw new Error("서로 다른 이미지가 같은 경로를 사용합니다: " + path);
      combinedImages[path] = image;
    }
    M.ValidateAnimations(combined);
    for (const entry of M.Entries(combined)) {
      const image = combinedImages[entry.motion.image];
      if (image && (image.width !== entry.motion.width || image.height !== entry.motion.height))
        throw new Error(entry.motion.image + ": 기존 이미지와 추가한 모션의 크기 선언이 다릅니다.");
    }
    const encodedBytes = Object.values(combinedImages).reduce((sum, image) => sum + image.dataUrl.length, 0);
    if (encodedBytes > 140 * 1024 * 1024) throw new Error("합친 문서의 이미지 총량 제한을 초과했습니다.");
    animations = combined; images = combinedImages; undo = []; redo = []; saveHandle = null;
    characterId = Object.keys(document.characters)[0]; motionId = Object.keys(document.characters[characterId].motions)[0]; frameIndex = 0;
    changed(true); Refresh();
  }

  function Remember() {
    undo.push(M.clone(rectangles)); if (undo.length > 30) undo.shift(); redo = [];
  }

  function Commit(next) {
    if (JSON.stringify(next) === JSON.stringify(rectangles)) return;
    Remember(); rectangles = next; changed(true); Refresh();
  }

  function SetRect(rect) {
    pause();
    const frame = currentFrame();
    if (!frame) throw new Error("편집할 프레임을 선택하세요.");
    if (!M.ValidateRect(rect, frame)) throw new Error("사각형은 프레임 안에 있어야 하며 너비·높이는 0보다 커야 합니다.");
    const next = M.clone(rectangles); next[currentKey()] = rect; Commit(next);
  }

  function SelectOptions(element, entries, selected) {
    element.replaceChildren(...entries.map(([value, text]) => {
      const option = document.createElement("option"); option.value = value; option.textContent = text; return option;
    }));
    element.value = selected;
  }

  function Refresh() {
    if (!animations) {
      $("character").replaceChildren(); $("motion").replaceChildren(); $("frames").replaceChildren();
      $("progress").textContent = ""; $("frameInfo").textContent = "빈 문서";
    } else {
      SelectOptions($("character"), Object.entries(animations.characters).map(([id, value]) => [id, (value.name || id) + " · " + id]), characterId);
      SelectOptions($("motion"), Object.keys(animations.characters[characterId].motions).map(id => [id, (M.LABELS[id] || id) + " · " + id]), motionId);
      const motion = currentMotion();
      $("frames").replaceChildren(...[...motion.frames].sort((left, right) => left.index - right.index).map(frame => {
        const button = document.createElement("button");
        const assigned = M.own(rectangles, M.key(characterId, motionId, frame.index));
        button.textContent = (assigned ? "● " : "○ ") + frame.index;
        button.dataset.index = String(frame.index);
        button.setAttribute("aria-label", "프레임 " + frame.index + (assigned ? " 영역 지정" : " 미지정"));
        button.classList.toggle("active", frame.index === frameIndex);
        button.onclick = () => { EndDrag(true); pause(); frameIndex = frame.index; Refresh(); };
        return button;
      }));
      let total = 0, assigned = 0;
      for (const character of Object.values(animations.characters)) for (const value of Object.values(character.motions)) total += value.frameCount;
      for (const value of Object.keys(rectangles)) if (M.own(rectangles, value)) assigned++;
      $("progress").textContent = "전체 " + assigned + " / " + total + " 프레임 지정";
    }
    UpdateFrameView();
  }

  function UpdateFrameView() {
    const motion = currentMotion(), frame = currentFrame();
    if (motion && frame) {
      for (const button of $("frames").children) button.classList.toggle("active", Number(button.dataset.index) === frameIndex);
      $("frameInfo").textContent = characterId + " / " + motionId + " / " + frameIndex + " · " + motion.fps.toFixed(2)
        + " fps · " + frame.sourceRect.width.toFixed(1) + " × " + frame.sourceRect.height.toFixed(1) + " px · " + motion.image;
    }
    SyncNumbers(); Draw();
  }

  function SyncNumbers() {
    const rect = currentRect();
    for (const [id, name] of [["rectX", "x"], ["rectY", "y"], ["rectWidth", "width"], ["rectHeight", "height"]])
      $(id).value = rect ? Number(rect[name].toFixed(3)) : "";
  }

  function Draw() {
    const viewport = $("viewport"), width = viewport.clientWidth, height = viewport.clientHeight;
    const dpr = window.devicePixelRatio || 1;
    if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
      canvas.width = Math.round(width * dpr); canvas.height = Math.round(height * dpr);
    }
    context.setTransform(dpr, 0, 0, dpr, 0, 0); context.clearRect(0, 0, width, height);
    const frame = currentFrame(), motion = currentMotion(), image = motion && images[motion.image];
    $("empty").hidden = !!image;
    $("empty").textContent = frame ? "이미지 폴더를 불러오세요." : "애니메이션 데이터를 불러오세요.";
    transform = null;
    if (!frame || !image) return;
    const source = frame.sourceRect;
    const fit = Math.min(Math.max(1, width - 80) / source.width, Math.max(1, height - 80) / source.height);
    const scale = fit * Number($("zoom").value), left = (width - source.width * scale) / 2, top = (height - source.height * scale) / 2;
    transform = { left, top, scale, width: source.width, height: source.height };
    const flip = $("flip").checked;
    context.save(); context.translate(left + (flip ? source.width * scale : 0), top);
    context.scale(flip ? -scale : scale, scale); context.imageSmoothingEnabled = false;
    context.drawImage(image.image, source.x, source.y, source.width, source.height, 0, 0, source.width, source.height);
    context.restore();
    context.strokeStyle = "#8293a3"; context.lineWidth = 1;
    context.strokeRect(left, top, source.width * scale, source.height * scale);
    const rect = currentRect();
    if (!rect) return;
    const x = left + (flip ? source.width - rect.x - rect.width : rect.x) * scale, y = top + rect.y * scale;
    const w = rect.width * scale, h = rect.height * scale;
    context.fillStyle = "rgba(70,230,188,.20)"; context.strokeStyle = "#72ffd1"; context.lineWidth = 2;
    context.fillRect(x, y, w, h); context.strokeRect(x, y, w, h);
    context.fillStyle = "#b7ffe5";
    for (const [cornerX, cornerY] of [[x, y], [x + w, y], [x, y + h], [x + w, y + h]]) context.fillRect(cornerX - 4, cornerY - 4, 8, 8);
  }

  function Point(event, clamp = false) {
    const box = canvas.getBoundingClientRect();
    let x = (event.clientX - box.left - transform.left) / transform.scale;
    let y = (event.clientY - box.top - transform.top) / transform.scale;
    if ($("flip").checked) x = transform.width - x;
    if (clamp) { x = Math.max(0, Math.min(transform.width, x)); y = Math.max(0, Math.min(transform.height, y)); }
    return { x, y };
  }

  function Corner(point, rect) {
    const radius = 9 / transform.scale;
    for (const [x, y, oppositeX, oppositeY] of [
      [rect.x, rect.y, rect.x + rect.width, rect.y + rect.height],
      [rect.x + rect.width, rect.y, rect.x, rect.y + rect.height],
      [rect.x, rect.y + rect.height, rect.x + rect.width, rect.y],
      [rect.x + rect.width, rect.y + rect.height, rect.x, rect.y]])
      if (Math.hypot(point.x - x, point.y - y) <= radius) return { x: oppositeX, y: oppositeY };
    return null;
  }

  canvas.addEventListener("pointerdown", event => {
    if (busy || drag || !transform || event.button !== 0) return;
    pause(); const point = Point(event), rect = currentRect();
    if (point.x < 0 || point.y < 0 || point.x > transform.width || point.y > transform.height) return;
    const corner = rect && tool === "select" ? Corner(point, rect) : null;
    const inside = rect && point.x >= rect.x && point.x <= rect.x + rect.width && point.y >= rect.y && point.y <= rect.y + rect.height;
    if (tool === "select" && !corner && !inside) { tell("R 또는 사각형 그리기를 선택한 뒤 드래그하세요."); return; }
    drag = { key: currentKey(), before: M.clone(rectangles), point, rect: rect && M.clone(rect),
      mode: tool === "draw" ? "draw" : corner ? "resize" : "move", opposite: corner, pointerId: event.pointerId };
    canvas.setPointerCapture(event.pointerId); event.preventDefault();
  });

  canvas.addEventListener("pointermove", event => {
    if (!drag || drag.pointerId !== event.pointerId || !transform) return;
    const point = Point(event, true); let rect;
    if (drag.mode === "move") {
      rect = { ...drag.rect, x: Math.max(0, Math.min(transform.width - drag.rect.width, drag.rect.x + point.x - drag.point.x)),
        y: Math.max(0, Math.min(transform.height - drag.rect.height, drag.rect.y + point.y - drag.point.y)) };
    } else {
      const origin = drag.mode === "resize" ? drag.opposite : drag.point;
      rect = { x: Math.min(origin.x, point.x), y: Math.min(origin.y, point.y),
        width: Math.abs(point.x - origin.x), height: Math.abs(point.y - origin.y) };
    }
    rectangles[drag.key] = rect; SyncNumbers(); Draw();
  });

  function EndDrag(cancel) {
    if (!drag) return;
    const operation = drag; drag = null;
    const result = rectangles[operation.key];
    const next = M.clone(rectangles); rectangles = operation.before;
    if (!cancel && result && M.ValidateRect(result, currentFrame())) Commit(next);
    else Refresh();
    if (canvas.hasPointerCapture(operation.pointerId)) canvas.releasePointerCapture(operation.pointerId);
  }
  canvas.addEventListener("pointerup", () => EndDrag(false));
  canvas.addEventListener("pointercancel", () => EndDrag(true));
  canvas.addEventListener("lostpointercapture", () => EndDrag(true));

  function SetTool(value) {
    EndDrag(true); tool = value;
    $("selectTool").classList.toggle("active", value === "select"); $("drawTool").classList.toggle("active", value === "draw");
    canvas.style.cursor = value === "draw" ? "crosshair" : "default";
  }

  function Step(amount) {
    const motion = currentMotion(); if (!motion) return;
    EndDrag(true); pause(); frameIndex = (frameIndex + amount + motion.frameCount) % motion.frameCount; Refresh();
  }

  function ShowCheck() {
    if (!animations) throw new Error("애니메이션을 먼저 불러오세요.");
    const result = M.Check(animations, rectangles, images);
    $("report").textContent = "지정 " + result.assigned + " / " + result.total + " 프레임\n오류 " + result.errors.length
      + "개 · 미지정 " + result.missing.length + "개\n\n" + [...result.errors, ...result.missing.map(path => "미지정: " + path)].slice(0, 200).join("\n")
      + (result.errors.length + result.missing.length > 200 ? "\n(앞의 200개만 표시)" : "");
    tell(!result.errors.length && !result.missing.length ? "정적 검사 완료. 게임용 출력 가능합니다." : "검사 결과를 확인하세요. 작업 저장은 가능합니다.");
    return result;
  }

  async function SaveJson(value, filename, working = false) {
    const blob = new Blob([JSON.stringify(value, null, 2)], { type: "application/json" });
    if (blob.size > MAX_JSON) throw new Error("저장 JSON 크기 제한을 초과했습니다.");
    if (typeof window.showSaveFilePicker === "function") {
      const handle = working && saveHandle ? saveHandle : await window.showSaveFilePicker({ suggestedName: filename,
        types: [{ description: "JSON", accept: { "application/json": [".json"] } }] });
      const writable = await handle.createWritable();
      try { await writable.write(blob); await writable.close(); }
      catch (error) { try { await writable.abort(); } catch (_) { /* Preserve the write error. */ } throw error; }
      if (working) { saveHandle = handle; changed(false); }
      tell(filename + " 저장 완료");
    } else {
      const url = URL.createObjectURL(blob), link = document.createElement("a");
      link.href = url; link.download = filename; document.body.append(link); link.click(); link.remove();
      window.setTimeout(() => URL.revokeObjectURL(url), 60000);
      tell(filename + " 다운로드 요청. 브라우저에서 완료를 확인하세요.");
    }
  }

  $("animationsOpen").onclick = () => $("animationFile").click();
  $("animationFile").onchange = event => {
    const file = event.target.files[0]; event.target.value = ""; if (!file) return;
    Task(async () => {
      const document = await ReadJson(file, 8 * 1024 * 1024); M.ValidateAnimations(document);
      AddAnimations(document, Object.create(null));
      tell("애니메이션 추가 완료. 참조 이미지를 포함한 폴더를 선택하세요.");
    });
  };
  $("imagesOpen").onclick = () => $("imageFolder").click();
  $("imageFolder").onchange = event => {
    const files = [...event.target.files]; event.target.value = ""; if (!files.length) return;
    Task(async () => {
      if (!animations) throw new Error("애니메이션 JSON을 먼저 불러오세요.");
      const paths = [...new Set(M.Entries(animations).map(entry => entry.motion.image))];
      const needed = paths.filter(path => !images[path]);
      const available = paths.filter(path => files.some(file => FilePath(file) === path || FilePath(file).endsWith("/" + path)));
      if (!available.length) throw new Error("현재 문서가 참조하는 이미지를 찾지 못했습니다.");
      const next = { ...images }; let bytes = 0;
      const requirements = new Map();
      for (const entry of M.Entries(animations)) {
        if (!requirements.has(entry.motion.image)) requirements.set(entry.motion.image, []);
        const sizes = requirements.get(entry.motion.image);
        if (!sizes.some(size => size.width === entry.motion.width && size.height === entry.motion.height))
          sizes.push({ width: entry.motion.width, height: entry.motion.height });
      }
      for (const path of available) {
        const file = FindFile(files, path); bytes += file.size;
        if (bytes > MAX_IMAGES) throw new Error("이미지 총량 제한 초과");
        const loaded = await ReadImage(file);
        if (!requirements.get(path).every(size => size.width === loaded.width && size.height === loaded.height))
          throw new Error(path + ": 선언과 실제 이미지 크기가 다릅니다.");
        next[path] = loaded;
      }
      if (Object.values(next).reduce((sum, image) => sum + image.dataUrl.length, 0) > 140 * 1024 * 1024) throw new Error("이미지 총량 제한 초과");
      images = next; changed(true); tell("이미지 확인 완료 · 남은 이미지 " + needed.filter(path => !images[path]).length + "개");
    });
  };
  $("playerOpen").onclick = () => $("playerFolder").click();
  $("playerFolder").onchange = event => {
    const files = [...event.target.files]; event.target.value = ""; if (!files.length) return;
    Task(async () => {
      const readIni = async path => {
        const file = FindFile(files, path);
        if (file.size > 1024 * 1024) throw new Error("INI 파일은 1MB 이하로 제한됩니다.");
        return M.ParseIni(await file.text());
      };
      const definitions = await readIni("Data/characters.ini"), animationDefinitions = await readIni("Data/animations.ini"), assets = await readIni("Data/assets.ini");
      const paths = new Set();
      if (!assets.Images) throw new Error("assets.ini Images 섹션 누락");
      for (const definition of Object.values(definitions)) for (const [motionId, defaultId] of M.PLAYER_BINDINGS) {
        const animationId = definition[motionId + "_animation"] || defaultId, animation = animationDefinitions[animationId];
        if (!animation) throw new Error("애니메이션 누락: " + animationId);
        const path = assets.Images[animation.image];
        if (!M.safePath(path)) throw new Error("이미지 경로 오류: " + path); paths.add(path);
      }
      const loaded = Object.create(null), sizes = Object.create(null); let bytes = 0;
      for (const path of paths) {
        const file = FindFile(files, path); bytes += file.size;
        if (bytes > MAX_IMAGES) throw new Error("이미지 총량 제한 초과");
        tell("플레이어 이미지 읽기: " + path); loaded[path] = await ReadImage(file); sizes[path] = loaded[path];
      }
      const sourceFiles = files.filter(file => FilePath(file).endsWith("/Game/Player.cpp"));
      if (sourceFiles.length > 1) throw new Error("Player.cpp 후보가 여러 개입니다. 클라이언트 프로젝트 폴더만 선택하세요.");
      if (sourceFiles[0]?.size > 1024 * 1024) throw new Error("Player.cpp 크기 제한 초과");
      const source = sourceFiles[0] ? await sourceFiles[0].text() : null;
      const document = M.ConvertPlayer(definitions, animationDefinitions, assets, sizes, source);
      AddAnimations(document, loaded);
      tell("플레이어 변환 완료 · " + (source === null ? "Assets만 선택: 문서화된 기존 연결표 사용" : "Player.cpp 연결표 일치 확인"));
    });
  };
  $("projectOpen").onclick = () => $("projectFile").click();
  $("projectFile").onchange = event => {
    const file = event.target.files[0]; event.target.value = ""; if (!file) return;
    Task(async () => {
      const project = M.ReadProject(await ReadJson(file)); const loaded = Object.create(null);
      for (const [path, dataUrl] of Object.entries(project.images)) loaded[path] = await Decode(dataUrl);
      const result = M.Check(project.animations, project.hurtRects, loaded);
      if (result.errors.some(error => !error.endsWith("이미지를 불러오세요."))) throw new Error(result.errors.join("\n"));
      if (!discard()) return;
      SetDocument(project.animations, project.hurtRects, loaded, false); tell("작업 파일 불러오기 완료");
    });
  };
  $("hurtOpen").onclick = () => $("hurtFile").click();
  $("hurtFile").onchange = event => {
    const file = event.target.files[0]; event.target.value = ""; if (!file) return;
    Task(async () => {
      if (!animations) throw new Error("애니메이션 JSON을 먼저 불러오세요.");
      const loaded = M.ReadHurtRects(await ReadJson(file, 16 * 1024 * 1024), animations);
      Commit(loaded); tell("피격 영역 불러오기 완료");
    });
  };
  $("save").onclick = () => Task(async () => {
    if (!animations) throw new Error("저장할 애니메이션 문서가 없습니다.");
    await SaveJson({ schemaVersion: 1, format: "CharacterEditorProject", animations,
      hurtRects: rectangles, images: Object.fromEntries(Object.entries(images).map(([path, image]) => [path, image.dataUrl])) }, "Character.character-project.json", true);
  });
  $("animationsSave").onclick = () => Task(async () => {
    if (!animations) throw new Error("애니메이션 데이터가 없습니다.");
    await SaveJson(animations, "animations.json");
  });
  $("export").onclick = () => Task(async () => {
    ShowCheck(); await SaveJson(M.Export(animations, rectangles, images), "Character.hurtrects.json");
  });
  $("check").onclick = () => Task(async () => ShowCheck());
  $("new").onclick = () => {
    if (busy || !discard()) return;
    EndDrag(true); pause(); animations = null; rectangles = Object.create(null); images = Object.create(null);
    characterId = ""; motionId = ""; undo = []; redo = []; clipboard = null; saveHandle = null;
    changed(false); $("report").textContent = "빈 문서입니다."; Refresh(); tell("새 문서");
  };
  $("character").onchange = () => {
    const selected = $("character").value; EndDrag(true); pause(); characterId = selected; motionId = Object.keys(animations.characters[characterId].motions)[0]; frameIndex = 0; Refresh();
  };
  $("motion").onchange = () => { const selected = $("motion").value; EndDrag(true); pause(); motionId = selected; frameIndex = 0; Refresh(); };
  $("selectTool").onclick = () => SetTool("select"); $("drawTool").onclick = () => SetTool("draw");
  $("previous").onclick = () => Step(-1); $("next").onclick = () => Step(1);
  $("play").onclick = () => {
    if (!currentMotion()) return;
    EndDrag(true);
    if (playing) pause(); else {
      if (frameIndex === currentMotion().frameCount - 1) frameIndex = 0;
      playing = true; elapsed = 0; lastTime = performance.now(); $("play").textContent = "❚❚ 일시 정지"; Refresh();
    }
  };
  $("zoom").oninput = () => { EndDrag(true); Draw(); }; $("flip").onchange = () => { EndDrag(true); Draw(); };
  $("fit").onclick = () => { EndDrag(true); $("zoom").value = "1"; Draw(); };
  $("applyNumbers").onclick = () => Task(async () => {
    const ids = ["rectX", "rectY", "rectWidth", "rectHeight"];
    if (ids.some(id => $(id).value.trim() === "")) throw new Error("X, Y, 너비, 높이를 모두 입력하세요.");
    SetRect({ x: Number($("rectX").value), y: Number($("rectY").value), width: Number($("rectWidth").value), height: Number($("rectHeight").value) });
  });
  $("copy").onclick = () => { if (currentRect()) { clipboard = M.clone(currentRect()); tell("사각형 복사 완료"); } };
  $("paste").onclick = () => Task(async () => { if (!clipboard) throw new Error("복사한 사각형이 없습니다."); SetRect(M.clone(clipboard)); });
  $("delete").onclick = () => { pause(); if (currentRect()) { const next = M.clone(rectangles); delete next[currentKey()]; Commit(next); } };
  $("applyMotion").onclick = () => Task(async () => {
    const rect = currentRect(), motion = currentMotion();
    if (!rect || !motion) throw new Error("복사할 사각형이 없습니다.");
    if (!motion.frames.every(frame => M.ValidateRect(rect, frame))) throw new Error("모든 프레임에 들어가는 사각형이어야 합니다. 다른 크기의 프레임은 개별 편집하세요.");
    const next = M.clone(rectangles);
    for (const frame of motion.frames) next[M.key(characterId, motionId, frame.index)] = M.clone(rect);
    Commit(next); tell("현재 모션 전체에 사각형 복사 완료");
  });
  $("undo").onclick = () => {
    pause(); if (!undo.length) return;
    redo.push(M.clone(rectangles)); rectangles = undo.pop(); changed(true); Refresh();
  };
  $("redo").onclick = () => {
    pause(); if (!redo.length) return;
    undo.push(M.clone(rectangles)); rectangles = redo.pop(); changed(true); Refresh();
  };
  window.addEventListener("keydown", event => {
    if (busy || /^(INPUT|SELECT|TEXTAREA)$/.test(event.target.tagName)) return;
    if (event.key === "Escape") { EndDrag(true); pause(); return; }
    if (drag) return;
    const lower = event.key.toLowerCase();
    if (event.ctrlKey && lower === "s") { event.preventDefault(); $("save").click(); }
    else if (event.ctrlKey && lower === "z") { event.preventDefault(); $(event.shiftKey ? "redo" : "undo").click(); }
    else if (event.ctrlKey && lower === "y") { event.preventDefault(); $("redo").click(); }
    else if (event.ctrlKey && lower === "c") { event.preventDefault(); $("copy").click(); }
    else if (event.ctrlKey && lower === "v") { event.preventDefault(); $("paste").click(); }
    else if (event.key === "Delete") $("delete").click();
    else if (event.key === "ArrowLeft") { event.preventDefault(); Step(-1); }
    else if (event.key === "ArrowRight") { event.preventDefault(); Step(1); }
    else if (event.code === "Space") { event.preventDefault(); $("play").click(); }
    else if (lower === "r") SetTool("draw"); else if (lower === "v") SetTool("select"); else if (lower === "f") $("fit").click();
  });
  window.addEventListener("beforeunload", event => { if (dirty || busy || drag) { event.preventDefault(); event.returnValue = ""; } });
  new ResizeObserver(() => { if (drag) EndDrag(true); Draw(); }).observe($("viewport"));
  function Tick(time) {
    if (playing && !busy && !drag) {
      elapsed += Math.min((time - lastTime) / 1000, 0.25);
      const motion = currentMotion(); let advanced = false;
      while (elapsed >= 1 / motion.fps && playing) {
        elapsed -= 1 / motion.fps;
        if (frameIndex + 1 < motion.frameCount) frameIndex++;
        else if (motion.loop) frameIndex = 0;
        else { if (!motion.holdLastFrame) frameIndex = 0; pause(); }
        advanced = true;
      }
      if (advanced) UpdateFrameView();
    }
    lastTime = time; requestAnimationFrame(Tick);
  }
  Refresh(); requestAnimationFrame(Tick);
})();
