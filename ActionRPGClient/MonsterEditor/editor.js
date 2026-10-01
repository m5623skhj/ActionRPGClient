(function () {
  "use strict";

  const AI = window.MonsterAI;
  const $ = id => document.getElementById(id);
  const SVG_NS = "http://www.w3.org/2000/svg";
  const NODE_WIDTH = 230;
  const NODE_HEIGHT = 94;
  const state = {
    document: AI.createDocument(), monsterIndex: 0, selection: null, connectFrom: null,
    view: { x: 35, y: 35, zoom: 1 }, pointer: null, drag: null,
    dirty: false, revision: 0, result: null, fileHandle: null, fileName: "",
    ioBusy: false, catalogType: "skill", catalogIndex: 0, fileNote: ""
  };
  const currentMonster = () => state.document.monsters[state.monsterIndex];
  const emptyAI = { initialNodeId: "", nodes: [], edges: [] };
  const currentAI = () => currentMonster()?.ai || emptyAI;
  const plain = value => value === undefined || value === null ? "" : String(value);
  const element = (tag, className, text) => {
    const item = document.createElement(tag);
    if (className) item.className = className;
    if (text !== undefined) item.textContent = plain(text);
    return item;
  };
  const svgElement = (tag, attributes = {}, text) => {
    const item = document.createElementNS(SVG_NS, tag);
    for (const [key, value] of Object.entries(attributes)) item.setAttribute(key, plain(value));
    if (text !== undefined) item.textContent = plain(text);
    return item;
  };
  const commitFocusedControl = () => {
    const active = document.activeElement;
    if (active && active !== document.body && active !== $("graph")) active.blur();
  };
  function invalidate() {
    AI.resetApproval(state.document);
    state.dirty = true;
    ++state.revision;
    state.result = null;
    state.fileNote = "";
    renderStatus();
  }
  function touch(rebuildInspector = false) {
    invalidate();
    renderSidebar();
    renderGraph();
    renderValidation();
    if (rebuildInspector) renderInspector();
  }
  function renderStatus() {
    const approved = state.document.approval.status === "approved";
    $("approval-status").className = "badge " + (approved ? "approved" : "draft");
    $("approval-status").textContent = approved ? "정적 검사 승인됨" : "편집 중";
    $("file-status").textContent = (state.fileName || "새 문서") + (state.dirty ? " · 저장하지 않은 변경" : "") + (state.fileNote ? " · " + state.fileNote : "");
    document.querySelector(".workspace").inert = state.ioBusy;
    $("catalog-dialog").inert = state.ioBusy;
    document.body.classList.toggle("io-busy", state.ioBusy);
    for (const id of ["new-document", "open-document", "save-document", "approve-document"]) $(id).disabled = state.ioBusy;
  }
  function renderSidebar() {
    const list = $("monster-list");
    if (list.children.length !== state.document.monsters.length) list.replaceChildren();
    state.document.monsters.forEach((monster, index) => {
      let item = list.children[index];
      if (!item) {
        item = element("button");
        item.append(element("span"), element("small"));
        item.addEventListener("click", () => {
          commitFocusedControl();
          state.monsterIndex = index;
          state.selection = null;
          state.connectFrom = null;
          renderAll();
          fitGraph();
        });
        list.append(item);
      }
      item.className = index === state.monsterIndex ? "active" : "";
      item.children[0].textContent = plain(monster.name) || "이름 없음";
      item.children[1].textContent = plain(monster.id) || "ID 없음";
    });
    $("skill-count").textContent = state.document.skills.length;
    $("motion-count").textContent = state.document.motions.length;
    const hasMonster = !!currentMonster();
    for (const id of ["duplicate-monster", "delete-monster", "add-node", "connect-nodes", "fit-graph"]) {
      $(id).disabled = !hasMonster;
    }
    $("graph-title").textContent = hasMonster ? currentMonster().name || "몬스터 AI" : "빈 문서";
    $("graph-summary").textContent = hasMonster
      ? currentAI().nodes.length + "개 상태 · " + currentAI().edges.length + "개 전환 · 시작: " + plain(currentAI().initialNodeId)
      : "몬스터를 추가하거나 JSON 파일을 불러오세요.";
  }
  function selectedNode() {
    return state.selection?.type === "node" ? currentAI().nodes[state.selection.index] : null;
  }
  function selectedEdge() {
    return state.selection?.type === "edge" ? currentAI().edges[state.selection.index] : null;
  }
  const coordinate = (value, fallback) => typeof value === "number" && Number.isFinite(value) ? value : fallback;
  const nodePosition = node => ({ x: coordinate(node.position.x, 0), y: coordinate(node.position.y, 0) });
  function edgeGeometry(edge, index) {
    const from = currentAI().nodes.find(node => node.id === edge.from);
    const to = currentAI().nodes.find(node => node.id === edge.to);
    if (!from || !to) return null;
    const a = nodePosition(from), b = nodePosition(to);
    const x1 = a.x + NODE_WIDTH, y1 = a.y + NODE_HEIGHT / 2;
    const x2 = b.x, y2 = b.y + NODE_HEIGHT / 2;
    const siblings = currentAI().edges.filter(item => item.from === edge.from && item.to === edge.to);
    const ordinal = siblings.indexOf(edge);
    if (from === to) {
      const span = 60 + ordinal * 30;
      return { path: "M " + x1 + " " + y1 + " C " + (x1 + span) + " " + (y1 - 170 - ordinal * 25) + ", " + (a.x - span) + " " + (y1 - 170 - ordinal * 25) + ", " + x2 + " " + y2,
        labelX: a.x + NODE_WIDTH / 2, labelY: a.y - 66 - ordinal * 20 };
    }
    const offset = (ordinal - (siblings.length - 1) / 2) * 32;
    const distance = Math.max(70, Math.abs(x2 - x1) * 0.45);
    const bendY = x2 <= x1 ? 95 + (index % 3) * 28 : 0;
    return { path: "M " + x1 + " " + y1 + " C " + (x1 + distance) + " " + (y1 + offset + bendY) + ", " + (x2 - distance) + " " + (y2 + offset + bendY) + ", " + x2 + " " + y2,
      labelX: (x1 + x2) / 2, labelY: (y1 + y2) / 2 + offset * 0.75 + bendY * 0.75 - 10 };
  }
  function renderGraph() {
    const world = $("graph-world");
    world.replaceChildren();
    world.setAttribute("transform", "translate(" + state.view.x + " " + state.view.y + ") scale(" + state.view.zoom + ")");
    const issues = state.result?.issues.filter(item => item.severity === "error" && item.monsterId === currentMonster()?.id) || [];
    currentAI().edges.forEach((edge, index) => {
      const geometry = edgeGeometry(edge, index);
      if (!geometry) return;
      const isSelected = state.selection?.type === "edge" && state.selection.index === index;
      const isInvalid = issues.some(item => item.edgeId === edge.id);
      const group = svgElement("g", { class: "edge" + (isSelected ? " selected" : "") + (isInvalid ? " invalid" : ""), "data-edge-index": index });
      group.append(svgElement("path", { d: geometry.path, class: "hit" }));
      group.append(svgElement("path", { d: geometry.path, class: "line", "marker-end": "url(#arrow)" }));
      group.append(svgElement("text", { x: geometry.labelX, y: geometry.labelY, "text-anchor": "middle" },
        "#" + plain(edge.priority) + " · " + AI.formatCondition(edge.condition)));
      group.append(svgElement("title", {}, plain(edge.id) + " / " + (AI.triggers.find(item => item.id === edge.trigger)?.label || plain(edge.trigger))));
      world.append(group);
    });
    currentAI().nodes.forEach((node, index) => {
      const position = nodePosition(node);
      const isSelected = state.selection?.type === "node" && state.selection.index === index;
      const isInvalid = issues.some(item => item.nodeId === node.id);
      const group = svgElement("g", {
        transform: "translate(" + position.x + " " + position.y + ")",
        class: "node" + (isSelected ? " selected" : "") + (isInvalid ? " invalid" : ""),
        "data-node-index": index
      });
      group.append(svgElement("rect", { x: 0, y: 0, width: NODE_WIDTH, height: NODE_HEIGHT, rx: 10 }));
      group.append(svgElement("text", { x: 16, y: 27, class: "name" }, plain(node.label).slice(0, 18)));
      group.append(svgElement("text", { x: 16, y: 52, class: "action" }, AI.formatAction(node.action, state.document).slice(0, 25)));
      group.append(svgElement("text", { x: 16, y: 77, class: "node-id" }, plain(node.id).slice(0, 24)));
      if (node.id === currentAI().initialNodeId) group.append(svgElement("text", { x: NODE_WIDTH - 15, y: 77, class: "initial", "text-anchor": "end" }, "시작"));
      group.append(svgElement("circle", { cx: 0, cy: NODE_HEIGHT / 2, r: 6, class: "port", "data-port": "in" }));
      group.append(svgElement("circle", { cx: NODE_WIDTH, cy: NODE_HEIGHT / 2, r: 6, class: "port", "data-port": "out" }));
      group.append(svgElement("title", {}, plain(node.label) + " [" + plain(node.id) + "]"));
      world.append(group);
    });
    if (state.connectFrom !== null && currentAI().nodes[state.connectFrom] && state.pointer) {
      const position = nodePosition(currentAI().nodes[state.connectFrom]);
      const x = position.x + NODE_WIDTH, y = position.y + NODE_HEIGHT / 2;
      world.append(svgElement("path", { class: "link-preview", d: "M " + x + " " + y + " L " + state.pointer.x + " " + state.pointer.y }));
    }
    $("graph-hint").textContent = !currentMonster() ? "왼쪽 몬스터 목록의 ＋ 버튼으로 시작하세요." : state.connectFrom !== null
      ? "대상 상태를 클릭하면 연결됩니다. 자기 자신으로 연결할 수도 있습니다. Esc로 취소합니다."
      : "상태·연결을 선택해 속성을 편집하세요. 빈 공간을 드래그하면 화면이 이동합니다.";
    $("graph-hint").className = "graph-hint" + (state.connectFrom !== null ? " connecting" : "");
    $("zoom-label").textContent = Math.round(state.view.zoom * 100) + "%";
  }
  function field(parent, label, type, value, onChange, options = {}) {
    const wrapper = element("label", "field");
    wrapper.append(element("span", "", label));
    const input = element("input");
    input.type = type;
    if (type === "checkbox") input.checked = value === true;
    else input.value = plain(value);
    if (type === "number") {
      input.step = options.integer ? "1" : "any";
      if (options.min !== undefined) input.min = options.min;
      if (options.max !== undefined) input.max = options.max;
    }
    if (options.readOnly) input.readOnly = true;
    input.addEventListener("change", () => {
      let next = input.value;
      if (type === "number") next = input.value.trim() === "" || !Number.isFinite(Number(input.value)) ? null : Number(input.value);
      if (type === "checkbox") next = input.checked;
      onChange(next, input);
    });
    wrapper.append(input);
    parent.append(wrapper);
    return input;
  }
  function selectField(parent, label, value, options, onChange) {
    const wrapper = element("label", "field");
    wrapper.append(element("span", "", label));
    const select = element("select");
    const entries = options.slice();
    if (!entries.some(item => item.id === value)) entries.unshift({ id: plain(value), label: "선택 필요 / 알 수 없는 값: " + plain(value) });
    entries.forEach(item => {
      const option = element("option", "", item.label);
      option.value = item.id;
      select.append(option);
    });
    select.value = plain(value);
    select.addEventListener("change", () => onChange(select.value));
    wrapper.append(select);
    parent.append(wrapper);
  }
  function button(parent, label, handler, className = "") {
    const item = element("button", className, label);
    item.addEventListener("click", () => { commitFocusedControl(); handler(); });
    parent.append(item);
    return item;
  }
  function uniqueId(items, prefix) {
    const used = new Set(items.map(item => item.id));
    let suffix = 1;
    while (used.has(prefix + suffix)) ++suffix;
    return prefix + suffix;
  }
  function tryRename(items, item, value, input, updateReferences) {
    if (!AI.ID_PATTERN.test(value) || items.some(other => other !== item && other.id === value)) {
      alert("ID는 영문자로 시작하는 1~64자이며 같은 목록 안에서 중복될 수 없습니다.");
      input.value = plain(item.id);
      return;
    }
    const previous = item.id;
    item.id = value;
    updateReferences(previous, value);
    touch();
  }
  function parameterFields(parent, parameters, definitions, onChange) {
    definitions.forEach(definition => {
      if (definition.type === "skill" || definition.type === "motion") {
        const entries = definition.type === "skill" ? state.document.skills : state.document.motions;
        selectField(parent, definition.label, parameters[definition.id], entries.map(item => ({ id: item.id, label: item.label + " [" + item.id + "]" })), value => {
          parameters[definition.id] = value;
          onChange();
        });
      } else {
        field(parent, definition.label, definition.type === "boolean" ? "checkbox" : "number", parameters[definition.id], value => {
          parameters[definition.id] = value;
          onChange();
        }, definition);
      }
    });
  }
  function conditionTerms(condition) {
    return 1 + (Array.isArray(condition.children) ? condition.children.reduce((sum, child) => sum + conditionTerms(child), 0) : 0);
  }
  function conditionEditor(parent, condition, replace, remove, depth = 1) {
    const box = element("div", "condition-box");
    const header = element("div", "condition-header");
    const select = element("select");
    const options = AI.conditionDefinitions.slice();
    if (!options.some(item => item.id === condition.type)) options.unshift({ id: plain(condition.type), label: "알 수 없는 조건: " + plain(condition.type) });
    options.forEach(item => {
      const option = element("option", "", item.label);
      option.value = item.id;
      select.append(option);
    });
    select.value = plain(condition.type);
    select.addEventListener("change", () => {
      if (AI.conditionDefinitions.find(item => item.id === select.value)?.group && depth >= AI.MAX_CONDITION_DEPTH) {
        alert("조건 그룹은 최대 깊이 " + AI.MAX_CONDITION_DEPTH + "까지만 추가할 수 있습니다.");
        renderInspector();
        return;
      }
      replace(AI.createCondition(select.value, state.document));
      touch(true);
    });
    header.append(select);
    if (remove) button(header, "×", () => { remove(); touch(true); }, "danger");
    box.append(header);
    const definition = AI.conditionDefinitions.find(item => item.id === condition.type);
    if (definition?.group) {
      const children = element("div", "condition-children");
      condition.children.forEach((child, index) => conditionEditor(children, child,
        next => { condition.children[index] = next; }, () => { condition.children.splice(index, 1); }, depth + 1));
      box.append(children);
      if (condition.type !== "Not") button(box, "＋ 조건", () => {
        if (conditionTerms(selectedEdge().condition) >= AI.MAX_CONDITION_TERMS || depth >= AI.MAX_CONDITION_DEPTH) {
          alert("조건 수 또는 중첩 깊이 한도에 도달했습니다.");
          return;
        }
        condition.children.push(AI.createCondition("Always", state.document));
        touch(true);
      });
    } else if (definition) parameterFields(box, condition.parameters, definition.parameters, () => touch());
    parent.append(box);
  }
  function renderInspector() {
    const content = $("inspector-content");
    content.replaceChildren();
    const monster = currentMonster();
    if (!monster) {
      $("inspector-title").textContent = "문서 속성";
      field(content, "문서 ID", "text", state.document.documentId, value => { state.document.documentId = value; touch(); });
      content.append(element("p", "muted", "몬스터를 추가하면 AI 상태와 전환을 편집할 수 있습니다."));
      return;
    }
    $("inspector-title").textContent = selectedNode() ? "상태 속성" : selectedEdge() ? "전환 속성" : "몬스터 속성";
    field(content, "몬스터 ID", "text", monster.id, (value, input) => tryRename(state.document.monsters, monster, value, input, () => {}));
    field(content, "몬스터 이름", "text", monster.name, value => { monster.name = value; touch(); });
    field(content, "최대 HP", "number", monster.maxHp, value => { monster.maxHp = value; touch(); }, { min: 1 });
    const node = selectedNode(), edge = selectedEdge();
    if (node) {
      content.append(element("h3", "", "상태"));
      field(content, "상태 ID", "text", node.id, (value, input) => tryRename(currentAI().nodes, node, value, input, (previous, next) => {
        if (currentAI().initialNodeId === previous) currentAI().initialNodeId = next;
        currentAI().edges.forEach(item => {
          if (item.from === previous) item.from = next;
          if (item.to === previous) item.to = next;
        });
      }));
      field(content, "표시 이름", "text", node.label, value => { node.label = value; touch(); });
      selectField(content, "행동", node.action.type, AI.actionDefinitions, value => { node.action = AI.createAction(value, state.document); touch(true); });
      const definition = AI.actionDefinitions.find(item => item.id === node.action.type);
      if (definition) parameterFields(content, node.action.parameters, definition.parameters, () => touch());
      const actions = element("div", "actions");
      button(actions, node.id === currentAI().initialNodeId ? "시작 상태" : "시작 상태로 지정", () => {
        currentAI().initialNodeId = node.id;
        touch(true);
      }).disabled = node.id === currentAI().initialNodeId;
      button(actions, "상태 복제", duplicateNode);
      button(actions, "상태 삭제", deleteSelection, "danger");
      content.append(actions);
      content.append(element("h3", "", "나가는 연결"));
      const outgoing = currentAI().edges.map((item, index) => ({ item, index })).filter(entry => entry.item.from === node.id).sort((a, b) => a.item.priority - b.item.priority);
      for (const entry of outgoing) button(content, "#" + plain(entry.item.priority) + " → " + plain(entry.item.to), () => selectItem("edge", entry.index));
      if (!outgoing.length) content.append(element("p", "muted", "오른쪽 포트 또는 연결 버튼으로 다음 상태를 연결하세요."));
    } else if (edge) {
      content.append(element("h3", "", "상태 전환"));
      field(content, "연결 ID", "text", edge.id, (value, input) => tryRename(currentAI().edges, edge, value, input, () => {}));
      const nodes = currentAI().nodes.map(item => ({ id: item.id, label: item.label + " [" + item.id + "]" }));
      selectField(content, "출발 상태", edge.from, nodes, value => { edge.from = value; touch(true); });
      selectField(content, "도착 상태", edge.to, nodes, value => { edge.to = value; touch(true); });
      field(content, "우선순위 (작은 숫자가 먼저)", "number", edge.priority, value => { edge.priority = value; touch(); }, { min: 0, integer: true });
      selectField(content, "전환 시점", edge.trigger, AI.triggers, value => { edge.trigger = value; touch(); });
      content.append(element("p", "muted", "다음 업데이트는 최소 한 틱을 기다립니다. 행동 완료 후는 실제 완료 신호를 기다립니다. 즉시 전환은 같은 업데이트 안에서 이어질 수 있어 순환 검사 대상입니다."));
      content.append(element("h3", "", "전환 조건"));
      conditionEditor(content, edge.condition, value => { edge.condition = value; });
      button(content, "연결 삭제", deleteSelection, "danger");
    } else {
      content.append(element("h3", "", "문서"));
      field(content, "문서 ID", "text", state.document.documentId, value => { state.document.documentId = value; touch(); });
      selectField(content, "시작 상태", currentAI().initialNodeId, currentAI().nodes.map(item => ({ id: item.id, label: item.label + " [" + item.id + "]" })), value => { currentAI().initialNodeId = value; touch(); });
      content.append(element("p", "muted", "순환 그래프를 사용할 수 있습니다. 사망은 공통 캐릭터 처리에서 AI를 종료하므로 죽음 상태를 그래프에 추가하지 않습니다."));
      content.append(element("p", "approval-note", "승인은 정적 검사 통과 기록입니다. 서버가 데이터를 읽을 때도 같은 규칙으로 다시 검사해야 합니다."));
    }
  }
  function renderValidation() {
    const content = $("validation-results");
    content.replaceChildren();
    if (!state.result) {
      $("validation-summary").textContent = state.dirty ? "내용이 변경되었습니다. 다시 검사·승인하세요." : "아직 검사하지 않았습니다.";
      content.append(element("p", "muted", "승인 시 모든 몬스터의 상태·연결·조건과 스킬·동작 참조를 검사합니다. 실행 결과와 외부 애니메이션 파일의 존재는 검사 범위에 포함되지 않습니다."));
      return;
    }
    const result = state.result;
    $("validation-summary").textContent = "오류 " + result.errors + "개 · 경고 " + result.warnings + "개" + (result.errors === 0 ? " · 정적 검사 통과" : " · 승인 불가");
    if (!result.errors) content.append(element("p", "success", "오류가 없습니다. 정상적인 순환 연결은 허용됩니다."));
    result.issues.forEach(issue => {
      const row = element("div", "issue");
      row.append(element("span", issue.severity, issue.severity === "error" ? "오류" : "경고"));
      const location = issue.monsterId ? "[" + issue.monsterId + "] " : issue.recordId ? "[" + issue.recordId + "] " : "";
      button(row, location + issue.message, () => focusIssue(issue));
      content.append(row);
    });
  }
  function focusIssue(issue) {
    if (issue.recordType) {
      openCatalog(issue.recordType);
      const list = state.catalogType === "skill" ? state.document.skills : state.document.motions;
      const index = list.findIndex(item => item.id === issue.recordId);
      if (index >= 0) state.catalogIndex = index;
      renderCatalog();
      return;
    }
    const index = state.document.monsters.findIndex(item => item.id === issue.monsterId);
    if (index >= 0) state.monsterIndex = index;
    if (issue.edgeId) {
      const edgeIndex = currentAI().edges.findIndex(item => item.id === issue.edgeId);
      state.selection = edgeIndex >= 0 ? { type: "edge", index: edgeIndex } : null;
    } else if (issue.nodeId) {
      const nodeIndex = currentAI().nodes.findIndex(item => item.id === issue.nodeId);
      state.selection = nodeIndex >= 0 ? { type: "node", index: nodeIndex } : null;
    } else state.selection = null;
    renderSidebar();
    renderGraph();
    renderInspector();
    fitGraph();
  }
  function renderAll() {
    renderSidebar();
    renderStatus();
    renderGraph();
    renderInspector();
    renderValidation();
  }
  function selectItem(type, index) {
    state.selection = type ? { type, index } : null;
    renderGraph();
    renderInspector();
  }
  function addNode() {
    if (!currentMonster()) return;
    if (currentAI().nodes.length >= AI.MAX_NODES) return alert("노드 수 한도에 도달했습니다.");
    const bounds = $("graph").getBoundingClientRect();
    const id = uniqueId(currentAI().nodes, "State");
    currentAI().nodes.push({
      id, label: "새 상태", position: { x: Math.round((bounds.width / 2 - state.view.x) / state.view.zoom), y: Math.round((bounds.height / 2 - state.view.y) / state.view.zoom) },
      action: AI.createAction("Wait", state.document)
    });
    state.selection = { type: "node", index: currentAI().nodes.length - 1 };
    touch(true);
  }
  function duplicateNode() {
    const node = selectedNode();
    if (!node || currentAI().nodes.length >= AI.MAX_NODES) return;
    const copy = AI.clone(node);
    copy.id = uniqueId(currentAI().nodes, "State");
    copy.label = plain(node.label) + " 복사";
    copy.position.x = coordinate(copy.position.x, 0) + 30;
    copy.position.y = coordinate(copy.position.y, 0) + 120;
    currentAI().nodes.push(copy);
    state.selection = { type: "node", index: currentAI().nodes.length - 1 };
    touch(true);
  }
  function deleteSelection() {
    if (selectedNode()) {
      const node = selectedNode();
      if (currentAI().nodes.length <= 1) return alert("최소 한 개의 상태가 필요합니다.");
      if (!confirm("상태 '" + plain(node.label) + "'와 연결된 전환을 삭제할까요?")) return;
      currentAI().nodes.splice(state.selection.index, 1);
      currentAI().edges = currentAI().edges.filter(edge => edge.from !== node.id && edge.to !== node.id);
      if (currentAI().initialNodeId === node.id) currentAI().initialNodeId = currentAI().nodes[0].id;
    } else if (selectedEdge()) currentAI().edges.splice(state.selection.index, 1);
    else return;
    state.selection = null;
    state.connectFrom = null;
    touch(true);
  }
  function beginConnection(index) {
    state.connectFrom = index;
    state.pointer = null;
    renderGraph();
  }
  function finishConnection(index) {
    const from = currentAI().nodes[state.connectFrom], to = currentAI().nodes[index];
    state.connectFrom = null;
    state.pointer = null;
    if (!from || !to) return renderGraph();
    if (currentAI().edges.length >= AI.MAX_EDGES) return alert("연결 수 한도에 도달했습니다.");
    const priorities = currentAI().edges.filter(edge => edge.from === from.id).map(edge => edge.priority).filter(Number.isSafeInteger);
    const priority = priorities.length ? Math.max(...priorities) + 1 : 0;
    currentAI().edges.push({ id: uniqueId(currentAI().edges, "Transition"), from: from.id, to: to.id, priority, trigger: "OnUpdate", condition: AI.createCondition("Always", state.document) });
    state.selection = { type: "edge", index: currentAI().edges.length - 1 };
    touch(true);
  }
  function worldPoint(event) {
    const bounds = $("graph").getBoundingClientRect();
    return { x: (event.clientX - bounds.left - state.view.x) / state.view.zoom, y: (event.clientY - bounds.top - state.view.y) / state.view.zoom };
  }
  function fitGraph() {
    const nodes = currentAI().nodes;
    if (!nodes.length) return;
    const bounds = $("graph").getBoundingClientRect();
    const left = Math.min(...nodes.map(node => nodePosition(node).x)) - 90;
    const top = Math.min(...nodes.map(node => nodePosition(node).y)) - 140;
    const right = Math.max(...nodes.map(node => nodePosition(node).x + NODE_WIDTH)) + 90;
    const bottom = Math.max(...nodes.map(node => nodePosition(node).y + NODE_HEIGHT)) + 150;
    state.view.zoom = Math.min(1.35, Math.max(0.12, Math.min(bounds.width / (right - left), bounds.height / (bottom - top))));
    state.view.x = (bounds.width - (right - left) * state.view.zoom) / 2 - left * state.view.zoom;
    state.view.y = (bounds.height - (bottom - top) * state.view.zoom) / 2 - top * state.view.zoom;
    renderGraph();
  }
  function walkConditions(condition, handler) {
    handler(condition);
    if (Array.isArray(condition.children)) condition.children.forEach(child => walkConditions(child, handler));
  }
  function renameCatalogReferences(type, previous, next) {
    for (const monster of state.document.monsters) {
      for (const node of monster.ai.nodes) {
        if (type === "skill" && node.action.type === "UseSkill" && node.action.parameters.skillId === previous) node.action.parameters.skillId = next;
        if (type === "motion" && node.action.type === "PlayMotion" && node.action.parameters.motionId === previous) node.action.parameters.motionId = next;
      }
      monster.ai.edges.forEach(edge => walkConditions(edge.condition, condition => {
        if (type === "skill" && condition.type === "SkillReady" && condition.parameters.skillId === previous) condition.parameters.skillId = next;
      }));
    }
  }
  function openCatalog(type) {
    commitFocusedControl();
    state.catalogType = type;
    state.catalogIndex = 0;
    renderCatalog();
    if (!$("catalog-dialog").open) $("catalog-dialog").showModal();
  }
  function renderCatalog() {
    const isSkill = state.catalogType === "skill";
    const list = isSkill ? state.document.skills : state.document.motions;
    $("catalog-title").textContent = isSkill ? "스킬 정의" : "동작 정의";
    $("catalog-list").replaceChildren();
    list.forEach((entry, index) => {
      button($("catalog-list"), entry.label + " [" + entry.id + "]", () => { state.catalogIndex = index; renderCatalog(); }, index === state.catalogIndex ? "active" : "");
    });
    const content = $("catalog-fields");
    content.replaceChildren();
    const entry = list[state.catalogIndex];
    if (!entry) { content.append(element("p", "empty", "정의를 추가하세요.")); return; }
    field(content, "ID", "text", entry.id, (value, input) => {
      tryRename(list, entry, value, input, (previous, next) => renameCatalogReferences(state.catalogType, previous, next));
      refreshCatalogLabels();
    });
    field(content, "이름", "text", entry.label, value => { entry.label = value; touch(); refreshCatalogLabels(); });
    field(content, "애니메이션 ID", "text", entry.animationId, value => { entry.animationId = value; touch(); });
    field(content, "동작 시간 (초)", "number", entry.durationSeconds, value => { entry.durationSeconds = value; touch(); }, { min: 0.001 });
    if (isSkill) {
      field(content, "쿨다운 (초)", "number", entry.cooldownSeconds, value => { entry.cooldownSeconds = value; touch(); }, { min: 0 });
      field(content, "최소 거리", "number", entry.minRange, value => { entry.minRange = value; touch(); }, { min: 0 });
      field(content, "최대 거리", "number", entry.maxRange, value => { entry.maxRange = value; touch(); }, { min: 0 });
      selectField(content, "피격 유형", entry.hitType, [{ id: "Normal", label: "일반 경직" }, { id: "Airborne", label: "에어본" }], value => { entry.hitType = value; touch(); });
    } else field(content, "반복 재생", "checkbox", entry.loop, value => { entry.loop = value; touch(); });
    content.append(element("p", "muted", "외부 이미지 파일과 실제 스킬 효과는 이 도구가 생성하지 않습니다. 게임에 동일한 정의를 연결해야 합니다."));
    button(content, "정의 삭제", () => {
      if (!confirm("정의를 삭제할까요? 사용하는 그래프의 참조 오류는 정적 검사에서 표시됩니다.")) return;
      list.splice(state.catalogIndex, 1);
      state.catalogIndex = Math.max(0, state.catalogIndex - 1);
      touch(true);
      renderCatalog();
    }, "danger");
  }
  function refreshCatalogLabels() {
    const list = state.catalogType === "skill" ? state.document.skills : state.document.motions;
    Array.from($("catalog-list").children).forEach((item, index) => {
      if (list[index]) item.textContent = plain(list[index].label) + " [" + plain(list[index].id) + "]";
    });
  }
  function runValidation() {
    commitFocusedControl();
    state.result = AI.validateDocument(state.document);
    renderValidation();
    renderGraph();
    return state.result;
  }
  const fileTypes = [{ description: "몬스터 AI JSON", accept: { "application/json": [".json"] } }];
  function fallbackDownload(snapshot, name) {
    const blob = new Blob([JSON.stringify(snapshot, null, 2) + "\n"], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const link = element("a");
    link.href = url;
    link.download = name;
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  }
  async function saveDocument(approve) {
    if (state.ioBusy) return;
    commitFocusedControl();
    const result = approve ? runValidation() : null;
    if (approve && result.errors > 0) return;
    const documentAtStart = state.document;
    const revision = state.revision;
    const snapshot = AI.clone(documentAtStart);
    if (approve) snapshot.approval = {
      status: "approved", validatorVersion: AI.VALIDATOR_VERSION,
      approvedAt: new Date().toISOString(), scope: "structure-and-progress",
      errors: 0, warnings: result.warnings
    };
    const suggestedName = (AI.ID_PATTERN.test(snapshot.documentId) ? snapshot.documentId : "MonsterDefinitions") + (approve ? ".approved.ai.json" : ".ai.json");
    state.ioBusy = true;
    renderStatus();
    try {
      let handle = approve ? null : state.fileHandle;
      let downloaded = false;
      if (!handle && typeof window.showSaveFilePicker === "function") {
        try {
          handle = await window.showSaveFilePicker({ suggestedName, types: fileTypes });
        } catch (error) {
          if (error.name === "AbortError") return;
          if (error.name !== "SecurityError" && error.name !== "NotSupportedError") throw error;
        }
      }
      if (handle) {
        const writable = await handle.createWritable();
        try {
          await writable.write(JSON.stringify(snapshot, null, 2) + "\n");
          await writable.close();
        } catch (error) {
          try { await writable.abort(); } catch (_) { /* Keep the original write error. */ }
          throw error;
        }
      } else {
        fallbackDownload(snapshot, suggestedName);
        downloaded = true;
      }
      if (state.document === documentAtStart) {
        // Approved exports are snapshots; later draft saves must not overwrite them.
        if (!approve) {
          state.fileHandle = handle;
          state.fileName = handle ? handle.name : suggestedName;
        }
        if (state.revision === revision) {
          if (approve) state.document.approval = snapshot.approval;
          // Download completion cannot be verified; keep the unsaved warning.
          state.dirty = downloaded;
        }
      }
      renderStatus();
      const note = downloaded ? "다운로드를 요청했습니다. 브라우저의 다운로드 목록에서 저장 여부를 확인하세요."
        : "파일을 저장했습니다.";
      state.fileNote = (approve ? (handle ? handle.name : suggestedName) + " · " : "") + note + (state.revision !== revision ? " 저장 중 변경된 내용은 다시 저장해야 합니다." : "");
      if (approve && state.revision === revision) {
        renderValidation();
        $("validation-results").prepend(element("p", "success", "AI 정적 검사 승인 완료. 승인 파일을 " + (downloaded ? "다운로드 요청했습니다." : "저장했습니다.")));
      }
    } catch (error) {
      if (error.name !== "AbortError") alert("파일을 저장하지 못했습니다: " + error.message);
    } finally {
      state.ioBusy = false;
      renderStatus();
    }
  }
  function canDiscardChanges() {
    commitFocusedControl();
    return !state.dirty || confirm("저장하지 않은 변경이 있습니다. 현재 문서를 닫고 계속할까요?");
  }
  async function importFile(file, handle) {
    if (file.size > 5 * 1024 * 1024) throw new Error("JSON 파일은 최대 5MB까지 불러올 수 있습니다.");
    const text = (await file.text()).replace(/^\uFEFF/, "");
    const next = JSON.parse(text);
    if (!AI.isEditableDocument(next)) throw new Error("지원하지 않는 버전 또는 편집할 수 없는 구조입니다. schemaVersion 1과 nodes/edges/parameters 구조를 확인하세요.");
    const approvedSource = next.approval?.status === "approved" || file.name.endsWith(".approved.ai.json");
    // Approval from a file is never trusted; a modified file must be re-approved.
    AI.resetApproval(next);
    state.document = next;
    state.monsterIndex = 0;
    state.selection = null;
    state.connectFrom = null;
    state.fileHandle = approvedSource ? null : handle;
    state.fileName = file.name;
    state.fileNote = "불러온 파일은 다시 승인해야 합니다.";
    state.dirty = true;
    ++state.revision;
    state.result = AI.validateDocument(next);
    renderAll();
    fitGraph();
    $("validation-results").prepend(element("p", "approval-note", "불러온 파일의 승인 기록을 해제했습니다. 현재 내용으로 다시 승인하세요."));
  }
  async function openDocument() {
    if (state.ioBusy || !canDiscardChanges()) return;
    if (typeof window.showOpenFilePicker !== "function") { $("file-input").click(); return; }
    state.ioBusy = true;
    renderStatus();
    try {
      const handles = await window.showOpenFilePicker({ types: fileTypes, multiple: false });
      const file = await handles[0].getFile();
      await importFile(file, handles[0]);
    } catch (error) {
      if (error.name === "SecurityError" || error.name === "NotSupportedError") $("file-input").click();
      else if (error.name !== "AbortError") alert("파일을 불러오지 못했습니다: " + error.message);
    } finally { state.ioBusy = false; renderStatus(); }
  }
  function newDocument() {
    if (state.ioBusy || !canDiscardChanges()) return;
    state.document = AI.createDocument();
    state.monsterIndex = 0;
    state.selection = null;
    state.connectFrom = null;
    state.fileHandle = null;
    state.fileName = "";
    state.fileNote = "";
    state.dirty = true;
    ++state.revision;
    state.result = null;
    renderAll();
    fitGraph();
  }

  $("graph").addEventListener("pointerdown", event => {
    if (event.button !== 0 && event.button !== 1) return;
    commitFocusedControl();
    $("graph").focus();
    const nodeElement = event.target.closest("[data-node-index]");
    const edgeElement = event.target.closest("[data-edge-index]");
    const nodeIndex = nodeElement ? Number(nodeElement.getAttribute("data-node-index")) : null;
    if (event.button === 0 && nodeIndex !== null) {
      if (state.connectFrom !== null) { finishConnection(nodeIndex); return; }
      if (event.target.getAttribute("data-port") === "out") { beginConnection(nodeIndex); return; }
      selectItem("node", nodeIndex);
      const position = nodePosition(currentAI().nodes[nodeIndex]);
      state.drag = { type: "node", index: nodeIndex, start: worldPoint(event), original: position, changed: false };
    } else if (event.button === 0 && edgeElement) {
      selectItem("edge", Number(edgeElement.getAttribute("data-edge-index")));
      return;
    } else {
      if (event.button === 0) selectItem(null);
      state.drag = { type: "pan", x: event.clientX, y: event.clientY, original: { x: state.view.x, y: state.view.y } };
    }
    $("graph").setPointerCapture(event.pointerId);
    event.preventDefault();
  });
  $("graph").addEventListener("pointermove", event => {
    if (state.connectFrom !== null) { state.pointer = worldPoint(event); renderGraph(); }
    if (!state.drag) return;
    const drag = state.drag;
    if (drag.type === "pan") {
      state.view.x = drag.original.x + event.clientX - drag.x;
      state.view.y = drag.original.y + event.clientY - drag.y;
    } else {
      const point = worldPoint(event);
      const position = currentAI().nodes[drag.index].position;
      const x = Math.max(-1000000, Math.min(1000000, Math.round(drag.original.x + point.x - drag.start.x)));
      const y = Math.max(-1000000, Math.min(1000000, Math.round(drag.original.y + point.y - drag.start.y)));
      if (position.x !== x || position.y !== y) {
        position.x = x;
        position.y = y;
        drag.changed = true;
        invalidate();
      }
    }
    renderGraph();
  });
  const endDrag = event => {
    if (state.drag?.changed) renderValidation();
    state.drag = null;
    if ($("graph").hasPointerCapture(event.pointerId)) $("graph").releasePointerCapture(event.pointerId);
  };
  $("graph").addEventListener("pointerup", endDrag);
  $("graph").addEventListener("pointercancel", endDrag);
  $("graph").addEventListener("lostpointercapture", () => { state.drag = null; });
  $("graph").addEventListener("wheel", event => {
    event.preventDefault();
    if (state.drag) return;
    const point = worldPoint(event);
    const bounds = $("graph").getBoundingClientRect();
    state.view.zoom = Math.max(0.12, Math.min(2.5, state.view.zoom * Math.exp(-event.deltaY * 0.001)));
    state.view.x = event.clientX - bounds.left - point.x * state.view.zoom;
    state.view.y = event.clientY - bounds.top - point.y * state.view.zoom;
    renderGraph();
  }, { passive: false });
  $("graph").addEventListener("contextmenu", event => event.preventDefault());
  $("new-document").addEventListener("click", newDocument);
  $("open-document").addEventListener("click", openDocument);
  $("save-document").addEventListener("click", () => saveDocument(false));
  $("approve-document").addEventListener("click", () => saveDocument(true));
  $("validate-document").addEventListener("click", runValidation);
  $("add-node").addEventListener("click", () => { commitFocusedControl(); addNode(); });
  $("connect-nodes").addEventListener("click", () => {
    commitFocusedControl();
    if (!selectedNode()) return alert("출발 상태를 먼저 선택하세요.");
    beginConnection(state.selection.index);
  });
  $("fit-graph").addEventListener("click", fitGraph);
  $("add-monster").addEventListener("click", () => {
    commitFocusedControl();
    if (state.document.monsters.length >= AI.MAX_MONSTERS) return alert("몬스터 수 한도에 도달했습니다.");
    state.document.monsters.push(AI.createMonster(uniqueId(state.document.monsters, "Monster"), "새 몬스터"));
    state.monsterIndex = state.document.monsters.length - 1;
    state.selection = null;
    state.connectFrom = null;
    touch(true);
    fitGraph();
  });
  $("duplicate-monster").addEventListener("click", () => {
    commitFocusedControl();
    if (!currentMonster() || state.document.monsters.length >= AI.MAX_MONSTERS) return;
    const copy = AI.clone(currentMonster());
    copy.id = uniqueId(state.document.monsters, "Monster");
    copy.name = plain(copy.name) + " 복사";
    state.document.monsters.push(copy);
    state.monsterIndex = state.document.monsters.length - 1;
    state.selection = null;
    state.connectFrom = null;
    touch(true);
    fitGraph();
  });
  $("delete-monster").addEventListener("click", () => {
    commitFocusedControl();
    if (!currentMonster() || !confirm("선택한 몬스터와 AI 그래프를 삭제할까요?")) return;
    state.document.monsters.splice(state.monsterIndex, 1);
    state.monsterIndex = Math.max(0, state.monsterIndex - 1);
    state.selection = null;
    state.connectFrom = null;
    touch(true);
    fitGraph();
  });
  $("skills-tab").addEventListener("click", () => openCatalog("skill"));
  $("motions-tab").addEventListener("click", () => openCatalog("motion"));
  $("close-catalog").addEventListener("click", () => { commitFocusedControl(); $("catalog-dialog").close(); renderInspector(); });
  $("catalog-dialog").addEventListener("cancel", () => { commitFocusedControl(); renderInspector(); });
  $("add-catalog-entry").addEventListener("click", () => {
    commitFocusedControl();
    const list = state.catalogType === "skill" ? state.document.skills : state.document.motions;
    if (list.length >= 2048) return alert("정의 목록 한도에 도달했습니다.");
    const id = uniqueId(list, state.catalogType === "skill" ? "Skill" : "Motion");
    list.push(state.catalogType === "skill"
      ? { id, label: "새 스킬", animationId: id + "Animation", durationSeconds: 0.5, cooldownSeconds: 1, minRange: 0, maxRange: 70, hitType: "Normal" }
      : { id, label: "새 동작", animationId: id + "Animation", durationSeconds: 0.5, loop: false });
    state.catalogIndex = list.length - 1;
    touch();
    renderCatalog();
  });
  $("file-input").addEventListener("change", async () => {
    const file = $("file-input").files[0];
    if (!file) return;
    state.ioBusy = true;
    renderStatus();
    try { await importFile(file, null); }
    catch (error) { alert("파일을 불러오지 못했습니다: " + error.message); }
    finally { $("file-input").value = ""; state.ioBusy = false; renderStatus(); }
  });
  // Clear approval immediately, including edits not yet committed by a blur event.
  document.addEventListener("input", event => {
    if ($("inspector-content").contains(event.target) || $("catalog-fields").contains(event.target)) {
      invalidate();
      renderValidation();
    }
  });
  document.addEventListener("keydown", event => {
    if (state.ioBusy) { event.preventDefault(); return; }
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") { event.preventDefault(); saveDocument(false); }
    else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "o") {
      event.preventDefault();
      commitFocusedControl();
      if ($("catalog-dialog").open) $("catalog-dialog").close();
      openDocument();
    }
    else if ($("catalog-dialog").open) return;
    else if (event.key === "Escape") { state.connectFrom = null; state.pointer = null; renderGraph(); }
    else if (event.key === "Delete" && !["INPUT", "SELECT", "TEXTAREA"].includes(event.target.tagName)) {
      event.preventDefault();
      deleteSelection();
    }
  });
  window.addEventListener("beforeunload", event => {
    if (state.dirty) { event.preventDefault(); event.returnValue = ""; }
  });
  window.addEventListener("resize", renderGraph);
  renderAll();
  requestAnimationFrame(fitGraph);
}());
