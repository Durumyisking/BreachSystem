/* React 18 educational model. Original RealBlast and Unreal code are not reproduced. */
const h=React.createElement;
const {useState,useEffect,useMemo}=React;
const PDF='https://media.gdcvault.com/gdc2016/Presentations/LHeureux_Julien_Art_Of_Destruction.pdf';
const OUTER=[[0,0],[480,0],[480,300],[0,300],[0,0]];
const WALL=[[OUTER]];
const steps=[
 {id:'overview',page:35,title:'평면으로 제한하면 계산이 줄어든다',short:'원래 벽',desc:'한 장의 평평한 벽을 준비합니다. 이 실험에서는 가로 480, 세로 300인 2D 표면이 기준 데이터입니다. 두께는 나중에 붙입니다.',ue:'벽의 로컬 평면, 외곽선, 두께를 별도 데이터로 보관합니다.'},
 {id:'projection',page:36,title:'3D 위치를 벽의 2D 좌표로',short:'2D 투영',desc:'벽 위의 가로·세로 축만 남깁니다. 화면 카메라에 투영하는 것이 아니라, 벽 자체의 좌표계로 옮기는 과정입니다.',ue:'InverseTransformPosition → 평면 축 U, V와 내적. 평면 좌표와 텍스처 UV는 구분합니다.'},
 {id:'pattern',page:37,title:'충돌 지점에서 절단 경계를 만든다',short:'커터 생성',desc:'주황색 점선은 잘라낼 후보 영역입니다. 아직 벽은 남아 있습니다. 크기·형태·위치를 바꾸면 입력 다각형이 달라집니다.',ue:'재료와 충돌 입력으로 폐곡선을 생성합니다. 여기의 난수는 재현 가능한 고정 seed를 씁니다.'},
 {id:'clipping',page:38,title:'겹치는 영역을 분류하고 경계를 다시 잇는다',short:'2D 절단',desc:'실제 다각형 차집합 S ∖ C를 계산합니다. 외곽에 닿으면 홈, 내부에 있으면 구멍이 됩니다. 벽을 누르면 절단을 누적할 수 있습니다.',ue:'2D polygon Boolean의 결과를 외곽선·구멍·분리된 여러 영역으로 보관합니다.'},
 {id:'triangulation',page:39,title:'구멍을 피해서 삼각형을 채운다',short:'삼각분할',desc:'남은 각 다각형과 구멍을 Earcut에 전달해 실제 삼각분할을 합니다. 선은 계산 결과이며 구멍을 가로질러 채우지 않습니다.',ue:'hole-aware Ear Clipping 또는 제약 Delaunay를 사용합니다. 단순 중심점 fan은 오목한 경계에 부적합합니다.'},
 {id:'extrusion',page:40,title:'앞면·뒷면·절단면을 연결한다',short:'두께 복원',desc:'같은 경계를 두께 방향으로 복제하고 경계 사이를 연결합니다. 두께 슬라이더로 새로 만들어지는 옆면을 확인하세요.',ue:'FDynamicMesh3에 앞·뒤 삼각형과 경계 측면을 생성합니다. 내부 구멍의 벽면도 필요합니다.'}
];
function ringPath(r){return r.map((p,i)=>(i?'L':'M')+p.join(',')).join(' ')+'Z';}
function multiPath(m){return m.flatMap(p=>p.map(ringPath)).join(' ');}
function cutRing(cx,cy,r,kind='rough',seed=1){
 const count=kind==='box'?4:22;
 const pts=Array.from({length:count},(_,i)=>{const a=i/count*Math.PI*2+(kind==='box'?Math.PI/4:0);const jitter=kind==='rough'?0.82+0.18*Math.sin(i*7.31+seed*1.9):1;return[cx+Math.cos(a)*r*jitter,cy+Math.sin(a)*r*(kind==='ellipse'?.68:1)*jitter];});
 return [...pts,pts[0]];
}
function triangles(m){return m.flatMap(poly=>{const clean=poly.map(r=>r.slice(0,-1));const flat=earcut.flatten(clean);const indices=earcut(flat.vertices,flat.holes,2);const points=[];for(let i=0;i<flat.vertices.length;i+=2)points.push([flat.vertices[i],flat.vertices[i+1]]);return Array.from({length:indices.length/3},(_,i)=>[points[indices[i*3]],points[indices[i*3+1]],points[indices[i*3+2]]]);});}
function area(m){return m.reduce((sum,p)=>sum+p.reduce((a,r,i)=>{let s=0;for(let k=0;k<r.length-1;k++)s+=r[k][0]*r[k+1][1]-r[k+1][0]*r[k][1];return a+(i?-1:1)*Math.abs(s/2);},0),0);}
function subtract(cuts){return cuts.length?polygonClipping.difference(WALL,...cuts.map(r=>[[r]])):WALL;}
function Mesh({mesh,wire=false,depth=0,cut,projection=false,stage=0,children}){
 const path=multiPath(mesh),dx=depth*.8,dy=-depth*.5;
 const side=mesh.flatMap(p=>p.flatMap(r=>r.slice(0,-1).map((a,i)=>{const b=r[i+1];return[a,b,[b[0]+dx,b[1]+dy],[a[0]+dx,a[1]+dy]];})));
 return h('g',{className:'motion',transform:projection?'translate(70,15) skewY(-7) scale(.8,1)':'translate(0,0)'},
  depth>0&&h('path',{d:path,transform:`translate(${dx},${dy})`,fill:'#294d42',stroke:'#6cb58e',fillRule:'evenodd'}),
  depth>0&&side.map((p,i)=>h('polygon',{key:'s'+i,points:p.map(p=>p.join(',')).join(' '),fill:i%2?'#467764':'#355e4e',stroke:'#a1caab',strokeWidth:.65})),
  h('path',{d:path,fill:'#78af90',stroke:'#bbebc8',strokeWidth:1.5,fillRule:'evenodd'}),
  wire&&triangles(mesh).map((t,i)=>h('polygon',{key:i,className:'tri',style:{animationDelay:Math.min(i*9,450)+'ms'},points:t.map(p=>p.join(',')).join(' '),fill:'none',stroke:'#203c2e',strokeWidth:.8})),
  cut&&h('path',{d:ringPath(cut),fill:stage===2?'#ffa56e28':'none',stroke:'#ffa56e',strokeWidth:2,strokeDasharray:'7 5'}),children);
}
function App(){
 const [tab,setTab]=useState('pipeline'),[step,setStep]=useState(0),[radius,setRadius]=useState(77),[kind,setKind]=useState('rough'),[depth,setDepth]=useState(36),[impact,setImpact]=useState([255,150]),[cuts,setCuts]=useState([]),[play,setPlay]=useState(false),[seed,setSeed]=useState(1),[uv,setUV]=useState(1),[uvShift,setUVShift]=useState(0),[decoStep,setDecoStep]=useState(0),[showCut,setShowCut]=useState(true),[showFeature,setShowFeature]=useState(true),[broken,setBroken]=useState(false);
 function route(){const id=location.hash.slice(1);const i=steps.findIndex(s=>s.id===id);if(i>=0){setTab('pipeline');setStep(i);}else if(['texture','cutters'].includes(id))setTab('texture');else if(['decorations','cut-decoration','feature-decoration','combined','baseline','result'].includes(id)){setTab('decoration');setDecoStep(id==='feature-decoration'?2:1);setShowCut(id!=='baseline');setShowFeature(id!=='baseline');}else if(id==='comparison')setTab('compare');}
 useEffect(()=>{route();window.addEventListener('hashchange',route);return()=>window.removeEventListener('hashchange',route);},[]);
 useEffect(()=>{if(!play)return;const id=setInterval(()=>setStep(s=>{if(s>=5){setPlay(false);return s;}return s+1;}),1600);return()=>clearInterval(id);},[play]);
 const cutter=useMemo(()=>cutRing(...impact,radius,kind,seed),[impact,radius,kind,seed]);
 const mesh=useMemo(()=>subtract([...cuts,cutter]),[cuts,cutter]);
 const active=steps[step];
 function switchTab(v){setTab(v);setPlay(false);location.hash=v==='pipeline'?steps[step].id:v==='decoration'?'decorations':v==='compare'?'comparison':'texture';}
 function clickWall(e){const p=new DOMPoint(e.clientX,e.clientY).matrixTransform(e.currentTarget.getScreenCTM().inverse());const x=p.x-130,y=p.y-75;if(x<0||x>480||y<0||y>300)return;if(step>=3&&cuts.length<7)setCuts([...cuts,cutter]);setImpact([x,y]);setSeed(s=>s+1);}
 const range=(label,value,set,min,max,unit='')=>h('div',{className:'control'},h('label',null,label,h('b',null,value+unit),h('input',{type:'range',min,max,value,'aria-label':label,onChange:e=>set(Number(e.target.value))})));
 const button=(text,action,pressed,extra={})=>h('button',{type:'button',onClick:action,'aria-pressed':pressed,...extra},text);
 const source=page=>h('a',{className:'source',href:PDF+'#page='+page,target:'_blank',rel:'noreferrer'},'원문 p.'+page+' ↗');
 function pipeline(){return h(React.Fragment,null,
  h('div',{className:'workspace'},h('section',{className:'main-panel'},h('div',{className:'panel-head'},h('h2',null,active.title),source(active.page)),
   h('div',{className:'steps'},steps.map((s,i)=>button(`${i+1} ${s.short}`,()=>{setStep(i);setPlay(false);location.hash=s.id;},step===i,{key:s.id}))),
   h('svg',{className:'stage clickable',viewBox:'0 0 800 450',role:'img','aria-label':'벽의 투영, 절단, 삼각분할, 두께 생성 단계. 아래 버튼으로 구멍 위치를 바꿀 수 있습니다.',onClick:clickWall},h('defs',null,h('pattern',{id:'grid',width:25,height:25,patternUnits:'userSpaceOnUse'},h('path',{d:'M 25 0 L 0 0 0 25',fill:'none',stroke:'#527264',strokeWidth:.5}))),h('g',{transform:'translate(130,75)'},h('rect',{x:-25,y:-25,width:550,height:365,fill:'url(#grid)',opacity:.38}),h(Mesh,{mesh:step>=3?mesh:WALL,wire:step===4,depth:step===0?depth:step===5?depth:0,projection:step===0,cut:step===2?cutter:null,stage:step}),step<3&&h('g',null,h('circle',{cx:impact[0],cy:impact[1],r:6,fill:'#ffa56e'}),h('circle',{cx:impact[0],cy:impact[1],r:14,fill:'none',stroke:'#ffa56e',opacity:.5})))),
   h('div',{className:'legend'},h('span',{style:{'--swatch':'#78af90'}},'남아 있는 표면'),h('span',{style:{'--swatch':'#ffa56e'}},'충돌 / 커터'),h('span',{style:{'--swatch':'#355e4e'}},'생성된 측면')),
   h('div',{className:'caption','aria-live':'polite'},h('strong',null,`${step+1} / 6. `),active.desc)),
   h('aside',{className:'controls'},h('div',{className:'control'},h('label',null,'단계 재생'),h('div',{className:'actions'},button(play?'일시정지':'처음부터 재생',()=>{if(play)setPlay(false);else{setStep(0);setPlay(true);}},play))),
    h('div',{className:'control'},h('label',{htmlFor:'kind'},'커터 모양'),h('select',{id:'kind',value:kind,onChange:e=>setKind(e.target.value)},h('option',{value:'rough'},'불규칙한 경계'),h('option',{value:'ellipse'},'타원'),h('option',{value:'box'},'사각형'))),range('커터 반경',radius,setRadius,25,170,' 단위'),range('벽 두께',depth,setDepth,0,85,' 단위'),
    h('div',{className:'control'},h('label',null,'충돌 위치 / 누적 절단'),h('div',{className:'actions'},button('가장자리',()=>setImpact([455,155])),button('구멍 추가',()=>{if(cuts.length<7)setCuts([...cuts,cutter]);setImpact([95+(seed*83)%280,65+(seed*47)%160]);setSeed(seed+1);setStep(3);})),h('div',{className:'actions',style:{marginTop:8}},button('초기화',()=>{setCuts([]);setImpact([255,150]);setSeed(1);setRadius(77);}))),
    h('div',{className:'stats'},h('div',null,'남은 영역 ',h('b',null,mesh.length),'개'),h('div',null,'내부 구멍 ',h('b',null,mesh.reduce((n,p)=>n+p.length-1,0)),'개'),h('div',null,'앞면 삼각형 ',h('b',null,triangles(mesh).length),'개'),h('div',null,'남은 면적 ',h('b',null,(area(mesh)/144000*100).toFixed(1)),'%'),h('small',null,'수치는 현재 커터 적용 후 기준입니다.')),
    h('p',{className:'note'},'실제 2D 차집합 + Earcut 계산. 투영·두께는 원리 설명용 2.5D 도식입니다. 최대 8개 절단을 표시합니다.'))),
  h('div',{className:'learn'},h('article',null,h('h3',null,'Unreal에 연결하면'),h('p',null,active.ue)),h('article',null,h('h3',null,'직접 확인하기'),h('p',null,'가장자리 → 2D 절단 → 삼각분할 순서로 눌러 보세요. 구멍이 외곽과 만나면 내부 hole이 아니라 외곽선의 홈이 됩니다. '),h('a',{href:'explanation.html#p'+active.page},'이 단계 자세히 읽기 →'))));}
 function texture(){
  const w=85/uv,hh=42/uv,offset=uvShift/100*w;
  const cells=[];for(let row=-1;row<Math.ceil(300/hh)+1;row++)for(let col=-2;col<Math.ceil(480/w)+2;col++){const x=col*w+(row%2)*w/2+offset,y=row*hh;cells.push([[x,y],[x+w,y],[x+w,y+hh],[x,y+hh],[x,y]]);}
  const chosen=cells.filter(r=>{const cx=(r[0][0]+r[2][0])/2,cy=(r[0][1]+r[2][1])/2;return(cx-255)**2+(cy-150)**2<radius**2;});
  const brickCut=chosen.length?polygonClipping.union(...chosen.map(r=>[[r]])):[];
  const result=brickCut.length?polygonClipping.difference(WALL,brickCut):WALL;
  return h('div',{className:'workspace'},h('section',{className:'main-panel'},h('div',{className:'panel-head'},h('h2',null,'그림의 줄눈과 절단선이 함께 움직인다'),source(42)),
   h('svg',{className:'stage',viewBox:'0 0 800 450',role:'img','aria-label':'벽돌 무늬에 맞춰 선택된 벡터 셀이 구멍의 경계를 결정하는 도식'},h('defs',null,h('clipPath',{id:'wallclip'},h('path',{d:multiPath(result),fillRule:'evenodd',clipRule:'evenodd'}))),h('g',{transform:'translate(130,75)'},h('path',{d:multiPath(result),fill:'#78af90',fillRule:'evenodd',stroke:'#c6e9d1'}),h('g',{clipPath:'url(#wallclip)'},cells.map((r,i)=>h('path',{key:i,d:ringPath(r),fill:i%3===0?'#84b59a':'#63987b',stroke:'#294236',strokeWidth:2}))),h('circle',{cx:255,cy:150,r:radius,fill:'none',stroke:'#ffa56e',strokeDasharray:'7 5'}))),
   h('div',{className:'caption'},h('strong',null,'UV → 표면 좌표. '),'타일 크기와 오프셋을 바꾸면 무늬와 벡터 경계가 함께 이동합니다. 주황색 원은 충격 범위이며, 이 데모는 중심점이 범위 안인 벽돌 셀을 선택합니다. 원문의 선택 규칙을 재현한 것은 아닙니다.')),
   h('aside',{className:'controls'},range('UV 반복 배율',uv,setUV,1,4,'×'),range('UV 가로 오프셋',uvShift,setUVShift,0,100,'%'),range('충격 반경',radius,setRadius,25,170,' 단위'),h('p',{className:'note'},'텍스처의 알파를 지우는 방식이 아닙니다. 미리 만든 벡터 패턴을 UV 변환과 일치시키고 실제 표면을 절단합니다.'),h('div',{className:'stats'},'선택한 패턴 셀 ',h('b',null,chosen.length),'개'),h('a',{href:'explanation.html#p42'},'Texture Cutter 해설 →')));
 }
 function decoration(){
  const hole1=cutRing(250,155,65,'rough',3),hole2=cutRing(305,145,102,'rough',8);
  const remain=subtract(decoStep===2?[hole1,hole2]:decoStep===1?[hole1]:[]);
  const stain=cutRing(250,155,112,'rough',12);const cutDeco=polygonClipping.intersection([[stain]],remain);
  const oldPoints=hole1.slice(0,-1).filter((_,i)=>i%2===0);
  return h(React.Fragment,null,h('div',{className:'workspace'},h('section',{className:'main-panel'},h('div',{className:'panel-head'},h('h2',null,'장식은 어디에 붙어 있는가?'),source(decoStep===2?47:46)),
   h('div',{className:'steps'},['0 파괴 전','1 첫 번째 절단','2 겹쳐서 다시 절단'].map((s,i)=>button(s,()=>setDecoStep(i),decoStep===i,{key:s}))),
   h('svg',{className:'stage',viewBox:'0 0 800 450',role:'img','aria-label':'표면 장식은 남은 면에 맞게 잘리고, 기존 경계에 붙은 장식은 경계가 없어진 곳에서 사라집니다.'},h('g',{transform:'translate(130,75)'},h(Mesh,{mesh:remain,depth:32},
    showCut&&h('path',{d:multiPath(cutDeco),fill:'#e3a2dc',opacity:.56,stroke:'#ffd5f9',strokeWidth:1,fillRule:'evenodd'}),
    showFeature&&decoStep>0&&oldPoints.map((p,i)=>{const a=hole1[i*2],b=hole1[i*2+1];const kept=decoStep!==2||polygonClipping.intersection([[[a,b,[b[0]+.01,b[1]+.01],a]]],[[hole2]]).length===0;return kept?h('polygon',{key:i,className:'appear',points:[[p[0]-8,p[1]-5],[p[0]+7,p[1]-10],[p[0]+15,p[1]+12],[p[0]-5,p[1]+9]].map(p=>p.join(',')).join(' '),fill:'#ffa56e',stroke:'#ffd3ad',strokeWidth:1}):null;})))),
   h('div',{className:'legend'},h('span',{style:{'--swatch':'#e3a2dc'}},'면에 붙은 Cut Decoration'),h('span',{style:{'--swatch':'#ffa56e'}},'기존 경계에 붙은 Feature Decoration')),
   h('div',{className:'caption'},h('strong',null,decoStep===2?'기존 장식의 수명 비교. ':'면 기반 vs 경계 기반. '),decoStep===2?'분홍 장식은 남은 부분만 다시 잘립니다. 주황 장식은 연결된 기존 경계가 절단에 닿으면 통째로 제거됩니다. 차이를 보기 위해 새 경계의 장식 생성은 생략했습니다.':'분홍색은 벽에 붙인 그림 조각, 주황색은 구멍의 경계에 붙인 작은 돌 조각이라고 생각하세요. 첫 절단과 재절단을 번갈아 보세요.')),
   h('aside',{className:'controls'},h('div',{className:'control'},h('label',{className:'checkbox'},h('input',{type:'checkbox',checked:showCut,onChange:e=>setShowCut(e.target.checked)}),'표면 장식 표시'),h('label',{className:'checkbox'},h('input',{type:'checkbox',checked:showFeature,onChange:e=>setShowFeature(e.target.checked)}),'경계 장식 표시')),h('p',{className:'note'},'장식 표시를 꺼도 구멍 모양은 같습니다. 장식은 시각적 품질을 높이고, 기본 절단 기하와는 별도로 관리합니다.'),h('a',{href:'explanation.html#p46'},'두 장식의 구현과 수명 →'))),
   h('div',{className:'learn'},h('article',null,h('h3',null,'Cut Decoration'),h('p',null,'표면 좌표에서 Decoration ∩ RemainingSurface를 구하고 UV를 보존해 다시 삼각분할합니다. 이 데모에서도 실제 교집합을 사용합니다.')),h('article',null,h('h3',null,'Feature-Bound Decoration'),h('p',null,'장식이 참조하는 경계의 생존 여부를 추적합니다. 데모는 작은 경계 구간과 새 절단의 겹침으로 제거를 판단합니다. 새 경계에는 별도 규칙으로 장식을 다시 배치할 수 있습니다.'))));
 }
 function comparison(){
  const cut=cutRing(240,150,83,'rough',4),remaining=broken?subtract([cut]):WALL;
  const entries=[['2D 표면 절단','새 경계 생성','평평한 벽의 외곽선과 구멍을 갱신하고 두께를 복원합니다. 얇은 벽의 반복적인 관통 구멍에 맞는 설계입니다.'],['Mesh Boolean','3D 메시 연산','두 3D 메시의 차집합으로 구멍을 만들 수 있습니다. 물리 낙하·지지 구조·장식 수명은 별도로 구성합니다.'],['Chaos Destruction','연결 해제 + 물리','일반적인 Geometry Collection 작업 흐름에서는 미리 나눈 조각의 연결을 끊고 움직입니다. 임의의 새 구멍 경계를 즉석에서 만드는 연산과 역할이 다릅니다.']];
  return h(React.Fragment,null,h('div',{className:'compare-grid'},entries.map((e,i)=>h('article',{className:'compare-card',key:i},h('h2',null,h('span',{className:'tag'},e[1]),e[0]),h('svg',{className:'mini',viewBox:'0 0 560 390',role:'img','aria-label':e[0]+' 개념 비교'},h('g',{transform:'translate(35,40)'},i<2?h(Mesh,{mesh:remaining,depth:i===1?26:0},!broken&&h('path',{d:ringPath(cut),fill:'#ffa56e33',stroke:'#ffa56e',strokeDasharray:'6 4'})):Array.from({length:30},(_,j)=>{const x=j%6,y=Math.floor(j/6),drop=x>=2&&x<=3&&y>=1&&y<=3;return h('rect',{key:j,className:'fragment',x:x*80,y:y*60,width:78,height:58,rx:2,fill:drop?'#ffa56e':'#78af90',stroke:'#b1d3be',style:{transform:broken&&drop?`translate(${(x-2.5)*25}px, ${260+y*12}px) rotate(${(j%3-1)*8}deg)`:'translate(0,0)',opacity:broken&&drop?0:1}});}))),h('p',null,e[2])))),h('div',{className:'compare-footer'},button(broken?'초기 상태로':'같은 위치에 충격',()=>setBroken(!broken)),h('p',null,'왼쪽 둘은 결과 형상의 차이를 설명하는 도식이며 여기서 3D Boolean을 실행하지 않습니다. 오른쪽은 미리 나눈 격자 조각의 개념 애니메이션이며 Chaos 물리 시뮬레이션이 아닙니다.')),h('div',{className:'learn'},h('article',null,h('h3',null,'함께 사용할 수 있습니다'),h('p',null,'맞춤형 2D 절단으로 정적인 벽을 갱신하고, 작은 잔해는 Chaos 강체로 움직일 수 있습니다. 큰 구조물은 Geometry Collection에 맡길 수 있습니다.')),h('article',null,h('h3',null,'세 기술은 완전한 대체재가 아닙니다'),h('p',null,'절단 기하 생성, 구조 연결, 강체 운동은 서로 다른 문제입니다. Unreal의 Chaos Physics 전체와 Chaos Destruction의 Geometry Collection 흐름도 구분해야 합니다.'),h('a',{href:'explanation.html#comparison'},'비교와 구현 선택 읽기 →'))));
 }
 return h(React.Fragment,null,h('header',null,h('div',{className:'brand'},'SURFACE / DESTRUCTION LAB'),h('a',{href:'explanation.html'},'차근차근 해설 읽기 ↗')),h('main',{className:'shell'},h('div',{className:'eyebrow'},'GDC 2016 · RAINBOW SIX: SIEGE · PAGES 34—49'),h('h1',null,'벽이 부서지는 과정을',h('br'),'한 단계씩 들여다보기'),h('p',{className:'intro'},'평면으로 옮기고, 경계를 자르고, 다시 두께를 만듭니다. 원문의 아이디어를 직접 조작하는 교육용 모델입니다.'),h('nav',{className:'tabs','aria-label':'실험 선택'},[['pipeline','01 기하 파이프라인'],['texture','02 재료와 UV'],['decoration','03 장식의 수명'],['compare','04 세 기술 비교']].map(([id,title])=>button(title,()=>switchTab(id),tab===id,{key:id}))),tab==='pipeline'?pipeline():tab==='texture'?texture():tab==='decoration'?decoration():comparison(),h('footer',{className:'footer'},'오프라인 실행 · React 18 · polygon-clipping 0.15.7 · Earcut 2.2.4',h('br'),'원문 구현을 복제한 제품이나 Unreal 플러그인이 아닙니다. 초록색은 남은 면, 주황색은 절단 입력 또는 경계 장식입니다. ',h('a',{href:'explanation.html#sources'},'출처와 검증 범위'))));
}
ReactDOM.createRoot(document.getElementById('root')).render(h(App));
