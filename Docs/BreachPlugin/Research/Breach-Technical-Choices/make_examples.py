from pathlib import Path
import sys,math,random,json,html
import numpy as np
sys.path.insert(0,str(Path(__file__).parent/'python-packages'))
import shapely
from shapely.geometry import Point,Polygon,MultiPoint,LineString,box
from shapely.ops import unary_union,polygonize,triangulate
from shapely.affinity import scale

OUT=Path(__file__).parent if Path(__file__).parent.name=='Breach-Technical-Choices' else Path('F:/Dev/BreachTest/Docs/BreachPlugin/Research/Breach-Technical-Choices')
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'figures').mkdir(exist_ok=True)
metrics={}
COLORS=['#6ca9a0','#e7b26c','#91a7cb','#bf8caa','#a5bd81','#6eabbc','#cc9585','#b6ad7f']
def parts(g):
    if g.is_empty:return []
    if g.geom_type=='Polygon':return [g]
    return [p for x in getattr(g,'geoms',[]) for p in parts(x)]
def path(g,fill='#79bdb0',stroke='#17695e',width=1.1):
    s=''
    for p in parts(g):
        rings=[p.exterior,*p.interiors]
        d=' '.join('M '+' L '.join(f'{x:.4f},{y:.4f}' for x,y in r.coords)+' Z' for r in rings)
        s+=f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{width}" fill-rule="evenodd"/>'
    return s
def dots(pts):return ''.join(f'<circle cx="{x}" cy="{y}" r="3.2" fill="#da6334" stroke="white" stroke-width="1"/>' for x,y in pts)
def panel(title,g=None,pts=(),back='',extra=''):
    return {'title':title,'content':back+(path(g) if g is not None else '')+extra+dots(pts)}
