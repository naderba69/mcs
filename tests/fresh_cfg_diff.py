#!/usr/bin/env python3
"""R56: effective-value diff of two profile files (DEFAULT inherited). Usage: fresh_cfg_diff.py OLD NEW"""
import re,sys
def load(path):
    lines=open(path,encoding='utf-8').read().split('\n')
    first=next(i for i,l in enumerate(lines) if l.strip().startswith('['))
    d={}
    for l in lines[:first]:
        s=l.strip()
        if s.startswith('DEFAULT ') and ':' in s:
            k,v=s[8:].split(':',1); d[' '.join(k.upper().split())]=v.split('#')[0].strip()
    profs={}; cur=None
    for l in lines[first:]:
        s=l.strip()
        if not s or s.startswith('#'): continue
        m=re.match(r'^\[\s*(.*?)\s*\]',s)
        if m:
            cur=m.group(1).split('#')[0].strip(); profs[cur]={}; continue
        if cur and ':' in s:
            k,v=s.split(':',1); profs[cur][' '.join(k.upper().split())]=v.split('#')[0].strip()
    eff={}
    for n,kv in profs.items():
        e=dict(d); e.update(kv); eff[n]=e
    return eff
a=load(sys.argv[1]); b=load(sys.argv[2])
assert list(a)==list(b), 'profile list differs'
diffs={}
for n in a:
    for k in set(a[n])|set(b[n]):
        if a[n].get(k)!=b[n].get(k):
            diffs.setdefault(k,[]).append((n,a[n].get(k),b[n].get(k)))
print('profiles:',len(a))
for k,v in sorted(diffs.items()):
    vals=sorted(set((x[1],x[2]) for x in v))
    print(f'{k:24s} differs in {len(v):2d} profile(s): {vals[:6]}')
