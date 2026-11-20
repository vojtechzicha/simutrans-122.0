import sys,os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from sim import *
import make as M
from hrdata import *
SP=DATA
src=Image.open(SP+'/hr/hr_haltbig0.png').convert('RGB')
BG=q(M.TRAIN_BG)
xs=[x for x in range(60,720) if src.getpixel((x,256))==BG or src.getpixel((x,256))==q(M.TRAIN_STRIPE)]
print('board x', min(xs), max(xs))
ys=[y for y in range(230,600) if src.getpixel((75,y)) in (BG,q(M.TRAIN_STRIPE),q(M.TRAIN_HEAD_BG))]
print('board y', min(ys), max(ys))
X=min(xs); W=max(xs)-X+1; Y=min(ys)
im=src.copy()
tmp=Image.new('RGB',(W,600),(0,0,0))
H,cx,cw=M.train_board(tmp,0,0,W,TRAIN_ROWS,0,with_track=False)
vis=max(ys)-Y+1
im.paste(tmp.crop((0,0,W,vis)),(X,Y))
diff=[(x,y) for y in range(Y,Y+vis) for x in range(X,X+W) if im.getpixel((x,y))!=src.getpixel((x,y))]
print('diff',len(diff), diff[:5], 'cw',cw, 'W',W,'vis',vis)
im.crop((60,40,720,600)).save(SP+'/hr/_check2.png')
