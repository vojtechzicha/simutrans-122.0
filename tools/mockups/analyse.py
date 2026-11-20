import json, re, sys, collections, pickle
SP=sys.argv[1]
d=json.load(open(SP+'/czr.json'))
stops={s['id']:s for s in d['stops']}
REG,TERM,ALLOFF,OLOAD,OUNLOAD,HOLD=0,1,2,3,4,5
def loads(e): return e['stop_type'] not in (ALLOFF,OUNLOAD,HOLD)
def arrives(e): return e['stop_type'] not in (OLOAD,HOLD)
def line_no(name):
    m=re.match(r'^\S+\s+(?:[!<{][^\s]*\s+)*\[([^\]]+)\]',name)
    if m: return m.group(1)
    m=re.match(r'^\[([^\]]+)\]',name)
    if m: return m.group(1)
    m=re.match(r'^(\d{6})[A-Z]?\b',name)
    if m: return str(int(m.group(1)[-3:]))
    m=re.match(r'^([A-Za-z]+)(\d{1,2})(?:[/A-Za-z]|\s|$)',name)
    if m: return m.group(1)+str(int(m.group(2)))
    m=re.match(r'^([A-Za-z]+)',name)
    return '('+(m.group(1) if m else '?')+')'
def short(name, train):
    parts=[p.strip() for p in name.split(' / ')]
    pick=None
    if train:
        for p in parts:
            if '<' in p: pick=p; break
        if not pick:
            for p in parts:
                if ',' not in p: pick=p; break
    else:
        for p in parts:
            if ',' in re.sub(r'\[[^\]]*\]|\([^)]*\)|<[^>]*>','',p): pick=p; break
    pick=pick or parts[0]
    pick=re.sub(r'<[^>]*>|\[[^\]]*\]','',pick)
    pick=re.sub(r'\((?:[A-Z][A-Za-z]*\d+[A-Za-z]*,?\s*)+\)','',pick)
    return re.sub(r'\s+',' ',pick).strip(' ,')
# rows: per halt, tiles grouped into straight runs
def rows_of(halt):
    T=set((t['x'],t['y'],t['z']) for t in halt['tiles'])
    row={}
    for (x,y,z) in T:
        # run along x
        a=x
        while (a-1,y,z) in T: a-=1
        b=x
        while (b+1,y,z) in T: b+=1
        c=y
        while (x,c-1,z) in T: c-=1
        e=y
        while (x,e+1,z) in T: e+=1
        if (b-a)>=(e-c): row[(x,y,z)]=('x',y,z,a,b)
        else: row[(x,y,z)]=('y',x,z,c,e)
    return row
plat=collections.defaultdict(lambda: collections.defaultdict(set))   # (halt,row) -> next_halt -> set(lineno)
tile=collections.defaultdict(lambda: collections.defaultdict(set))   # (halt,tilepos) -> next -> lines
kind={}
exitonly=collections.defaultdict(set)
rowcache={}
nlines=0
for l in d['lines']:
    if l['type'] not in ('rail','road','tram'): continue
    if 0 not in l['goods_categories']: continue
    if not l['convoy_ids']: continue
    E=l['schedule']['entries']; n=len(E)
    if n<2: continue
    nlines+=1
    no=l['schedule']['entries'][0]['board']['line_shown'] or line_no(l['name'])
    for i,e in enumerate(E):
        h=e['halt_id']
        if h is None or e['depot']: continue
        p=(e['pos']['x'],e['pos']['y'],e['pos']['z'])
        if h not in rowcache: rowcache[h]=rows_of(stops[h])
        r=rowcache[h].get(p,('t',)+p)
        kind[(h,r)]=l['type']
        if not loads(e):
            exitonly[(h,r)].add(no); continue
        # skip if next halt entry is the same halt (arrival track then departure track)
        j=(i+1)%n; nxt=None; k=0
        while k<n:
            f=E[j]
            if f['halt_id'] is not None and not f['depot'] and f['stop_type']!=HOLD:
                nxt=f; break
            j=(j+1)%n; k+=1
        if nxt is None or nxt['halt_id']==h: continue
        if not arrives(nxt):
            pass
        eno=e['board']['line_shown'] if e['board']['line_shown'] not in (None,) else no
        plat[(h,r)][nxt['halt_id']].add(eno if eno!='' else '(none)')
        tile[(h,p)][nxt['halt_id']].add(eno)
print('passenger lines with convoys', nlines)
for wt in ('rail','road','tram'):
    P=[(k,v) for k,v in plat.items() if kind[k]==wt]
    nl=collections.Counter(len(set().union(*v.values())) for k,v in P)
    ng=collections.Counter(len(v) for k,v in P)
    print(wt,'platforms',len(P)); print('  lines per platform',sorted(nl.items())); print('  next-stop groups per platform',sorted(ng.items()))
    # halts by number of platforms
    hp=collections.Counter(k[0] for k,v in P)
    print('  platforms per stop',sorted(collections.Counter(hp.values()).items()))
pickle.dump((dict((k,dict(v)) for k,v in plat.items()),kind,dict(exitonly)),open(SP+'/plat.pkl','wb'))
def show(hid):
    s=stops[hid]; print('==',s['name'][:110])
    for (h,r),v in sorted(plat.items(), key=lambda kv: str(kv[0])):
        if h!=hid: continue
        print('  ',kind[(h,r)],r, ' | '.join(','.join(sorted(ls))+' > '+short(stops[nh]['name'], kind[(h,r)]=='rail') for nh,ls in v.items()))
    for (h,r),v in exitonly.items():
        if h==hid and (h,r) not in plat: print('   exit only',r,sorted(v))
for q in sys.argv[2:]:
    for s in d['stops']:
        if s['name'].startswith(q): show(s['id']); break
