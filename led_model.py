# 3D sticker model of the 2x2x2, used to derive the LED net tables in cube.S
# and the checksums make ledtest expects; usage: python3 led_model.py
import random, itertools
src=[[1,4,2,0,3,5,6],[0,1,2,4,5,6,3],[0,2,5,3,1,4,6]]
tw=[[1,2,0,2,1,0,0],[0,0,0,1,2,1,2],[0,0,0,0,0,0,0]]
# internal positions 0..6 and the fixed corner as 7
POS=[(1,1,1),(1,-1,1),(-1,-1,1),(1,1,-1),(1,-1,-1),(-1,-1,-1),(-1,1,-1),(-1,1,1)]
FACE={(0,1,0):'U',(0,-1,0):'D',(1,0,0):'R',(-1,0,0):'L',(0,0,1):'F',(0,0,-1):'B'}
def rot(v,f):
    x,y,z=v
    if f==0: return (x,z,-y)      # R: about +x, clockwise from +x
    if f==1: return (-y,x,z)      # B: clockwise from -z
    return (z,y,-x)               # D: clockwise from -y
def inlayer(v,f): return [v[0]==1, v[2]==-1, v[1]==-1][f]
def geo_solved():
    st={}
    for P in POS:
        for a in range(3):
            n=[0,0,0]; n[a]=P[a]; n=tuple(n)
            st[(P,n)]=FACE[n]
    return st
def geo_move(st,f):
    out={}
    for (P,n),c in st.items():
        if inlayer(P,f): out[(rot(P,f),rot(n,f))]=c
        else: out[(P,n)]=c
    return out
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def slots(P,order):
    # U/D normal first, then the other two in a fixed rotational sense
    ud=(0,P[1],0); a=(P[0],0,0); b=(0,0,P[2])
    # choose order so that cross(ud, s1) points along s2 (order=+1) or opposite
    s1,s2=(a,b) if (cross(ud,a)==b)==(order==1) else (b,a)
    return [ud,s1,s2]
def pm_render(p,o,order,sign):
    st={}
    for i in range(8):
        c=p[i] if i<7 else 7; t=o[i] if i<7 else 0
        S=slots(POS[i],order); H=slots(POS[c],order)
        for k in range(3):
            st[(POS[i],S[k])]=FACE[H[(k+sign*t)%3]]
    return st
def pm_move(p,o,f):
    return [p[src[f][i]] for i in range(7)],[(o[src[f][i]]+tw[f][i])%3 for i in range(7)]

# check the (p, o) model against rotating stickers in 3D
def convention_ok(order, sign):
    for trial in range(200):
        p = list(range(7)); o = [0] * 7; g = geo_solved()
        for _ in range(random.randint(1, 12)):
            f = random.randrange(3)
            p, o = pm_move(p, o, f); g = geo_move(g, f)
        if pm_render(p, o, order, sign) != g:
            return False
    return True

assert convention_ok(-1, 1), 'slot k shows home slot (k + o) mod 3'
ORDER=-1
FIDX={'U':0,'L':1,'F':2,'R':3,'B':4,'D':5}
NORMAL={'U':(0,1,0),'D':(0,-1,0),'R':(1,0,0),'L':(-1,0,0),'F':(0,0,1),'B':(0,0,-1)}
NET={'U':((1,0),[[6,3],[7,0]]),'L':((0,1),[[6,7],[5,2]]),'F':((1,1),[[7,0],[2,1]]),
     'R':((2,1),[[0,3],[1,4]]),'B':((3,1),[[3,6],[4,5]]),'D':((1,2),[[2,1],[5,4]])}
facelets=[]
for name,((fc,fr),grid) in NET.items():
    for r in range(2):
        for c in range(2):
            pos=grid[r][c]
            k=slots(POS[pos],ORDER).index(NORMAL[name])
            facelets.append((fc*9+c*4, fr*7+r*3, pos, k, name))
home=[[FIDX[FACE[n]] for n in slots(POS[c],ORDER)] for c in range(8)]
def render_faces(p,o):
    p=p+[7]; o=o+[0]
    return [home[p[pos]][(k+o[pos])%3] for (_,_,pos,k,_) in facelets]
COL=[0xFFFFFF,0xFF8000,0x00C000,0xFF0000,0x0000FF,0xFFFF00]
def checksum(p,o):
    led=[0]*(35*25)
    for (x0,y0,pos,k,_),f in zip(facelets,render_faces(p,o)):
        for y in range(3):
            for x in range(4): led[(y0+y)*35+x0+x]=COL[f]
    s=0
    for w in led: s=(((s<<1)|(s>>31))&0xFFFFFFFF)^w
    return s-(1<<32) if s>=1<<31 else s
if __name__=='__main__':
    print("# net facelets: x, y of the top-left pixel, position (7 = fixed), slot")
    print("facelets:")
    for x,y,pos,k,name in facelets:
        print(f"        .byte   {x}, {y}, {pos}, {k}".ljust(36)+f"# {name}")
    print("# face of each slot of every cubie at home, 4 bytes per cubie")
    print("home_face:")
    for c in range(8):
        print(f"        .byte   {home[c][0]}, {home[c][1]}, {home[c][2]}, 0")
    def parse(s): return [int(ch)-1 for ch in s[:7]],[int(ch)-1 for ch in s[7:]]
    for s in ["12345671111111","23165742131312","21345671111111"]:
        print("#",s,render_faces(*parse(s)),checksum(*parse(s)))
