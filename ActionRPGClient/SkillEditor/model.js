/* Player-only skill authoring contract. CharacterEditor owns animation/hurtbox validation. */
(function () {
  "use strict";
  const C = window.CharacterEditorModel;
  const clone = C.clone, own = C.own;
  const assert = (ok, message) => { if (!ok) throw new Error(message); };
  const id = value => typeof value === "string" && /^[A-Za-z][A-Za-z0-9_.-]{0,63}$/.test(value)
    && !["__proto__", "prototype", "constructor"].includes(value);
  const number = (value, min, max, label) => assert(typeof value === "number" && Number.isFinite(value)
    && value >= min && value <= max, label + ": 수치 범위 " + min + "~" + max);
  const integer = (value, min, max, label) => { number(value, min, max, label); assert(Number.isInteger(value), label + ": 정수 필요"); };
  const KEYS = ["Left", "Right", "Up", "Down", "Z", "X", "C", "V"];
  const TYPES = ["direct", "projectile", "buff"];
  const COORDINATES = "groundXY-height-facingRight-worldUnits";
  const object = value => value !== null && typeof value === "object" && !Array.isArray(value);
  function Keys(value, names, label) {
    assert(object(value) && Object.keys(value).length === names.length && names.every(name => own(value, name)), label + ": 필드 형식 오류");
  }

  // One cumulative timeline drives preview, gameplay events and emitted visual data.
  function Timeline(motion, limit = 60, minimum = 0.001) {
    integer(motion.frameCount, 1, 512, "프레임 수"); number(motion.fps, 0.001, 240, "FPS");
    const explicit = own(motion, "frameDurationsSeconds");
    assert(!explicit || (Array.isArray(motion.frameDurationsSeconds) && motion.frameDurationsSeconds.length === motion.frameCount),
      "프레임 시간은 프레임 수와 같은 길이의 배열이어야 합니다.");
    const durations = explicit ? clone(motion.frameDurationsSeconds) : Array(motion.frameCount).fill(1 / motion.fps);
    const starts = [0]; let representedTotal = 0;
    for (const seconds of durations) {
      assert(typeof seconds === "number" && Number.isFinite(seconds) && seconds > 0
        && Number.isFinite(Math.fround(seconds)) && Math.fround(seconds) > 0, "프레임 시간은 float로 표현 가능한 양수여야 합니다.");
      const previous = starts[starts.length - 1], next = previous + seconds;
      const representedNext = Math.fround(representedTotal + Math.fround(seconds));
      assert(Number.isFinite(next) && Number.isFinite(representedNext) && representedNext > representedTotal
        && representedNext <= limit && next > previous && next <= limit,
        "누적 프레임 시간은 엄격히 증가하고 모션 시간 한도 이내여야 합니다.");
      starts.push(next); representedTotal = representedNext;
    }
    const total = starts[starts.length - 1];
    assert(total >= minimum, "모션 총시간이 너무 짧습니다.");
    if (own(motion, "durationSeconds")) assert(typeof motion.durationSeconds === "number" && Number.isFinite(motion.durationSeconds)
      && Math.abs(motion.durationSeconds - total) <= 0.0001, "모션 총시간과 프레임 시간 합계가 다릅니다.");
    return { durations, starts, total };
  }
  function FrameAt(timeline, seconds, loop = false) {
    const time = loop ? Math.max(0, seconds) % timeline.total : Math.max(0, seconds);
    let first = 0, last = timeline.durations.length;
    while (first + 1 < last) { const middle = Math.floor((first + last) / 2);
      if (timeline.starts[middle] <= time) first = middle; else last = middle; }
    return first;
  }
  function ValidateAnimationTimes(document) {
    for (const character of Object.values(document.characters))
      for (const motion of Object.values(character.motions)) Timeline(motion, 60, 0);
  }

  // Missing legacy values preserve the original no-hitstop behavior; explicit zero stays zero.
  function HitstopSeconds(execution) {
    const value = own(execution, "hitstopSeconds") ? execution.hitstopSeconds : 0;
    number(value, 0, 3.4028234663852886e38, "역경직 시간 (초)");
    const represented = Math.fround(value);
    assert(Number.isFinite(represented) && (value === 0 || represented > 0), "역경직 시간: float 표현 범위 오류");
    return value;
  }

  function NewProject() {
    return { format: "PlayerSkillEditorProject", schemaVersion: 1, characters: [], animations: null,
      hurtRects: null, skills: [], images: {} };
  }
  function Characters(ini) {
    return Object.keys(ini).filter(name => name !== "Default").map(name => {
      const match = /^Character([1-9][0-9]*)$/.exec(name);
      assert(match, "지원하지 않는 플레이어 ID 연결: " + name);
      const dataId = Number(match[1]); integer(dataId, 1, 1000000, "캐릭터 Data ID");
      return { id: name, dataId };
    });
  }
  function Motion(project, skill, variant) {
    return project.animations?.characters[skill.characterId]?.motions[variant?.motionId];
  }
  function Variant(project, characterId, motionId, type) {
    const motion = project.animations?.characters[characterId]?.motions[motionId];
    const value = { motionId, eventFrame: 0, endFrame: motion ? motion.frameCount - 1 : 0, effect: null };
    if (type === "direct") value.attackRects = {};
    if (type === "projectile") Object.assign(value, { spawn: { x: 24, y: 0, height: 58 }, yawDegrees: 0, pitchDegrees: 0 });
    return value;
  }
  function NewSkill(project, characterId, type, skillId) {
    assert(TYPES.includes(type) && id(skillId), "스킬 유형 또는 ID 오류");
    const motions = Object.keys(project.animations?.characters[characterId]?.motions || {});
    assert(motions.length, "플레이어 모션을 먼저 불러오세요.");
    const motionId = motions.includes("attackFire") ? "attackFire" : motions[0];
    const skill = { id: skillId, name: skillId, characterId, type,
      input: { command: ["Z"], maxStepSeconds: 0.35 }, cooldownSeconds: 1,
      ground: Variant(project, characterId, motionId, type), air: null };
    SetType(skill, type);
    return skill;
  }
  function SetType(skill, type) {
    assert(TYPES.includes(type), "스킬 유형 오류"); skill.type = type;
    skill.execution = type === "direct" ? { damage: 20, depthRadius: 24, hitstopSeconds: 0.05 }
      : type === "projectile" ? { damage: 20, speed: 1000, radius: 4, range: 900, hitstopSeconds: 0 }
      : { target: "self", stat: "damageMultiplier", multiplier: 1.25, durationSeconds: 5, refresh: "replaceDuration" };
    for (const variant of [skill.ground, skill.air].filter(Boolean)) {
      delete variant.attackRects; delete variant.spawn; delete variant.yawDegrees; delete variant.pitchDegrees;
      if (type === "direct") variant.attackRects = {};
      if (type === "projectile") Object.assign(variant, { spawn: { x: 24, y: 0, height: 58 }, yawDegrees: 0, pitchDegrees: 0 });
    }
  }
  function Scale(motion, frame) {
    return motion.renderSize ? motion.renderSize.height / frame.sourceRect.height : motion.scaleToMovement || 1;
  }
  function CheckRect(rect, label) {
    assert(rect && typeof rect === "object", label + ": 사각형 필요");
    number(rect.x, -1000, 1000, label + " X"); number(rect.y, -1000, 1000, label + " 높이");
    number(rect.width, 0.1, 2000, label + " 너비"); number(rect.height, 0.1, 2000, label + " 높이 크기");
  }
  function CheckImage(motion, images, paths) {
    assert(window.DungeonArchive.isSafePath(motion.image), motion.image + ": ZIP 출력 경로 형식 오류");
    const image = images[motion.image];
    assert(image && image.width === motion.width && image.height === motion.height, motion.image + ": 이미지 누락 또는 크기 불일치");
    paths.add(motion.image);
  }
  function EffectMotion(effect, images) {
    assert(effect && C.safePath(effect.image) && /\.png$/i.test(effect.image), "이펙트에는 안전한 PNG 상대 경로가 필요합니다.");
    const image = images[effect.image]; assert(image, effect.image + ": 이펙트 이미지 누락");
    integer(effect.columns, 1, 512, "이펙트 열"); integer(effect.rows, 1, 512, "이펙트 행");
    integer(effect.frameCount, 1, 512, "이펙트 프레임 수");
    assert(effect.frameCount <= effect.columns * effect.rows, "이펙트 시트의 프레임 수 초과");
    number(effect.fps, 1, 240, "이펙트 FPS"); number(effect.scale, 0.01, 20, "이펙트 월드 배율");
    number(effect.pivotX, 0, 1, "이펙트 기준점 X"); number(effect.pivotY, 0, 1, "이펙트 기준점 Y");
    for (const name of ["x", "y", "height"]) number(effect.offset[name], -1000, 1000, "이펙트 위치 " + name);
    assert(typeof effect.loop === "boolean", "이펙트 반복 설정 오류");
    const timing = Timeline(effect, 60, 0);
    const width = image.width / effect.columns, height = image.height / effect.rows;
    return { durationSeconds: timing.total,
      ...(own(effect, "frameDurationsSeconds") ? { frameDurationsSeconds: clone(effect.frameDurationsSeconds) } : {}),
      image: effect.image, width: image.width, height: image.height, frameCount: effect.frameCount,
      fps: effect.fps, loop: effect.loop, holdLastFrame: false, scaleToMovement: effect.scale,
      frames: Array.from({ length: effect.frameCount }, (_, index) => ({ index,
        sourceRect: { x: index % effect.columns * width, y: Math.floor(index / effect.columns) * height, width, height },
        pivot: { x: width * effect.pivotX, y: height * effect.pivotY } })) };
  }
  function Build(project, images) {
    assert(project?.format === "PlayerSkillEditorProject" && project.schemaVersion === 1, "작업 파일 형식 오류");
    C.ValidateAnimations(project.animations); ValidateAnimationTimes(project.animations);
    assert(Array.isArray(project.characters) && project.characters.length > 0 && project.characters.length <= 256, "characters.ini를 불러오세요.");
    const characterIds = new Set(), dataIds = new Set();
    for (const character of project.characters) {
      assert(id(character.id) && !characterIds.has(character.id), "캐릭터 ID 중복/오류");
      integer(character.dataId, 1, 1000000, "캐릭터 Data ID"); assert(!dataIds.has(character.dataId), "캐릭터 Data ID 중복");
      assert(character.id === "Character" + character.dataId, "characters.ini의 플레이어 ID 연결과 다릅니다.");
      characterIds.add(character.id); dataIds.add(character.dataId);
      assert(project.animations.characters[character.id], character.id + ": 플레이어 애니메이션 누락");
    }
    assert(Array.isArray(project.skills) && project.skills.length > 0 && project.skills.length <= 256, "스킬은 1~256개여야 합니다.");
    const skills = [], visuals = [], ids = new Set(), commands = new Map(), paths = new Set();
    for (const skill of project.skills) {
      const label = skill.id;
      assert(id(label) && !ids.has(label), "스킬 ID 중복/오류: " + label); ids.add(label);
      assert(typeof skill.name === "string" && skill.name.trim().length > 0 && skill.name.length <= 80, label + ": 이름 오류");
      assert(characterIds.has(skill.characterId) && TYPES.includes(skill.type), label + ": 플레이어/유형 오류");
      assert(skill.input && Array.isArray(skill.input.command) && skill.input.command.length > 0 && skill.input.command.length <= 16
        && skill.input.command.every(key => KEYS.includes(key)), label + ": 커맨드 오류");
      number(skill.input.maxStepSeconds, 0.01, 1.5, label + " 커맨드 간격");
      number(skill.cooldownSeconds, 0, 86400, label + " 쿨다운");
      const command = JSON.stringify([skill.characterId, skill.input.command]);
      assert(!commands.has(command), label + ": 동일 캐릭터의 중복 커맨드 " + commands.get(command)); commands.set(command, label);
      assert(skill.ground || skill.air, label + ": 지상 또는 공중 모션 필요");
      const execution = clone(skill.execution);
      if (skill.type !== "buff") execution.hitstopSeconds = HitstopSeconds(execution);
      Keys(execution, skill.type === "direct" ? ["damage", "depthRadius", "hitstopSeconds"] : skill.type === "projectile"
        ? ["damage", "speed", "radius", "range", "hitstopSeconds"] : ["target", "stat", "multiplier", "durationSeconds", "refresh"], label);
      if (skill.type === "buff") {
        assert(execution.target === "self" && execution.refresh === "replaceDuration"
          && ["damageMultiplier", "movementMultiplier"].includes(execution.stat), label + ": 지원하지 않는 버프 정책");
        number(execution.multiplier, 0.1, 10, label + " 버프 배율"); number(execution.durationSeconds, 0.05, 3600, label + " 버프 지속시간");
      } else {
        integer(execution.damage, 1, 1000000, label + " 피해량");
        if (skill.type === "direct") number(execution.depthRadius, 0.1, 500, label + " 지면 Y 판정 반경");
        else {
          number(execution.speed, 1, 4000, label + " 총속도"); number(execution.radius, 0.1, 100, label + " 충돌 반경");
          number(execution.range, 1, 10000, label + " 이동거리");
        }
      }
      const output = { id: label, name: skill.name, characterId: skill.characterId, type: skill.type,
        input: clone(skill.input), cooldownSeconds: skill.cooldownSeconds, execution: clone(execution), ground: null, air: null };
      const visual = { id: label, characterId: skill.characterId, ground: null, air: null };
      for (const mode of ["ground", "air"]) {
        const variant = skill[mode]; if (!variant) continue;
        const motion = Motion(project, skill, variant); assert(motion, label + ": 모션 참조 누락");
        if (motion.renderSize) {
          number(motion.renderSize.width, 0.01, 10000, label + " 표시 너비"); number(motion.renderSize.height, 0.01, 10000, label + " 표시 높이");
        } else number(motion.scaleToMovement ?? 1, 0.01, 20, label + " 월드 배율");
        CheckImage(motion, images, paths);
        integer(variant.eventFrame, 0, motion.frameCount - 1, label + " 시작 프레임");
        integer(variant.endFrame, variant.eventFrame, motion.frameCount - 1, label + " 종료 프레임");
        const timing = Timeline(motion);
        const compiled = { motionId: variant.motionId, frameCount: motion.frameCount, fps: motion.fps,
          durationSeconds: timing.total, eventFrame: variant.eventFrame, endFrame: variant.endFrame };
        if (own(motion, "frameDurationsSeconds")) compiled.frameDurationsSeconds = clone(motion.frameDurationsSeconds);
        assert(compiled.durationSeconds <= 60, label + ": 모션은 60초 이하로 제한됩니다.");
        if (skill.type === "direct") {
          compiled.attackRects = [];
          for (let index = variant.eventFrame; index <= variant.endFrame; ++index) {
            const rect = variant.attackRects[index]; CheckRect(rect, label + " / " + mode + " / " + index);
            compiled.attackRects.push({ index, rect: clone(rect) });
          }
        } else if (skill.type === "projectile") {
          for (const axis of ["x", "y", "height"]) number(variant.spawn[axis], -1000, 1000, label + " 발사 위치 " + axis);
          number(variant.yawDegrees, -180, 180, label + " 지면 방향각"); number(variant.pitchDegrees, -90, 90, label + " 위아래 각도");
          Object.assign(compiled, { spawn: clone(variant.spawn), yawDegrees: variant.yawDegrees, pitchDegrees: variant.pitchDegrees });
        }
        output[mode] = compiled;
        const shown = { motion: { ...clone(motion), durationSeconds: timing.total }, effect: null };
        if (variant.effect) {
          const effect = variant.effect, effectMotion = EffectMotion(effect, images);
          integer(effect.eventFrame, 0, motion.frameCount - 1, "이펙트 시작 프레임");
          CheckImage(effectMotion, images, paths);
          shown.effect = { motion: effectMotion, eventFrame: effect.eventFrame, offset: clone(effect.offset), loop: effect.loop };
        }
        visual[mode] = shown;
      }
      skills.push(output); visuals.push(visual);
    }
    return { shared: { format: "PlayerSkills", schemaVersion: 1, coordinates: COORDINATES, characters: clone(project.characters), skills },
      client: { format: "PlayerSkillVisuals", schemaVersion: 1, skills: visuals }, paths: [...paths].sort() };
  }
  function Check(project, images) {
    try { const built = Build(project, images); return { errors: [], skillCount: built.shared.skills.length, imageCount: built.paths.length }; }
    catch (error) { return { errors: [error.message], skillCount: project.skills.length, imageCount: 0 }; }
  }
  function ReadProject(value) {
    assert(value?.format === "PlayerSkillEditorProject" && value.schemaVersion === 1 && Array.isArray(value.characters)
      && value.characters.length <= 256 && Array.isArray(value.skills) && value.skills.length <= 256, "스킬 작업 파일 형식 오류");
    if (value.animations) { C.ValidateAnimations(value.animations); ValidateAnimationTimes(value.animations); }
    const ids = new Set();
    for (const character of value.characters) {
      assert(object(character) && id(character.id) && character.id === "Character" + character.dataId && !ids.has(character.id), "작업 캐릭터 연결 오류");
      integer(character.dataId, 1, 1000000, "캐릭터 Data ID"); ids.add(character.id);
    }
    const skillIds = new Set();
    for (const skill of value.skills) {
      assert(object(skill) && id(skill.id) && !skillIds.has(skill.id) && ids.has(skill.characterId)
        && typeof skill.name === "string" && TYPES.includes(skill.type) && object(skill.input)
        && Array.isArray(skill.input.command) && skill.input.command.length <= 16 && skill.input.command.every(key => typeof key === "string")
        && object(skill.execution), "작업 스킬 형식 오류");
      if (skill.type !== "buff") HitstopSeconds(skill.execution);
      else assert(!own(skill.execution, "hitstopSeconds"), "버프에는 역경직 시간을 지정할 수 없습니다.");
      skillIds.add(skill.id);
      for (const mode of ["ground", "air"]) {
        const v = skill[mode]; if (v === null) continue;
        assert(object(v) && typeof v.motionId === "string", "작업 모드 형식 오류");
        if (skill.type === "direct") assert(object(v.attackRects), "공격 영역 목록 오류");
        if (skill.type === "projectile") assert(object(v.spawn), "발사 위치 오류");
        if (v.effect !== null) {
          assert(object(v.effect) && object(v.effect.offset), "이펙트 연결 오류");
          if (own(v.effect, "frameDurationsSeconds")) Timeline(v.effect, 60, 0);
        }
      }
    }
    assert(value.images && typeof value.images === "object" && !Array.isArray(value.images), "작업 이미지 목록 오류");
    let total = 0;
    for (const [path, data] of Object.entries(value.images)) {
      assert(C.safePath(path) && typeof data === "string" && /^data:image\/(png|jpeg|webp);base64,[A-Za-z0-9+/=]+$/.test(data), "작업 이미지 오류");
      total += data.length; assert(total <= 140 * 1024 * 1024, "작업 이미지 총량 초과");
    }
    const output = clone(value);
    for (const skill of output.skills)
      if (skill.type !== "buff" && !own(skill.execution, "hitstopSeconds")) skill.execution.hitstopSeconds = 0;
    return output;
  }
  function Velocity(variant, facingLeft, speed) {
    const yaw = variant.yawDegrees * Math.PI / 180, pitch = variant.pitchDegrees * Math.PI / 180;
    return { x: (facingLeft ? -1 : 1) * Math.cos(yaw) * Math.cos(pitch) * speed,
      y: Math.sin(yaw) * Math.cos(pitch) * speed, height: Math.sin(pitch) * speed };
  }
  window.PlayerSkillModel = { NewProject, Characters, NewSkill, Variant, SetType, Motion, Scale,
    EffectMotion, Timeline, FrameAt, Build, Check, ReadProject, Velocity, COORDINATES, KEYS, IsId: id, clone };
})();
