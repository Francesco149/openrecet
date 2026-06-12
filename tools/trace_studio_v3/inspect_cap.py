#!/usr/bin/env python3
"""Inspect an orv3 capture container — structured JSON summary (classifier-clean).

Parses the flat record stream (format: format/orv3_format.h) and reports device
params, the resource store (counts/types/bytes), the call-op histogram, and the
state-presence signals that decide whether a single captured frame is
SELF-CONTAINED (re-sets its own transforms/z-state/clear) or relies on INHERITED
device state from earlier frames (the R4 question for sliced single-frame capture).

Usage: inspect_cap.py <cap.bin>
"""
import sys, json, struct

# record types (mirror orv3_format.h)
DEV_PARAMS=1; RES_TEX=2; RES_VB=3; RES_IB=4
SetRenderState=10; SetTextureStageState=11; SetTransform=12; SetMaterial=13
SetTexture=14; SetStreamSource=15; SetIndices=16; SetVertexShader=17
DrawPrimitive=18; DrawIndexedPrimitive=19; DrawPrimitiveUP=20; DrawIndexedPrimitiveUP=21
Clear=22; SetLight=23; LightEnable=24; BeginScene=25; EndScene=26; Present=27; EOF=99

NAMES={DEV_PARAMS:"DEV_PARAMS",RES_TEX:"RES_TEX",RES_VB:"RES_VB",RES_IB:"RES_IB",
  SetRenderState:"SetRenderState",SetTextureStageState:"SetTextureStageState",
  SetTransform:"SetTransform",SetMaterial:"SetMaterial",SetTexture:"SetTexture",
  SetStreamSource:"SetStreamSource",SetIndices:"SetIndices",SetVertexShader:"SetVertexShader",
  DrawPrimitive:"DrawPrimitive",DrawIndexedPrimitive:"DrawIndexedPrimitive",
  DrawPrimitiveUP:"DrawPrimitiveUP",DrawIndexedPrimitiveUP:"DrawIndexedPrimitiveUP",
  Clear:"Clear",SetLight:"SetLight",LightEnable:"LightEnable",
  BeginScene:"BeginScene",EndScene:"EndScene",Present:"Present",EOF:"EOF"}

# D3DTRANSFORMSTATETYPE
XFORM={2:"VIEW",3:"PROJECTION",4:"TEXTURE0",256:"WORLD"}
# a few D3DRENDERSTATETYPE we care about
RS={7:"ZENABLE",8:"FILLMODE",14:"ZWRITEENABLE",15:"ALPHATESTENABLE",22:"CULLMODE",
    23:"ZFUNC",27:"ALPHABLENDENABLE",52:"CLIPPING",136:"LIGHTING",137:"AMBIENT"}

