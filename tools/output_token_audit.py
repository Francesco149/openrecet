#!/usr/bin/env python3
"""Output-token efficiency audit over Claude Code session transcripts.

Answers "where do this project's OUTPUT tokens actually go" — the cost driver for
these sessions. Reproducible measurement behind docs/audits/2026-06-21-output-efficiency.md
and the CLAUDE.md "TERSE MODE" convention. Run before/after a convention change to see
whether thinking% / tools-per-turn / prose-share moved (the revert decision needs this).

Usage:
  nix develop --command python3 tools/output_token_audit.py [PROJECT_TRANSCRIPT_DIR]
Default dir = this project's transcripts. Pass a sibling project's dir to audit it.

Two traps this tool handles (both burned us once):
  1. Claude Code stores ONE content block per jsonl line but stamps EACH line with the
     whole turn's usage.output_tokens. Naive Σ multi-counts by ~blocks/turn (3-5x). We
     DEDUPE billed output by message id.
  2. Thinking text is REDACTED in stored transcripts (block present, content stripped),
     yet billed. So thinking tok = billed - visible(measured chars->tok). Slight overcount
     (uncounted tool-JSON overhead); conclusions robust even at 70%.
"""
import json, glob, os, sys
from collections import Counter

DEFAULT_DIR = "/home/headpats/.claude/projects/-opt-src-openrecet"
CODE_EXT = {'.c','.h','.py','.js','.ts','.sh','.mk','.rs','.go','.cpp','.hpp','.fish','.json','.csv','.bat','.ps1'}
MD_EXT = {'.md','.txt',''}
CATS = ['resp_prose','resp_code','doc_md_prose','doc_md_code','write_code','bash','toolargs_misc']
RATIO = {'resp_prose':4.0,'resp_code':3.5,'doc_md_prose':4.0,'doc_md_code':3.5,'write_code':3.5,'bash':3.5,'toolargs_misc':3.5}

def split_fenced(s):
    pr=co=0; inf=False
    for ln in s.split('\n'):
        if ln.lstrip().startswith('```'): inf=not inf; co+=len(ln)+1; continue
        if inf: co+=len(ln)+1
        else: pr+=len(ln)+1
    return pr,co

def classify_path(p):
    if not p: return 'md'
    base=os.path.basename(p); _,ext=os.path.splitext(base)
    if base=='Makefile' or ext in CODE_EXT: return 'code'
    if ext in MD_EXT: return 'md'
    return 'other'

def collect(d):
    files = sorted(glob.glob(os.path.join(d,'*.jsonl')), key=lambda f: os.path.getmtime(f))
    recent = set(files[-40:])
    turns={}  # id -> per-turn aggregate (billed counted ONCE per id)
    for f in files:
        rec=f in recent
        try: fh=open(f,encoding='utf-8')
        except Exception: continue
        for line in fh:
            line=line.strip()
            if not line: continue
            try: obj=json.loads(line)
            except Exception: continue
            if obj.get('type')!='assistant' and (obj.get('message') or {}).get('role')!='assistant': continue
            msg=obj.get('message') or {}; mid=msg.get('id') or obj.get('uuid'); u=msg.get('usage') or {}
            t=turns.get(mid)
            if t is None:
                t={'out':u.get('output_tokens',0) or 0,'recent':rec,'ntool':0,'writes':0,'text':0,'visible_ch':0}
                for c in CATS: t[c]=0
                turns[mid]=t
            for b in (msg.get('content') or []):
                if not isinstance(b,dict): continue
                bt=b.get('type')
                if bt=='text':
                    s=b.get('text','') or ''; t['text']+=len(s); t['visible_ch']+=len(s)
                    p,c=split_fenced(s); t['resp_prose']+=p; t['resp_code']+=c
                elif bt=='tool_use':
                    t['ntool']+=1; name=b.get('name',''); inp=b.get('input') or {}
                    if name in ('Write','Edit'):
                        s=inp.get('content','') if name=='Write' else inp.get('new_string','')
                        s=s or ''; t['visible_ch']+=len(s); kind=classify_path(inp.get('file_path',''))
                        if kind=='md': p,c=split_fenced(s); t['doc_md_prose']+=p; t['doc_md_code']+=c; t['writes']+=1
                        elif kind=='code': t['write_code']+=len(s); t['writes']+=1
                        else: t['toolargs_misc']+=len(s); t['writes']+=1
                    elif name=='Bash':
                        s=inp.get('command','') or ''; t['bash']+=len(s); t['visible_ch']+=len(s)
                    else:
                        j=json.dumps(inp); t['toolargs_misc']+=len(j); t['visible_ch']+=len(j)
        fh.close()
    return turns, len(files)

