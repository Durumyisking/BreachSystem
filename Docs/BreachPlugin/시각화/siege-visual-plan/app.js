(() => {
  'use strict';

  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const state = { material: 'plaster', attack: 'rifle', phase: 5, recut: 1, hits: 0, cells: new Set(), timer: null };
  const materials = {
    plaster: { name: '석고보드', face: '#d5d2c2', core: '#bdb096', edge: '#f0eadb', crack: '#82765f' },
    wood: { name: '바리케이드', face: '#ae9a68', core: '#806540', edge: '#d0ba80', crack: '#675238' },
    brick: { name: '벽돌', face: '#ac6f4d', core: '#8c573a', edge: '#c6ae8b', crack: '#68442e' }
  };
  const phases = [
    { title: '영향 범위와 재료 도안', description: '공격의 위치·방향·크기와 재료 도안을 결합합니다. 직접 절단할 외곽과 내부 조각, 누적 피해를 줄 범위를 구분합니다. 현재는 공격 설정으로 만든 외곽 다각형을 로컬 타격 위치로 옮기는 경로가 있습니다.', data: ['공격 입력', '표면 좌표', '재료 방향 / UV', '분할 도안'] },
    { title: '구멍과 제거 조각의 윤곽', description: '직접 절단, 누적 손상, 지지 연결 판정으로 제거 영역을 확정합니다. 남는 표면만 보관하지 않고, 제거 조각과 새 경계도 표현 단계에 전달합니다. 이 도식의 라이플 구멍은 누적 탈락 이후의 예시입니다.', data: ['남는 영역', '제거 영역', '조각별 윤곽', '새 경계'] },
    { title: '단면의 두께와 내부 재질', description: '표면과 내부가 다른 재질을 사용하고, 두께 방향을 포함한 UV와 노멀맵 기준을 만듭니다. 거친 심재와 박리 형상은 측면에서도 실제 깊이를 가져야 합니다.', data: ['Material ID', '경계 거리 × 두께 UV', '단면 형상', '법선 / 탄젠트'] },
    { title: '표면에 남는 균열과 흔적', description: 'Cut Decoration은 남는 표면에 붙습니다. 다음 타격으로 표면이 잘리면 균열과 흔적도 같은 영역에서 잘려야 합니다. 가는 균열의 재질과 실제 벌어진 형상은 필요한 시각 효과에 맞게 구성합니다.', data: ['표면 장식', '표면 좌표', '재절단 클리핑', '균열 / 그을음'] },
    { title: '경계에 남는 입체 조각', description: 'Feature-bound Decoration은 경계·꼭짓점에 붙는 종이 박리, 석고 조각, 목재 가시 등입니다. 현재 경계가 없어지면 장식도 제거하거나 새 경계에 맞게 재구성합니다.', data: ['경계 정보', '입체 장식', '실루엣 / 시차', '장식 수명'] },
    { title: '잔해와 효과까지 연결', description: '큰 탈락 조각의 윤곽·두께·재질을 파괴 결과와 맞추고, 작은 파편·분진·그을음을 같은 위치와 타이밍에 연결합니다. 도식의 떠 있는 조각은 구성 표시이며 물리 시뮬레이션 결과가 아닙니다.', data: ['탈락 조각', '파편 / 분진', '공격 방향', '발생 타이밍'] }
  ];

  function polygon(points) {
    return points.map(([x, y], i) => `${i ? 'L' : 'M'}${x.toFixed(1)} ${y.toFixed(1)}`).join(' ') + ' Z';
  }

  function holePoints() {
    const { material, attack } = state;
    const cx = 299, cy = 166;
    const radius = attack === 'melee' ? 65 : attack === 'shotgun' ? 104 : attack === 'breach' ? 119 : 86;
    return Array.from({ length: 24 }, (_, i) => {
      const angle = Math.PI * 2 * i / 24;
      let x, y;
      if (attack === 'breach') {
        x = Math.sign(Math.cos(angle)) * Math.pow(Math.abs(Math.cos(angle)), 0.25) * radius;
        y = Math.sign(Math.sin(angle)) * Math.pow(Math.abs(Math.sin(angle)), 0.25) * radius * 0.88;
      } else {
        const irregularity = 1 + Math.sin(i * 2.72 + 0.9) * 0.13 + Math.cos(i * 1.7) * 0.06;
        x = Math.cos(angle) * radius * irregularity * (material === 'wood' ? 1.32 : 1);
        y = Math.sin(angle) * radius * irregularity * (material === 'wood' ? 0.68 : 1);
      }
      if (material === 'brick' && attack !== 'breach') {
        x = Math.round(x / 29) * 29;
        y = Math.round(y / 25) * 25;
      }
      return [cx + x, cy + y];
    });
  }

  function wallTexture(id, material) {
    const m = materials[material];
    if (material === 'wood') {
      return `<pattern id="${id}" width="540" height="48" patternUnits="userSpaceOnUse"><rect width="540" height="48" fill="${m.face}"/><path d="M0 47H540" stroke="#736342" stroke-width="3"/><path d="M0 9Q100 2 240 10T540 7M0 23Q170 14 330 24T540 20M0 37Q100 31 300 36T540 36" fill="none" stroke="#8c774d" stroke-width="1.2"/><ellipse cx="130" cy="24" rx="14" ry="4" fill="none" stroke="#8c774d"/></pattern>`;
    }
    if (material === 'brick') {
      return `<pattern id="${id}" width="144" height="80" patternUnits="userSpaceOnUse"><rect width="144" height="80" fill="#aaa18b"/><path d="M4 4H140V36H4ZM-68 44H68V76H-68ZM76 44H212V76H76Z" fill="${m.face}"/><path d="M8 9H132M80 49H141M5 49H61" stroke="#bb8058" stroke-width="2" opacity=".7"/></pattern>`;
    }
    return `<pattern id="${id}" width="190" height="145" patternUnits="userSpaceOnUse"><rect width="190" height="145" fill="${m.face}"/><path d="M0 77H190M121 0V145" stroke="#e5e1d1" stroke-width="7" opacity=".75"/><path d="M16 22L70 18L82 49L21 47ZM139 92L184 87V121L143 128Z" fill="#c7c5b7" opacity=".6"/></pattern>`;
  }

  function cracks(points, material) {
    const color = materials[material].crack;
    return points.filter((_, i) => i % 3 === 0).map(([x, y], i) => {
      const dx = x - 299, dy = y - 166, length = Math.hypot(dx, dy);
      const nx = dx / length, ny = dy / length;
      const mx = x + nx * 27 - ny * 10, my = y + ny * 27 + nx * 10;
      const ex = x + nx * (49 + i % 3 * 9), ey = y + ny * (49 + i % 3 * 9);
      return `<path d="M${x} ${y}L${mx} ${my}L${ex} ${ey}m${-nx * 18} ${-ny * 18}l${ny * 19 + nx * 9} ${-nx * 19 + ny * 9}" fill="none" stroke="${color}" stroke-width="1.5"/>`;
    }).join('');
  }

  function flakes(points, material) {
    const m = materials[material];
    return points.filter((_, i) => i % 3 === 1).map(([x, y], i) => {
      const dx = x - 299, dy = y - 166, len = Math.hypot(dx, dy), nx = dx / len, ny = dy / len;
      const reach = material === 'wood' ? 35 + i % 3 * 10 : 15 + i % 3 * 6;
      const width = material === 'wood' ? 5 : 14;
      const pts = [[x - ny * width, y + nx * width], [x - nx * reach - ny * 4, y - ny * reach + nx * 4], [x - nx * (reach + 5) + ny * 6, y - ny * (reach + 5) - nx * 6], [x + ny * width + nx * 8, y - nx * width + ny * 8]];
      return `<path d="${polygon(pts.map(([a, b]) => [a + 3, b + 5]))}" fill="#334b3544"/><path d="${polygon(pts)}" fill="${m.edge}" stroke="${m.core}" stroke-width="1.2"/>`;
    }).join('');
  }

  function scene(target) {
    const phase = state.phase, material = state.material, m = materials[material];
    const id = target ? 'target' : 'current', points = holePoints(), hole = polygon(points);
    const cut = phase >= 1;
    let detail = '';
    if (target && phase >= 2) {
      detail += `<g mask="url(#wall-${id})"><path d="${hole}" fill="none" stroke="${m.core}" stroke-width="18" stroke-linejoin="round"/><path d="${hole}" fill="none" stroke="${m.edge}" stroke-width="3"/></g>`;
    }
    if (target && phase >= 3) {
      detail += `<g mask="url(#wall-${id})">${cracks(points, material)}</g>`;
      if (state.attack === 'breach') detail += `<path d="${hole}" fill="none" stroke="#554a344f" stroke-width="29" mask="url(#wall-${id})"/>`;
    }
    if (target && phase >= 4) detail += flakes(points, material);
    if (target && phase >= 5) {
      detail += `<g fill="${m.core}" stroke="${m.crack}" stroke-width=".8"><path d="M180 304l25-8 19 15-31 12-17-10Z"/><path d="M323 303l28-15 18 17-11 19-26-2Z"/><path d="M432 286l18-9 12 20-16 9Z"/><path d="M225 237l15-7 14 12-10 18-21-8Z"/></g>`;
      detail += Array.from({ length: 18 }, (_, i) => `<circle cx="${141 + (i * 47) % 332}" cy="${287 + i % 4 * 9}" r="${1.5 + i % 3}" fill="${m.core}" opacity="${0.3 + i % 4 * .12}"/>`).join('');
    }
    return `<svg viewBox="0 0 600 350" role="img" aria-label="${m.name} 파괴 구성 ${target ? '목표' : '기본'} 도식, ${phase + 1}단계"><defs>${wallTexture(`texture-${id}`, material)}<mask id="wall-${id}" maskUnits="userSpaceOnUse" x="0" y="0" width="600" height="350"><rect x="31" y="27" width="538" height="263" rx="2" fill="white"/>${cut ? `<path d="${hole}" fill="black"/>` : ''}</mask></defs><path d="M31 290H569L597 335H4Z" fill="#d1d9c4"/><rect x="31" y="27" width="538" height="263" fill="#405f4b"/><path d="M40 262H560M55 244H550M61 41V289M540 40V290" stroke="#68836c" opacity=".45"/><path d="M229 85H412V216H229Z" fill="#b2c2a0" opacity=".55"/><path d="M235 205L390 101M235 101L390 205" stroke="#77926e" stroke-width="2" opacity=".5"/><rect x="31" y="27" width="538" height="263" fill="url(#texture-${id})" mask="url(#wall-${id})"/>${cut && (!target || phase < 2) ? `<path d="${hole}" fill="none" stroke="${m.core}" stroke-width="3"/>` : ''}${phase === 0 ? `<path d="${hole}" fill="#b86c4530" stroke="#ab5934" stroke-width="2" stroke-dasharray="8 6"/><circle cx="299" cy="166" r="5" fill="#a64d2c"/>` : ''}${detail}<path d="M31 291H569" stroke="#71866b" stroke-width="2"/></svg>`;
  }

  function renderPipeline() {
    $('#current-scene').innerHTML = scene(false);
    $('#target-scene').innerHTML = scene(true);
    const phase = phases[state.phase];
    $('#target-scene-title').textContent = phase.title;
    $('#phase-count').textContent = `${String(state.phase + 1).padStart(2, '0')} / 06`;
    $('#phase-title').textContent = phase.title;
    $('#phase-description').textContent = phase.description;
    $('#phase-data').replaceChildren(...phase.data.map(text => {
      const span = document.createElement('span'); span.textContent = text; return span;
    }));
    $$('[data-phase]').forEach(button => button.setAttribute('aria-pressed', String(Number(button.dataset.phase) === state.phase)));
  }

  function stopPlayback() {
    if (state.timer !== null) window.clearInterval(state.timer);
    state.timer = null;
    $('#play-stages').textContent = '단계 재생';
  }

  $('#material').addEventListener('change', event => { state.material = event.target.value; renderPipeline(); });
  $('#attack').addEventListener('change', event => { state.attack = event.target.value; renderPipeline(); });
  $$('[data-phase]').forEach(button => button.addEventListener('click', () => { stopPlayback(); state.phase = Number(button.dataset.phase); renderPipeline(); }));
  $('#play-stages').addEventListener('click', () => {
    if (state.timer !== null) { stopPlayback(); return; }
    state.phase = 0; renderPipeline(); $('#play-stages').textContent = '재생 중지';
    const delay = window.matchMedia('(prefers-reduced-motion: reduce)').matches ? 1400 : 1000;
    state.timer = window.setInterval(() => { state.phase += 1; renderPipeline(); if (state.phase === 5) stopPlayback(); }, delay);
  });

  function brickScene(allocated) {
    const id = allocated ? 'allocated' : 'white';
    let cells = '';
    for (let row = 0; row < 5; row++) {
      for (let col = -1; col < 6; col++) {
        const key = `${row}:${col}`, x = 20 + col * 112 - (row % 2 ? 56 : 0), y = 24 + row * 48;
        if (x + 112 <= 20 || x >= 580) continue;
        const selected = state.cells.has(key), open = '#45664f';
        cells += `<g role="button" tabindex="0" aria-label="${row + 1}행 ${col + 2}번 벽돌 ${selected ? '복구' : '제거'}" aria-pressed="${selected}" data-cell="${key}"><rect x="${x}" y="${y}" width="112" height="48" fill="${selected && allocated ? open : '#9b9580'}"/><rect class="brick-face" x="${x + 4}" y="${y + 4}" width="104" height="40" rx="2" fill="${selected ? open : (row + col) % 3 ? '#ac7352' : '#9f6646'}"/>${selected ? `<path d="M${x + 50} ${y + 18}l12 12m0-12-12 12" stroke="#b9c8ab" stroke-width="1.6"/>` : ''}</g>`;
      }
    }
    return `<svg viewBox="0 0 600 295" role="group" aria-label="${allocated ? '줄눈을 배정한 셀' : '흰 벽돌 면'} 제거 도식. 벽돌을 누르거나 Tab과 Enter로 선택하세요."><defs><clipPath id="brick-clip-${id}"><rect x="20" y="24" width="560" height="240" rx="2"/></clipPath></defs><rect x="20" y="24" width="560" height="240" fill="#45664f"/><g clip-path="url(#brick-clip-${id})">${cells}</g></svg>`;
  }

  function renderBricks() {
    $('#brick-white').innerHTML = brickScene(false);
    $('#brick-cells').innerHTML = brickScene(true);
    $('#brick-count').textContent = `선택 ${state.cells.size}개`;
  }

  $$('.brick-demo').forEach(root => {
    const activate = (event, keyboard) => {
      const cell = event.target.closest('[data-cell]');
      if (!cell || !root.contains(cell)) return;
      if (keyboard && !['Enter', ' '].includes(event.key)) return;
      event.preventDefault();
      const key = cell.dataset.cell;
      if (state.cells.has(key)) state.cells.delete(key); else state.cells.add(key);
      renderBricks();
      if (keyboard) root.querySelector(`[data-cell="${key}"]`).focus();
    };
    root.addEventListener('click', event => activate(event, false));
    root.addEventListener('keydown', event => activate(event, true));
  });
  $('#brick-preset').addEventListener('click', () => { state.cells = new Set(['2:2', '2:3', '3:2']); renderBricks(); });
  $('#brick-reset').addEventListener('click', () => { state.cells.clear(); renderBricks(); });

  const shotPattern = [[263, 143], [290, 160], [313, 146], [277, 183], [307, 183], [288, 131]];
  function damageScene(withCells) {
    const shots = Array.from({ length: state.hits }, (_, i) => shotPattern[i % shotPattern.length]);
    let content = '', removed = 0;
    const vertex = (col, row) => [35 + col * 66 + (col > 0 && col < 8 ? Math.sin(col * 41 + row * 17) * 7 : 0), 45 + row * 46 + (row > 0 && row < 5 ? Math.cos(col * 23 + row * 37) * 6 : 0)];
    if (withCells) {
      for (let row = 0; row < 5; row++) {
        for (let col = 0; col < 8; col++) {
          const points = [vertex(col, row), vertex(col + 1, row), vertex(col + 1, row + 1), vertex(col, row + 1)];
          const x = points.reduce((sum, p) => sum + p[0], 0) / 4, y = points.reduce((sum, p) => sum + p[1], 0) / 4;
          const damage = shots.reduce((sum, p) => sum + Math.max(0, 1 - Math.hypot(x - p[0], y - p[1]) / 110) * .5, 0);
          const broken = damage >= 1.5;
          if (broken) removed += 1;
          content += `<path d="${polygon(points)}" fill="${broken ? '#45664f' : '#d6d2bf'}" stroke="${broken ? '#45664f' : '#aaa995'}" stroke-width="1"/>`;
          if (!broken && damage > .05) content += `<path d="${polygon(points)}" fill="#bb714d" opacity="${Math.min(.65, damage / 1.5 * .65)}"/>`;
        }
      }
    } else content = '<rect x="35" y="45" width="528" height="230" fill="#d6d2bf"/>';
    content += shots.map(([x, y]) => `<circle cx="${x}" cy="${y}" r="4.5" fill="#45664f"/>`).join('');
    return `<svg viewBox="0 0 600 320" role="img" data-removed="${removed}" aria-label="${withCells ? '주변 피해를 누적한' : '작은 구멍만 낸'} 타격 ${state.hits}회 도식, 추가 탈락 ${removed}개 영역"><rect x="35" y="45" width="528" height="230" fill="#45664f"/>${content}</svg>`;
  }
  function renderDamage() {
    $('#damage-simple').innerHTML = damageScene(false);
    $('#damage-cells').innerHTML = damageScene(true);
    const removed = $('#damage-cells svg').getAttribute('data-removed');
    $('#damage-count').textContent = `타격 ${state.hits}회 · 추가 탈락 ${removed}개 영역`;
    $('#damage-add').disabled = state.hits >= 12;
  }
  $('#damage-add').addEventListener('click', () => { state.hits = Math.min(12, state.hits + 1); renderDamage(); });
  $('#damage-six').addEventListener('click', () => { state.hits = 6; renderDamage(); });
  $('#damage-reset').addEventListener('click', () => { state.hits = 0; renderDamage(); });

  function recutScene(correct) {
    const id = correct ? 'good' : 'bad';
    const allHoles = [{ x: 239, y: 166, r: 76 }, { x: 359, y: 164, r: 96 }];
    const holes = allHoles.slice(0, state.recut);
    const holeMarkup = holes.map(hole => `<circle cx="${hole.x}" cy="${hole.y}" r="${hole.r}" fill="black"/>`).join('');
    let marks = '', features = '';
    holes.forEach((hole, h) => {
      for (let i = 0; i < 12; i++) {
        const angle = i * Math.PI / 6 + .13;
        const nx = Math.cos(angle), ny = Math.sin(angle), x = hole.x + nx * hole.r, y = hole.y + ny * hole.r;
        marks += `<path d="M${x} ${y}l${nx * 28 - ny * 8} ${ny * 28 + nx * 8}l${nx * 22 + ny * 10} ${ny * 22 - nx * 10}" stroke="#8c7255" stroke-width="1.7" fill="none"/>`;
        const covered = holes.some((other, j) => h !== j && Math.hypot(x - other.x, y - other.y) < other.r + 6);
        if (!correct || !covered) {
          const pts = [[x - ny * 10 + nx * 7, y + nx * 10 + ny * 7], [x - nx * 26 - ny * 5, y - ny * 26 + nx * 5], [x - nx * 19 + ny * 11, y - ny * 19 - nx * 11], [x + ny * 9 + nx * 7, y - nx * 9 + ny * 7]];
          features += `<path d="${polygon(pts)}" fill="${h === 0 ? '#f1e6cd' : '#e1d7bc'}" stroke="#a69b81" stroke-width="1.2"/>`;
        }
      }
    });
    return `<svg viewBox="0 0 600 350" role="img" aria-label="${correct ? '경계 수명을 관리한' : '장식을 덧붙이기만 한'} 재타격 도식, 타격 ${state.recut}회"><defs><mask id="recut-mask-${id}" maskUnits="userSpaceOnUse" x="0" y="0" width="600" height="350"><rect x="31" y="27" width="538" height="263" fill="white"/>${holeMarkup}</mask></defs><rect x="31" y="27" width="538" height="263" fill="#45664e"/><path d="M31 250H569M85 27V290M503 27V290" stroke="#839b76" opacity=".35"/><rect x="31" y="27" width="538" height="263" fill="#d6d2bf" mask="url(#recut-mask-${id})"/><path d="M31 107H569M145 27V290" stroke="#e6e1d1" stroke-width="7" mask="url(#recut-mask-${id})"/><g ${correct ? `mask="url(#recut-mask-${id})"` : ''}>${marks}</g>${features}<path d="M31 291H569" stroke="#879879" stroke-width="2"/></svg>`;
  }

  function renderRecut() {
    $('#recut-bad').innerHTML = recutScene(false);
    $('#recut-good').innerHTML = recutScene(true);
    $$('[data-recut]').forEach(button => button.setAttribute('aria-pressed', String(Number(button.dataset.recut) === state.recut)));
  }
  $$('[data-recut]').forEach(button => button.addEventListener('click', () => { state.recut = Number(button.dataset.recut); renderRecut(); }));

  $$('[data-filter]').forEach(button => button.addEventListener('click', () => {
    $$('[data-filter]').forEach(item => item.setAttribute('aria-pressed', String(item === button)));
    $$('.requirement').forEach(card => { card.hidden = button.dataset.filter !== 'all' && card.dataset.kind !== button.dataset.filter; });
  }));

  const checkKey = 'breach-visual-plan-20261021';
  const checks = $$('[data-check]');
  let canStore = true;
  try {
    const saved = JSON.parse(localStorage.getItem(checkKey) || '[]');
    if (Array.isArray(saved)) checks.forEach(check => { check.checked = saved.includes(check.dataset.check); });
  } catch (_) { canStore = false; }
  function updateChecks(save = true) {
    const selected = checks.filter(check => check.checked).map(check => check.dataset.check);
    $('#check-count').textContent = `${selected.length} / ${checks.length}`;
    $('#check-progress').max = checks.length;
    $('#check-progress').value = selected.length;
    if (save && canStore) {
      try { localStorage.setItem(checkKey, JSON.stringify(selected)); } catch (_) { canStore = false; }
    }
    if (!canStore) $('#storage-note').textContent = '이 환경에서는 브라우저 저장소를 사용할 수 없어 현재 화면에서만 기록합니다. 실제 Editor/PIE 결과를 자동 판정하지 않으며 대응 증거는 별도로 남겨야 합니다.';
  }
  checks.forEach(check => check.addEventListener('change', () => updateChecks()));
  $('#clear-checks').addEventListener('click', () => { checks.forEach(check => { check.checked = false; }); updateChecks(); });

  const dialog = $('#image-dialog');
  $$('[data-image]').forEach(button => button.addEventListener('click', () => {
    $('#dialog-image').src = button.dataset.image;
    $('#dialog-image').alt = $('img', button).alt;
    $('#dialog-caption').textContent = button.dataset.caption;
    dialog.showModal();
  }));
  dialog.addEventListener('click', event => { if (event.target === dialog) dialog.close(); });
  $('.print-button').addEventListener('click', () => window.print());

  if ('IntersectionObserver' in window) {
    const observer = new IntersectionObserver(entries => {
      entries.forEach(entry => {
        if (!entry.isIntersecting) return;
        $$('.sidebar nav a').forEach(link => link.classList.toggle('active', link.getAttribute('href') === `#${entry.target.id}`));
      });
    }, { rootMargin: '-10% 0px -65% 0px', threshold: 0 });
    $$('section[id]').forEach(section => observer.observe(section));
  }
  window.addEventListener('pagehide', stopPlayback);
  renderPipeline(); renderBricks(); renderDamage(); renderRecut(); updateChecks(false);
})();