def main():
    f=open(sys.argv[1],"rb"); data=f.read(); f.close()
    p=0
    def u():
        nonlocal p; v=struct.unpack_from("<I",data,p)[0]; p+=4; return v
    def skip(n):
        nonlocal p; p+=n
    magic=u(); ver=u()
    out={"file":sys.argv[1],"bytes":len(data),"magic_ok":magic==0x33565241,"version":ver}
    hist={}; res={"TEX":0,"VB":0,"IB":0,"tex_bytes":0,"vb_bytes":0,"ib_bytes":0,
          "tex_dims":[],"vb_sizes":[],"ib_sizes":[]}
    xforms=set(); rstates={}; clears=[]; tss=set()
    draws={"DrawPrimitive":0,"DrawIndexedPrimitive":0,"DrawPrimitiveUP":0,"DrawIndexedPrimitiveUP":0}
    stream_binds=[]; tex_binds=0; idx_binds=0
    while p < len(data):
        t=u(); hist[NAMES.get(t,t)]=hist.get(NAMES.get(t,t),0)+1
        if t==DEV_PARAMS:
            w,h,bbfmt,depthfmt,windowed,bbcount,presentflags,behavior,interval,adapter,devtype,autods=[u() for _ in range(12)]
            out["dev"]={"w":w,"h":h,"bbfmt":bbfmt,"depthfmt":depthfmt,"windowed":windowed,
              "bbcount":bbcount,"presentflags":presentflags,"behavior":hex(behavior),
              "interval":interval,"adapter":adapter,"devtype":devtype,"autods":autods,
              "NOTE_swapeffect":"NOT CAPTURED (replayer hardcodes DISCARD)"}
        elif t==RES_TEX:
            rid=u(); levels=u(); res["TEX"]+=1
            for l in range(levels):
                lw,lh,lf,rb,dl=u(),u(),u(),u(),u(); skip(dl); res["tex_bytes"]+=dl
                if l==0: res["tex_dims"].append(f"{lw}x{lh}f{lf}")
        elif t==RES_VB:
            rid=u(); size=u(); fvf=u(); dl=u(); skip(dl); res["VB"]+=1; res["vb_bytes"]+=dl; res["vb_sizes"].append((size,hex(fvf)))
        elif t==RES_IB:
            rid=u(); size=u(); fmt=u(); dl=u(); skip(dl); res["IB"]+=1; res["ib_bytes"]+=dl; res["ib_sizes"].append((size,fmt))
        elif t==SetRenderState:
            s=u(); v=u(); rstates[RS.get(s,str(s))]=v
        elif t==SetTextureStageState:
            st=u(); ty=u(); v=u(); tss.add(ty)
        elif t==SetTransform:
            s=u(); skip(64); xforms.add(XFORM.get(s,str(s)))
        elif t==SetMaterial: skip(68)
        elif t==SetTexture: u(); u(); tex_binds+=1
        elif t==SetStreamSource: u(); rid=u(); stride=u(); stream_binds.append(rid)
        elif t==SetIndices: u(); u(); idx_binds+=1
        elif t==SetVertexShader: u()
        elif t==DrawPrimitive: u();u();u(); draws["DrawPrimitive"]+=1
        elif t==DrawIndexedPrimitive: u();u();u();u();u(); draws["DrawIndexedPrimitive"]+=1
        elif t==DrawPrimitiveUP:
            u();u();u(); dl=u(); skip(dl); draws["DrawPrimitiveUP"]+=1
        elif t==DrawIndexedPrimitiveUP:
            u();u();u();u();u(); il=u(); skip(il); u(); vl=u(); skip(vl); draws["DrawIndexedPrimitiveUP"]+=1
        elif t==Clear:
            cnt=u(); skip(cnt*16); flags=u(); color=u(); z=u(); stencil=u()
            clears.append({"flags":flags,"clears_target":bool(flags&1),"clears_zbuffer":bool(flags&2),"clears_stencil":bool(flags&4),"color":hex(color)})
        elif t==SetLight: u(); dl=u(); skip(dl)
        elif t==LightEnable: u(); u()
        elif t in (BeginScene,EndScene): pass
        elif t==Present: u()
        elif t==EOF: break
        else:
            out["PARSE_ERROR"]=f"unknown op {t} at {p-4}"; break
    out["resources"]=res
    out["call_hist"]=hist
    out["draws"]=draws
    out["transforms_set"]=sorted(xforms)
    out["render_states_set"]=rstates
    out["tss_types_set"]=sorted(tss)
    out["clears"]=clears
    out["stream_binds_resids"]=stream_binds
    out["self_contained_signals"]={
      "has_PROJECTION_xform":"PROJECTION" in xforms,
      "has_VIEW_xform":"VIEW" in xforms,
      "has_WORLD_xform":"WORLD" in xforms,
      "has_clear":len(clears)>0,
      "any_clear_zbuffer":any(c["clears_zbuffer"] for c in clears),
      "has_ZENABLE_rs":"ZENABLE" in rstates,
      "n_VB_draws":draws["DrawPrimitive"]+draws["DrawIndexedPrimitive"],
      "n_UP_draws":draws["DrawPrimitiveUP"]+draws["DrawIndexedPrimitiveUP"],
    }
    print(json.dumps(out,indent=1))

if __name__=="__main__": main()