def sheet(name,panels,cols=3):
    rows=math.ceil(len(panels)/cols);w=cols*300;h=rows*235
    s=f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" role="img" aria-label="{name}"><rect width="100%" height="100%" fill="#f6f4ed"/>'
    for i,p in enumerate(panels):
        x=(i%cols)*300;y=(i//cols)*235
        s+=f'<g transform="translate({x},{y})"><rect x="8" y="8" width="284" height="219" rx="8" fill="white" stroke="#d9ded8"/><text x="20" y="32" font-family="Malgun Gothic,sans-serif" font-size="14" font-weight="bold" fill="#193b3b">{html.escape(p["title"])}</text><g transform="translate(25,42)">'
        for gx in range(0,241,30):s+=f'<path d="M{gx} 0V165" stroke="#e9efeb" stroke-width=".5"/>'
        for gy in range(0,166,30):s+=f'<path d="M0 {gy}H240" stroke="#e9efeb" stroke-width=".5"/>'
        s+=p['content']+'</g></g>'
    s+='</svg>'
    (OUT/'figures'/f'{name}.svg').write_text(s,encoding='utf-8')

def field(pts,R=45,T=1.1,h=2,rx=None,ry=None):
    xs=np.arange(-20,261,h);ys=np.arange(-20,191,h)
    X,Y=np.meshgrid(xs,ys);D=np.zeros(X.shape)
    for x,y in pts:
        q=(X-x)**2/(rx or R)**2+(Y-y)**2/(ry or R)**2
        D+=np.maximum(0,1-q)**2*(q<1)
    vals=D-T; lines=[]
    # Marching squares: deterministic center-value choice for saddle cases.
    for j in range(len(ys)-1):
      for i in range(len(xs)-1):
        v=[vals[j,i],vals[j,i+1],vals[j+1,i+1],vals[j+1,i]]
        xy=[(xs[i],ys[j]),(xs[i+1],ys[j]),(xs[i+1],ys[j+1]),(xs[i],ys[j+1])]
        edges=[]
        for e in range(4):
            f=(e+1)%4
            if (v[e]>=0)!=(v[f]>=0):
                t=v[e]/(v[e]-v[f]); a,b=xy[e],xy[f]
                edges.append((e,(round(float(a[0]+t*(b[0]-a[0])),8),round(float(a[1]+t*(b[1]-a[1])),8))))
        if len(edges)==2:lines.append(LineString([edges[0][1],edges[1][1]]))
        elif len(edges)==4:
            ep=dict(edges)
            pairs=[(0,1),(2,3)] if (v[0]>=0)==(sum(v)>=0) else [(0,3),(1,2)]
            lines.extend(LineString([ep[a],ep[b]]) for a,b in pairs)
    candidates=list(polygonize(lines));keep=[]
    for g in candidates:
        p=g.representative_point();total=0
        for x,y in pts:
            q=(p.x-x)**2/(rx or R)**2+(p.y-y)**2/(ry or R)**2
            total+=max(0,1-q)**2 if q<1 else 0
        if total>=T:keep.append(g)
    return unary_union(keep),int(D.size)

pts=[(65,45),(61,74),(68,103),(91,125),(119,130),(150,124),(173,100),(179,73),(172,44)]
disks=unary_union([Point(p).buffer(22,quad_segs=12) for p in pts])
hull=MultiPoint(pts).convex_hull.buffer(10,quad_segs=8)
tris=[]
for tr in triangulate(MultiPoint(pts)):
    co=list(tr.exterior.coords)[:3]
    lengths=[math.dist(co[i],co[(i+1)%3]) for i in range(3)]
    radius=math.prod(lengths)/(4*tr.area)
    if radius<=48:tris.append(tr)
alpha=unary_union(tris).buffer(8,quad_segs=6)
dmg,_=field(pts)
closed=disks.buffer(12,quad_segs=8).buffer(-12,quad_segs=8)
sheet('outlines',[panel('동일 탄착점 · 입력',None,pts),panel('볼록 껍질 + 10',hull,pts),panel('반경 22 원들의 합집합',disks,pts),panel('α R48 · 면적 없음',alpha,pts),panel('합집합 + Closing 12',closed,pts),panel('피해장 R45 / T1.1',dmg,pts)])

pp=[]
for count in [1,3,6]:
    g,_=field([(120,85)]*count,R=45,T=2)
    pp.append(panel(f'동일 위치 {count}발 · 피해장',g,[(120,85)]))
    metrics[f'repeat_{count}_area']=g.area
for h in [2,6,12]:
    g,size=field(pts,h=h)
    pp.append(panel(f'동일 피해장 · 격자 h={h}',g,pts))
    metrics[f'field_h{h}']={'nodes_evaluated':size,'area':g.area,'boundary_vertices':sum(len(p.exterior.coords)-1 for p in parts(g))}
sheet('field-resolution',pp)

# Explicit radius graph vs DBSCAN for a bridge case.
points=[(40,60),(45,65),(40,70),(50,60),(50,70),(185,60),(190,65),(185,70),(195,60),(195,70),(73,65),(97,65),(121,65),(145,65),(165,65)]
eps=25;neighbors=[[j for j,q in enumerate(points) if math.dist(p,q)<=eps] for p in points]
core=[len(n)>=4 for n in neighbors]; labels=[-1]*len(points);c=0
for i in range(len(points)):
    if not core[i] or labels[i]>=0:continue
    labels[i]=c;todo=[i]
    while todo:
        j=todo.pop()
        for k in neighbors[j]:
            if labels[k]<0:
                labels[k]=c
                if core[k]:todo.append(k)
    c+=1
lines=''.join(f'<path d="M{points[i][0]} {points[i][1]}L{points[j][0]} {points[j][1]}" stroke="#9caca3"/>' for i in range(len(points)) for j in neighbors[i] if i<j)
dbdots=''.join(f'<circle cx="{p[0]}" cy="{p[1]}" r="{5 if core[i] else 3.5}" fill="{COLORS[labels[i]] if labels[i]>=0 else "#a5aaa7"}" stroke="#334d45"/>' for i,p in enumerate(points))
graph=unary_union([Point(p).buffer(7) for p in points]).convex_hull
sheet('clustering',[panel('거리 그래프 ε25 · 1개 군집',None,points,extra=lines),panel('DBSCAN ε25 / minPts4',None,extra=dbdots)],cols=2)
metrics['clustering']={'eps':eps,'minPts':4,'dbscan_clusters':c,'noise_points':labels.count(-1),'radius_graph_clusters':1,'points':points}

# Material policy examples are synthetic, not an asset import demonstration.
foot=Point(120,86).buffer(46,quad_segs=20)
bricks=[]
for j in range(6):
  for i in range(-1,6):
    cell=box(i*48+(24 if j%2 else 0),j*28,(i+1)*48+(24 if j%2 else 0),(j+1)*28).intersection(box(0,0,240,165))
    if cell.area>0:bricks.append(cell)
chosen=unary_union([b for b in bricks if b.intersection(foot).area/b.area>=.2])
back=''.join(path(b,'none','#c0c9c1',.65) for b in bricks)
wood,_=field([(120,78)]*4,R=45,T=2,rx=20,ry=70)
woodback=''.join(f'<path d="M{x} 0V165" stroke="#d7c8b3"/>' for x in range(0,241,20))
sheet('material-policies',[panel('범위 그대로 · 기본 원형',foot,[(120,86)],back),panel('겹침 ≥20% 셀 전체 제거',chosen,[(120,86)],back),panel('방향성 피해장 · 세로 결',wood,[(120,78)],woodback)])

def voronoi(sites,bounds,anisotropy=(1,1),weights=None):
    cells=[];sx,sy=anisotropy;weights=weights or [0]*len(sites)
    xmin,ymin,xmax,ymax=bounds
    for i,p in enumerate(sites):
        verts=[(xmin,ymin),(xmax,ymin),(xmax,ymax),(xmin,ymax)]
        for j,q in enumerate(sites):
            if i==j:continue
            a=2*(q[0]-p[0])/sx**2;b=2*(q[1]-p[1])/sy**2
            c=(q[0]**2-p[0]**2)/sx**2+(q[1]**2-p[1]**2)/sy**2+weights[i]-weights[j]
            new=[]
            for k,A in enumerate(verts):
                B=verts[(k+1)%len(verts)];va=a*A[0]+b*A[1]-c;vb=a*B[0]+b*B[1]-c
                if va<=1e-9:new.append(A)
                if (va<=1e-9)!=(vb<=1e-9):
                    t=va/(va-vb);new.append((A[0]+t*(B[0]-A[0]),A[1]+t*(B[1]-A[1])))
            verts=new
            if len(verts)<3:break
        cells.append(Polygon(verts) if len(verts)>=3 else Polygon())
    return cells

boundary=Polygon([(35,30),(90,20),(110,40),(155,20),(200,36),(215,68),(194,90),(210,132),(160,148),(135,129),(95,151),(65,132),(30,140),(42,93),(25,65)])
old_hole=Point(107,89).buffer(12,quad_segs=12)
removed=boundary.difference(old_hole)
rng=random.Random(4829);sites=[]
while len(sites)<18:
    p=(rng.uniform(25,215),rng.uniform(20,151))
    if removed.contains(Point(p)) and all(math.dist(p,q)>18 for q in sites):sites.append(p)
vpan=[]
for name,metric,weights in [('일반 2D · 18 sites',(1,1),None),('방향성 · 세로 축 2.5',(1,2.5),None),('가중 · 일부 seed 우대',(1,1),[700 if i%5==0 else 0 for i in range(18)])]:
    cells=voronoi(sites,(-10,-10,250,180),metric,weights)
    clipped=[c.intersection(removed) for c in cells]
    err=unary_union(clipped).symmetric_difference(removed).area
    overlap=sum(g.area for g in clipped)-unary_union(clipped).area
    assert err<1e-6 and abs(overlap)<1e-6
    vpan.append(panel(name,None,sites,extra=''.join(path(g,COLORS[i%len(COLORS)],'#f4f6ee',.7) for i,g in enumerate(clipped))+path(removed,'none','#263c34',1.3)))
    metrics[name]={'sites':len(sites),'polygon_pieces':sum(len(parts(g)) for g in clipped),'area_error':err,'overlap_area':overlap,'holes_in_removed_region':1}
sheet('voronoi',vpan)
before=box(10,10,230,155).difference(old_hole)
after=before.difference(boundary)
assert before.difference(after).symmetric_difference(removed).area<1e-6
assert after.intersection(boundary).area<1e-6
sheet('removed-region',[panel('절삭 전 벽 · 기존 구멍 포함',before),panel('이번 제거 영역',removed),panel('제거 후 남은 벽',after)])
(OUT/'examples.json').write_text(json.dumps({'notice':'Synthetic deterministic 2D examples; not R6 or Unreal runtime results','seed':4829,'shapely_version':shapely.__version__,'metrics':metrics},ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'figures':len(list((OUT/'figures').glob('*.svg'))),'metrics':metrics},ensure_ascii=False))
