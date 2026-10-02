/* Data/import contract; no DOM or game-runtime dependency. */
(function () {
  "use strict";
  const MAX_FRAMES = 32768;
  const PLAYER_BINDINGS = [
    ["idle", "PlayerIdle"], ["walk", "PlayerWalk"], ["run", "PlayerRun"],
    ["attackStart", "PlayerShootStart"], ["attackFire", "PlayerShootFire"], ["attackEnd", "PlayerShootEnd"],
    ["jumpStart", "PlayerJumpStart"], ["jumpHold", "PlayerJumpHold"], ["jumpLand", "PlayerJumpLand"],
    ["airAttackStart", "PlayerAirShootStart"], ["airAttackFire", "PlayerAirShootFire"], ["airAttackEnd", "PlayerAirShootEnd"],
    ["hit", "PlayerHit"], ["airHitStart", "PlayerAirHitStart"], ["airHitFall", "PlayerAirHitFall"],
    ["knockdown", "PlayerKnockdown"], ["getUp", "PlayerGetUp"]
  ];
  const LABELS = { idle: "대기", walk: "걷기", run: "달리기", move: "이동", attack: "공격",
    attackStart: "공격 준비", attackFire: "발사", attackEnd: "공격 회수", jumpStart: "점프 준비",
    jumpHold: "공중 유지", jumpLand: "착지", airAttackStart: "공중 공격 준비",
    airAttackFire: "공중 발사", airAttackEnd: "공중 공격 회수", hit: "피격",
    airHitStart: "공중 피격 시작", airHitFall: "피격 추락", airborne: "에어본", knockdown: "쓰러짐",
    getUp: "기상", death: "사망" };
  const own = (object, key) => Object.prototype.hasOwnProperty.call(object, key);
  const object = value => value !== null && typeof value === "object" && !Array.isArray(value);
  const finite = value => typeof value === "number" && Number.isFinite(value);
  const positive = value => finite(value) && value > 0;
  const id = value => typeof value === "string" && /^[A-Za-z][A-Za-z0-9_.-]{0,63}$/.test(value)
    && !["__proto__", "constructor", "prototype"].includes(value);
  const safePath = value => typeof value === "string" && value.length <= 240
    && /^[A-Za-z0-9_. /-]+$/.test(value) && !value.startsWith("/")
    && value.split("/").every(part => part && part !== "." && part !== "..");
  const key = (characterId, motionId, index) => JSON.stringify([characterId, motionId, index]);
  const clone = value => JSON.parse(JSON.stringify(value));
  const assert = (condition, message) => { if (!condition) throw new Error(message); };

  function ValidateAnimations(document) {
    assert(object(document) && document.version === 1 && object(document.characters), "animations.json version 1 / characters 객체가 필요합니다.");
    const characters = Object.entries(document.characters);
    assert(characters.length > 0 && characters.length <= 256, "캐릭터는 1~256개여야 합니다.");
    let count = 0;
    for (const [characterId, character] of characters) {
      assert(id(characterId) && object(character) && object(character.motions), "잘못된 캐릭터: " + characterId);
      if (own(character, "referenceStandingHeight")) assert(positive(character.referenceStandingHeight), characterId + ": 기준 높이 오류");
      const motions = Object.entries(character.motions);
      assert(motions.length > 0 && motions.length <= 128, characterId + ": 모션은 1~128개여야 합니다.");
      for (const [motionId, motion] of motions) {
        const at = characterId + " / " + motionId;
        assert(id(motionId) && object(motion), at + ": 모션 형식 오류");
        assert(safePath(motion.image) && /\.(png|jpe?g|webp)$/i.test(motion.image), at + ": 안전한 래스터 이미지 경로가 필요합니다.");
        assert(Number.isInteger(motion.width) && motion.width > 0 && motion.width <= 8192
          && Number.isInteger(motion.height) && motion.height > 0 && motion.height <= 8192, at + ": 이미지 크기 오류");
        assert(Number.isInteger(motion.frameCount) && motion.frameCount > 0 && motion.frameCount <= 512
          && Array.isArray(motion.frames) && motion.frames.length === motion.frameCount, at + ": 프레임 수 오류");
        assert(positive(motion.fps) && motion.fps <= 240 && typeof motion.loop === "boolean"
          && typeof motion.holdLastFrame === "boolean", at + ": 재생 설정 오류");
        if (own(motion, "scaleToMovement")) assert(positive(motion.scaleToMovement), at + ": 상대 배율 오류");
        if (own(motion, "renderSize")) assert(object(motion.renderSize) && positive(motion.renderSize.width)
          && positive(motion.renderSize.height), at + ": 게임 표시 크기 오류");
        const indexes = new Set();
        for (const frame of motion.frames) {
          assert(object(frame) && Number.isInteger(frame.index) && frame.index >= 0 && frame.index < motion.frameCount
            && !indexes.has(frame.index), at + ": 중복 또는 잘못된 프레임 index");
          indexes.add(frame.index);
          const rect = frame.sourceRect, pivot = frame.pivot;
          assert(object(rect) && finite(rect.x) && finite(rect.y) && positive(rect.width) && positive(rect.height)
            && rect.x >= 0 && rect.y >= 0 && rect.x + rect.width <= motion.width + 0.001
            && rect.y + rect.height <= motion.height + 0.001, at + " / " + frame.index + ": sourceRect 경계 오류");
          assert(object(pivot) && finite(pivot.x) && finite(pivot.y) && pivot.x >= 0 && pivot.y >= 0
            && pivot.x <= rect.width && pivot.y <= rect.height, at + " / " + frame.index + ": pivot 오류");
        }
        count += motion.frameCount;
        assert(count <= MAX_FRAMES, "전체 프레임은 " + MAX_FRAMES + "개 이하로 제한됩니다.");
      }
    }
    return count;
  }

  function Entries(document) {
    const entries = [];
    for (const [characterId, character] of Object.entries(document.characters))
      for (const [motionId, motion] of Object.entries(character.motions))
        for (const frame of [...motion.frames].sort((left, right) => left.index - right.index))
          entries.push({ characterId, motionId, motion, frame, key: key(characterId, motionId, frame.index) });
    return entries;
  }

  function ValidateRect(rect, frame) {
    const bounds = frame.sourceRect;
    return object(rect) && finite(rect.x) && finite(rect.y) && positive(rect.width) && positive(rect.height)
      && rect.x >= 0 && rect.y >= 0 && rect.x + rect.width <= bounds.width + 0.001
      && rect.y + rect.height <= bounds.height + 0.001;
  }

  function Check(document, rectangles, images) {
    const errors = [], missing = [];
    ValidateAnimations(document);
    const keys = new Set(), imagePaths = new Set();
    for (const entry of Entries(document)) {
      keys.add(entry.key);
      if (!own(rectangles, entry.key)) missing.push(entry.characterId + "/" + entry.motionId + "/" + entry.frame.index);
      else if (!ValidateRect(rectangles[entry.key], entry.frame)) errors.push(entry.key + ": 피격 사각형의 수치 또는 경계 오류");
      if (!imagePaths.has(entry.motion.image)) {
        imagePaths.add(entry.motion.image);
        const image = images[entry.motion.image];
        if (!image) errors.push(entry.motion.image + ": 이미지를 불러오세요.");
        else if (image.width !== entry.motion.width || image.height !== entry.motion.height)
          errors.push(entry.motion.image + ": 실제 이미지 크기가 선언과 다릅니다.");
      } else {
        const image = images[entry.motion.image];
        if (image && (image.width !== entry.motion.width || image.height !== entry.motion.height))
          errors.push(entry.motion.image + ": 모션 간 이미지 크기 선언이 다릅니다.");
      }
    }
    for (const rectKey of Object.keys(rectangles)) if (!keys.has(rectKey)) errors.push(rectKey + ": 원본에 없는 프레임 참조");
    return { errors, missing, total: keys.size, assigned: keys.size - missing.length };
  }

  function Export(document, rectangles, images) {
    const result = Check(document, rectangles, images);
    assert(!result.errors.length && !result.missing.length, "검사 오류 또는 미지정 프레임이 있어 게임용 출력을 할 수 없습니다.");
    const characters = Object.create(null);
    for (const [characterId, character] of Object.entries(document.characters)) {
      const motions = Object.create(null);
      characters[characterId] = { motions };
      if (own(character, "referenceStandingHeight")) characters[characterId].referenceStandingHeight = character.referenceStandingHeight;
      for (const [motionId, motion] of Object.entries(character.motions)) {
        const output = { frameCount: motion.frameCount, fps: motion.fps, loop: motion.loop,
          holdLastFrame: motion.holdLastFrame, frames: [] };
        if (motion.renderSize) output.renderSize = clone(motion.renderSize);
        if (own(motion, "scaleToMovement")) output.scaleToMovement = motion.scaleToMovement;
        for (const frame of [...motion.frames].sort((left, right) => left.index - right.index))
          output.frames.push({ index: frame.index, sourceRect: clone(frame.sourceRect), pivot: clone(frame.pivot),
            hurtRect: clone(rectangles[key(characterId, motionId, frame.index)]) });
        motions[motionId] = output;
      }
    }
    return { schemaVersion: 1, format: "CharacterHurtRects", animationVersion: document.version,
      coordinates: { unit: "sourcePixels", origin: "frameTopLeft", positiveX: "right", positiveY: "down",
        facing: "right", frameIndexBase: 0 }, characters };
  }

  function ReadHurtRects(value, document) {
    assert(object(value) && value.schemaVersion === 1 && value.format === "CharacterHurtRects"
      && value.animationVersion === document.version && object(value.characters), "지원하지 않는 피격 영역 파일입니다.");
    const expected = { unit: "sourcePixels", origin: "frameTopLeft", positiveX: "right", positiveY: "down", facing: "right", frameIndexBase: 0 };
    assert(object(value.coordinates) && Object.entries(expected).every(([name, setting]) => value.coordinates[name] === setting), "피격 영역 좌표 규칙이 다릅니다.");
    const rectangles = Object.create(null);
    for (const [characterId, character] of Object.entries(value.characters)) {
      assert(own(document.characters, characterId) && object(character) && object(character.motions), "원본에 없는 캐릭터: " + characterId);
      assert((character.referenceStandingHeight ?? null) === (document.characters[characterId].referenceStandingHeight ?? null), "캐릭터의 기준 높이가 변경되었습니다: " + characterId);
      for (const [motionId, motion] of Object.entries(character.motions)) {
        const source = document.characters[characterId].motions[motionId];
        assert(own(document.characters[characterId].motions, motionId) && object(motion) && Array.isArray(motion.frames)
          && motion.frameCount === source.frameCount && motion.fps === source.fps && motion.loop === source.loop
          && motion.holdLastFrame === source.holdLastFrame
          && JSON.stringify(motion.renderSize || null) === JSON.stringify(source.renderSize || null)
          && (motion.scaleToMovement ?? null) === (source.scaleToMovement ?? null), "모션 정의가 변경되었습니다: " + motionId);
        for (const frame of motion.frames) {
          const original = source.frames.find(item => item.index === frame.index);
          const rectKey = key(characterId, motionId, frame.index);
          assert(original && !own(rectangles, rectKey) && SameGeometry(original, frame)
            && ValidateRect(frame.hurtRect, original), "피격 영역 참조/원본 좌표가 다릅니다: " + rectKey);
          rectangles[rectKey] = clone(frame.hurtRect);
        }
      }
    }
    return rectangles;
  }

  function SameGeometry(left, right) {
    return ["x", "y", "width", "height"].every(name => left.sourceRect?.[name] === right.sourceRect?.[name])
      && ["x", "y"].every(name => left.pivot?.[name] === right.pivot?.[name]);
  }

  function ReadProject(value) {
    assert(object(value) && value.schemaVersion === 1 && value.format === "CharacterEditorProject"
      && object(value.hurtRects) && object(value.images), "캐릭터 편집기 작업 파일 형식이 아닙니다.");
    ValidateAnimations(value.animations);
    const rectangles = Object.create(null), assets = Object.create(null);
    assert(Object.keys(value.hurtRects).length <= MAX_FRAMES, "피격 영역 수 제한 초과");
    const entries = new Map(Entries(value.animations).map(entry => [entry.key, entry]));
    for (const [rectKey, rect] of Object.entries(value.hurtRects)) {
      assert(entries.has(rectKey) && ValidateRect(rect, entries.get(rectKey).frame), "작업 파일의 피격 영역 오류: " + rectKey);
      rectangles[rectKey] = clone(rect);
    }
    let bytes = 0;
    const paths = new Set(Entries(value.animations).map(entry => entry.motion.image));
    for (const [path, data] of Object.entries(value.images)) {
      assert(paths.has(path) && typeof data === "string" && /^data:image\/(png|jpeg|webp);base64,[A-Za-z0-9+/]+=*$/.test(data), "작업 파일 이미지 형식 오류");
      assert(data.length <= 21 * 1024 * 1024, "이미지당 15MB 제한 초과");
      bytes += data.length;
      assert(bytes <= 140 * 1024 * 1024, "작업 파일 이미지 총량 제한 초과");
      assets[path] = data;
    }
    return { animations: clone(value.animations), hurtRects: rectangles, images: assets };
  }

  function ParseIni(text) {
    const result = Object.create(null);
    let section = null;
    for (const raw of text.replace(/^\uFEFF/, "").split(/\r?\n/)) {
      const line = raw.trim();
      if (!line || line.startsWith("#") || line.startsWith(";")) continue;
      const header = /^\[([^\]]+)\]$/.exec(line);
      if (header) {
        assert(!own(result, header[1]), "중복 INI 섹션: " + header[1]);
        section = Object.create(null); result[header[1]] = section;
      } else {
        const equals = line.indexOf("=");
        assert(section && equals > 0, "INI 구문 오류: " + line);
        const name = line.slice(0, equals).trim();
        assert(!own(section, name), "중복 INI 키: " + name);
        section[name] = line.slice(equals + 1).trim();
      }
    }
    return result;
  }

  /** Convert the current PLAYER_ANIMATIONS contract without guessing image-name prefixes. */
  function ConvertPlayer(characters, animations, assets, sizes, playerSource) {
    if (playerSource !== null) {
      const withoutComments = playerSource.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/[^\n]*/g, "");
      const declaration = /PLAYER_ANIMATIONS\s*\{([\s\S]*?)\}/.exec(withoutComments);
      const ids = declaration ? [...declaration[1].matchAll(/"([^"]+)"/g)].map(match => match[1]) : [];
      assert(ids.length === PLAYER_BINDINGS.length && ids.every((value, index) => value === PLAYER_BINDINGS[index][1]),
        "Player.cpp의 PLAYER_ANIMATIONS가 변환기 연결표와 다릅니다. 연결표 검토가 필요합니다.");
    }
    const output = { version: 1, coordinates: "sourceRect in original PNG pixels; pivot relative to sourceRect; frame indexes are zero-based",
      characters: Object.create(null) };
    assert(object(assets.Images), "assets.ini의 Images 섹션이 없습니다.");
    for (const [characterId, definition] of Object.entries(characters)) {
      assert(id(characterId), "캐릭터 ID 오류: " + characterId);
      for (const name of Object.keys(definition))
        assert(name === "idle_animation" || name === "walk_animation", "지원하지 않는 characters.ini 필드: " + name);
      const motions = Object.create(null);
      output.characters[characterId] = { name: characterId, motions };
      for (const [motionId, defaultId] of PLAYER_BINDINGS) {
        const animationId = definition[motionId + "_animation"] || defaultId;
        const animation = animations[animationId];
        assert(object(animation), "애니메이션 섹션 누락: " + animationId);
        const image = assets.Images[animation.image];
        assert(safePath(image) && sizes[image], "이미지 누락 또는 경로 오류: " + image);
        const size = sizes[image];
        const columns = Number(animation.columns), rows = Number(animation.rows), frameCount = Number(animation.frame_count);
        const seconds = Number(animation.frame_seconds), width = Number(animation.render_width), height = Number(animation.render_height);
        const ratio = Number(animation.first_row_ratio ?? 0.5), anchorY = Number(animation.anchor_y);
        const secondAnchorY = Number(animation.second_row_anchor_y ?? animation.anchor_y);
        assert(Number.isInteger(columns) && columns > 0 && Number.isInteger(rows) && rows > 0
          && Number.isInteger(frameCount) && frameCount > 0 && frameCount <= 512 && frameCount <= columns * rows
          && positive(seconds) && positive(width) && positive(height) && ratio > 0 && ratio < 1
          && anchorY > 0 && anchorY <= 1 && secondAnchorY > 0 && secondAnchorY <= 1,
        animationId + ": INI 프레임/기준점 설정 오류");
        const anchorTexts = own(animation, "anchor_xs") ? animation.anchor_xs.split(",") : null;
        const anchors = anchorTexts ? anchorTexts.map(Number) : Array(frameCount).fill(0.5);
        assert((!anchorTexts || anchorTexts.every(value => value.trim() !== "")) && anchors.length === frameCount
          && anchors.every(value => finite(value) && value >= 0 && value <= 1), animationId + ": anchor_xs 오류");
        const loop = ["idle", "walk", "run"].includes(motionId);
        const motion = { animationId, image, width: size.width, height: size.height, frameCount, fps: 1 / seconds,
          loop, holdLastFrame: !loop, renderSize: { width, height }, frames: [] };
        for (let index = 0; index < frameCount; ++index) {
          const column = index % columns, row = Math.floor(index / columns), frameWidth = size.width / columns;
          const top = rows === 2 ? (row === 0 ? 0 : size.height * ratio) : row * size.height / rows;
          const bottom = rows === 2 ? (row === 0 ? size.height * ratio : size.height) : (row + 1) * size.height / rows;
          motion.frames.push({ index, sourceRect: { x: column * frameWidth, y: top, width: frameWidth, height: bottom - top },
            pivot: { x: frameWidth * anchors[index], y: (bottom - top) * (rows === 2 && row === 1 ? secondAnchorY : anchorY) } });
        }
        motions[motionId] = motion;
      }
    }
    ValidateAnimations(output);
    return output;
  }

  window.CharacterEditorModel = { ValidateAnimations, Entries, ValidateRect, Check, Export, ReadHurtRects,
    ReadProject, ParseIni, ConvertPlayer, PLAYER_BINDINGS, LABELS, key, clone, own, safePath };
})();
