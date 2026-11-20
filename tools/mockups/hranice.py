import json, re, sys, collections
SP=sys.argv[1]
d=json.load(open(SP+'/czr.json'))
stops={s['id']:s for s in d['stops']}
def line_no(name):
    m=re.match(r'^\S+\s+(?:[!<{][^\s]*\s+)*\[([^\]]+)\]',name)
    if m: return m.group(1)
    m=re.match(r'^(\d{6})[A-Z]?\b',name)
    if m: return str(int(m.group(1)[-3:]))
    m=re.match(r'^([A-Za-z]+)(\d{1,2})(?:[/A-Za-z]|\s|$)',name)
    if m: return m.group(1)+str(int(m.group(2)))
    return name.split()[0]
def short(name, train, town=None):
    parts=[p.strip() for p in name.split(' / ')]
    strip=lambda p: re.sub(r'\s+',' ',re.sub(r'\([A-Z][A-Za-z]*\d*[A-Za-z]*(?:\s*,\s*[A-Z][A-Za-z]*\d*[A-Za-z]*)*\)','',re.sub(r'<[^>]*>|\[[^\]]*\]','',p))).strip(' ,')
    pick=None
    if train:
        for p in parts:
            if '<' in p: pick=p; break
        if not pick:
            for p in parts:
                if ',' not in strip(p): pick=p; break
    else:
        for p in parts:
            if ',' in strip(p): pick=p; break
    s=strip(pick or parts[0]); s=re.sub(r'\s+,',',',s)
    if town and s.startswith(town+','): s=s[len(town)+1:].strip()
    return s
H=int(sys.argv[2]); town=sys.argv[3]
g=collections.defaultdict(lambda: collections.defaultdict(list)); arr=collections.defaultdict(set); dest=collections.defaultdict(dict)
for l in d['lines']:
    if not l['convoy_ids'] or 0 not in l['goods_categories']: continue
    E=l['schedule']['entries']; n=len(E); no=line_no(l['name']); train=l['type']=='rail'
    for i,e in enumerate(E):
        if e['halt_id']!=H: continue
        p=(e['pos']['x'],e['pos']['y'])
        eno=e['board']['line_shown'] if e['board']['line_shown'] is not None else no
        if e['stop_type'] in (2,4,5): arr[p].add(eno); continue
        j=(i+1)%n
        while E[j]['halt_id'] is None or E[j]['stop_type']==5: j=(j+1)%n
        if E[j]['halt_id']==H: continue
        # destination: next terminal/alloff, else last before returning
        k=j; dst=None; c=0
        while c<n:
            f=E[k]
            if f['halt_id'] and f['stop_type'] in (1,2): dst=f['halt_id']; break
            k=(k+1)%n; c+=1
        nh=E[j]['halt_id']
        g[p][nh].append((eno, l['type'], short(stops[dst]['name'],train,town) if dst else '?', l['name'][:40]))
for p in sorted(g, key=lambda p:(p[1],p[0])):
    print(p)
    for nh,ls in g[p].items():
        t=ls[0][1]=='rail'
        print('    >',short(stops[nh]['name'],t,town),'||', '; '.join('%s→%s'%(a,c) for a,b,c,dd in sorted(set(ls))))
    if p in arr: print('    arr', sorted(arr[p]))
for p in arr:
    if p not in g: print(p,'arrivals only',sorted(arr[p]))
