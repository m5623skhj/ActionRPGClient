/* Shared browser/CLI package assembly. Raster decoding is supplied by the host. */
"use strict";
window.DungeonPackage = (() => {
  const M = window.DungeonModel;
  const encoder = new TextEncoder();
  const MAX_JSON = 105 * 1024 * 1024, MAX_ZIP = 128 * 1024 * 1024;
  const fail = message => { throw new Error(message); };

  function ImageBytes(asset) {
    const raw = atob(asset.dataUrl.slice(asset.dataUrl.indexOf(",") + 1));
    if (raw.length > 15 * 1024 * 1024) fail(asset.name + ": 이미지당 15MB 제한 초과");
    return Uint8Array.from(raw, character => character.charCodeAt(0));
  }

  function CheckFiles(files) {
    const names = new Set(); let bytes = 22, compact = 0;
    for (const file of files) {
      if (!/^[A-Za-z0-9_./-]+$/.test(file.name) || file.name.startsWith("/")
        || file.name.split("/").some(part => !part || part === "." || part === "..")
        || names.has(file.name.toLowerCase())) fail("출력 경로 중복/오류: " + file.name);
      names.add(file.name.toLowerCase());
      const data = typeof file.data === "string" ? encoder.encode(file.data) : file.data;
      if (!(data instanceof Uint8Array)) fail("출력 데이터 형식 오류: " + file.name);
      bytes += data.length + 76 + encoder.encode(file.name).length * 2;
      if (file.name === "Dungeon.json" || /^Maps\//.test(file.name)) {
        if (data.length > 4 * 1024 * 1024) fail(file.name + ": JSON 4MB 제한 초과");
        compact += encoder.encode(JSON.stringify(JSON.parse(typeof file.data === "string" ? file.data : new TextDecoder().decode(data)))).length;
      }
    }
    if (compact > 3.5 * 1024 * 1024) fail("던전 JSON 서버 전송 한도 3.5MB 초과");
    if (bytes > MAX_ZIP) fail("ZIP 128MB 제한 초과");
    for (const file of files.filter(file => /^Maps\//.test(file.name))) {
      const room = JSON.parse(file.data);
      if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(room.mapId)) fail("서버 mapId 길이/형식 오류: " + room.mapId);
      for (const image of room.images) if (!names.has(("Assets/" + image.asset).toLowerCase())) fail("출력 이미지 누락: " + image.asset);
    }
    return { jsonBytes: compact, zipBytes: bytes, entryCount: files.length };
  }

  /** Snapshot and validate once; UI and CLI share file naming, SVG and size limits. */
  async function Build(input, host) {
    const text = typeof input === "string" ? input.replace(/^\uFEFF/, "") : JSON.stringify(input);
    if (encoder.encode(text).length > MAX_JSON) fail("작업 JSON 105MB 제한 초과");
    const project = M.parse(text), issues = M.validate(project);
    if (issues.some(issue => issue.severity === "error")) {
      const error = new Error("던전 정적 검사 실패: " + issues.filter(issue => issue.severity === "error").map(issue => issue.message).join("\n"));
      error.issues = issues; throw error;
    }
    const used = M.usedAssets(project);
    for (const asset of project.assets.filter(asset => used.has(asset.asset))) {
      const size = await host.decodeImage(ImageBytes(asset), asset.mime);
      if (size.width !== asset.width || size.height !== asset.height) fail(asset.name + ": 실제 이미지 크기가 선언과 다릅니다.");
    }
    const map = M.minimap(project), files = M.runtimeFiles(project, map);
    const addPng = async (name, svg, width, height) => {
      const data = await host.rasterizeSvg(svg, width, height);
      if (!(data instanceof Uint8Array) || ![137, 80, 78, 71, 13, 10, 26, 10].every((value, index) => data[index] === value)) fail(name + ": PNG 생성 실패");
      const size = await host.decodeImage(data, "image/png");
      if (size.width !== width || size.height !== height) fail(name + ": 출력 PNG 크기 불일치");
      files.push({ name, data });
    };
    await addPng("Minimap.png", M.svg(map), map.width, map.height);
    for (const kind of ["normal", "boss"]) await addPng("Icons/" + kind + ".png", M.iconSvg(kind), 128, 128);
    const kinds = new Set(project.rooms.flatMap(room => [...room.warpZones.filter(zone => !zone.visual.asset).map(zone => zone.visual.kind),
      ...room.objects.filter(item => !item.asset).map(() => "object")]));
    for (const kind of kinds) await addPng("Assets/" + M.builtinAssetPath(project, kind), M.iconSvg(kind), 128, 128);
    await addPng("Assets/Images/Monsters/dummy.png", M.iconSvg("dummy").replace('width="128" height="128"', 'width="64" height="96"'), 64, 96);
    const limits = CheckFiles(files), blob = window.DungeonArchive.create(files);
    if (blob.size > MAX_ZIP) fail("ZIP 128MB 제한 초과");
    return { project, map, files, blob, issues, report: { format: "DungeonPackageReport", version: 1,
      status: "package-generated", dataId: project.dataId, dungeonId: project.dungeonId, schemaVersion: project.schemaVersion,
      manifestVersion: 3, roomVersion: 5, roomCount: project.rooms.length,
      monsterCount: project.rooms.reduce((sum, room) => sum + room.monsters.length, 0), issues, ...limits,
      editorUiUsed: false, sourceMigrated: !!project.wasMigrated, installed: false, gameBuildOrPlayTestPerformed: false, playableVerified: false } };
  }
  return { Build, CheckFiles };
})();
