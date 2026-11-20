# pixel toolkit matching Simutrans' 16-bit renderer and BDF font handling (display/font.cc)
from PIL import Image
import os
HERE=os.path.dirname(os.path.abspath(__file__))
# screenshots (img/, hr/) and results (out/, out2/) live outside the repo: set MOCKUP_DIR
DATA=os.environ.get('MOCKUP_DIR', os.path.join(HERE,'data'))
def q(c):
    if isinstance(c,int): c=((c>>16)&255,(c>>8)&255,c&255)
    r,g,b=c[:3]; r5=r>>3; g6=g>>2; b5=b>>3
    return ((r5<<3)|(r5>>2),(g6<<2)|(g6>>4),(b5<<3)|(b5>>2))
class Font:
    def __init__(s,path):
        s.g={}; fh=0; fd=0
        lines=open(path,encoding='latin-1').read().split('\n'); i=0
        while i<len(lines):
            L=lines[i]
            if L.startswith('FONTBOUNDINGBOX'):
                p=L.split(); fh=int(p[2]); fd=int(p[4])
            elif L.startswith('STARTCHAR'):
                enc=0; adv=-1; bbx=(0,0,0,0); rows=[]
                i+=1
                while not lines[i].startswith('ENDCHAR'):
                    M=lines[i]
                    if M.startswith('ENCODING'): enc=int(M.split()[1])
                    elif M.startswith('DWIDTH'): adv=int(M.split()[1])
                    elif M.startswith('BBX'): bbx=tuple(int(v) for v in M.split()[1:5])
                    elif M.startswith('BITMAP'):
                        w,h,xo,gd=bbx; top=fh+fd-h-gd
                        for y in range(top, min(h+top,fh+4)):
                            i+=1; hx=lines[i].strip()
                            data=int(hx[:2],16)<<8
                            if w>8 and len(hx)>=4: data|=int(hx[2:4],16)
                            data>>=xo
                            if y>=0:
                                for b in range(16):
                                    if data&(0x8000>>b): rows.append((b,y))
                    i+=1
                s.g[enc]=(adv if adv>=0 else bbx[0]+1, rows)
            i+=1
        s.h=fh
        s.g[32]=(max(3,min(fh//2,23)), [])
    def width(s,t):
        return sum(s.g.get(ord(ch),(8,[]))[0] for ch in t)
    def draw(s,im,x,y,t,col,clip=None):
        col=q(col); px=im.load(); W,H=im.size
        for ch in t:
            adv,rows=s.g.get(ord(ch),(8,[]))
            for (bx,by) in rows:
                X=x+bx; Y=y+by
                if clip and not (clip[0]<=X<clip[2] and clip[1]<=Y<clip[3]): continue
                if 0<=X<W and 0<=Y<H: px[X,Y]=col
            x+=adv
        return x
    def ellipsis(s,im,x,y,w,t,col):
        if s.width(t)<=w: s.draw(im,x,y,t,col); return s.width(t)
        e='...'; ew=s.width(e)
        while t and s.width(t)+ew>w: t=t[:-1]
        s.draw(im,x,y,t+e,col); return s.width(t+e)
FONT=Font(os.path.join(HERE,'..','..','simutrans','font','cyr.bdf'))
LS=FONT.h
def box(im,x,y,w,h,col):
    if w<=0 or h<=0: return
    im.paste(q(col),(x,y,x+w,y+h))