def dist_report(title, sel):
    billed=sum(t['out'] for t in sel) or 1; n=len(sel) or 1
    ch={c:sum(t[c] for t in sel) for c in CATS}
    tok={c:ch[c]/RATIO[c] for c in CATS}
    visible=sum(tok.values()); thinking=max(0,billed-visible)
    print(f"\n=== {title} : {n:,} turns, {billed:,.0f} billed output tok (avg {billed/n:,.0f}/turn) ===")
    rows=[('THINKING (reasoning, redacted)',thinking)]+[(c,tok[c]) for c in CATS]
    for name,v in rows: print(f"  {name:32s} {v:>12,.0f} tok  {100*v/billed:5.1f}%")
    comp=tok['resp_prose']+tok['doc_md_prose']
    print(f"  -> compressible PROSE = {100*comp/billed:.1f}% of output (compress 48% => save ~{100*0.48*comp/billed:.1f}%)")

def turntype_report(sel):
    billed=sum(t['out'] for t in sel) or 1
    def cls(t):
        if t['writes']: return 'authoring (Write/Edit)'
        if t['text']>300: return 'analysis/response (prose>300ch)'
        return 'mechanical (tools, little prose)'
    agg={}
    for t in sel:
        a=agg.setdefault(cls(t),{'n':0,'b':0,'vis':0})
        a['n']+=1; a['b']+=t['out']; a['vis']+=t['visible_ch']/3.8
    print(f"\n=== where thinking goes (turn type) ===")
    for c,a in sorted(agg.items(),key=lambda kv:-kv[1]['b']):
        think=max(0,a['b']-a['vis'])
        print(f"  {c:36s} {a['n']:>7,} turns  {100*a['b']/billed:5.1f}% out  think%row={100*think/(a['b'] or 1):5.1f}%")

def shape_report(sel):
    billed=sum(t['out'] for t in sel) or 1; n=len(sel) or 1
    d=Counter(t['ntool'] for t in sel)
    avg=sum(t['ntool'] for t in sel)/n
    single=100*d.get(1,0)/n
    m1=[t for t in sel if t['ntool']==1 and not t['writes'] and t['text']<=300]
    b1=sum(t['out'] for t in m1)
    print(f"\n=== turn shape (batching headroom) ===")
    print(f"  avg tool-calls/turn = {avg:.2f}   single-tool turns = {single:.1f}%")
    print(f"  single-tool MECHANICAL turns = {len(m1):,} ({100*len(m1)/n:.1f}% of turns), {100*b1/billed:.1f}% of output")
    print(f"  -> halving those (batch pairs) ~saves {100*0.5*b1/billed:.1f}% of output (upper bound; dependency chains can't batch)")

if __name__=='__main__':
    d=sys.argv[1] if len(sys.argv)>1 else DEFAULT_DIR
    turns,nf=collect(d)
    allt=list(turns.values())
    print(f"audit dir: {d}\nfiles: {nf}   unique assistant turns: {len(allt):,}")
    dist_report("ALL SESSIONS", allt)
    dist_report("RECENT 40 SESSIONS", [t for t in allt if t['recent']])
    turntype_report(allt)
    shape_report(allt)
