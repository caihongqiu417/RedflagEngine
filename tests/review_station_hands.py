"""从原始逐帧数据核对库存、生命、手与物体的距离。可另外将真实窗口截图按游戏时间剪成十章。"""
import argparse
import bisect
import gzip
import hashlib
import io
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile
import wave
import zipfile

import numpy as np
from PIL import Image,ImageDraw,ImageFont
import yaml

WHO={"player":"玩家","watt":"瓦特"}
ACTIONS={"pickup":"拾起医疗包","give":"递给队友","repair":"修理发电机","revive":"扶起队友","heal":"使用医疗包"}
CLIPS={"pickup":"Station-Pickup","give":"Station-Give","repair":"Station-Repair","revive":"Station-Revive","heal":"Station-UseMedkit"}
IDS={"player":"1101","watt":"1102"}
RATE=24


def dump(path,data):
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+"\n",encoding="utf8")


class ModelSurface:
    """以最近三角形表面而非物体中心测距；只筛掉不可能比当前上界更近的三角形。"""
    def __init__(self,glb,transform):
        import trimesh
        from scipy.spatial import cKDTree
        mesh=trimesh.load_scene(io.BytesIO(glb),file_type="glb",process=False).to_geometry()
        m=trimesh.transformations.euler_matrix(*transform["Rotation"],axes="sxyz")
        m[:3,:3]=m[:3,:3]@np.diag(transform["Scale"]);m[:3,3]=transform["Translation"]
        mesh.apply_transform(m)
        self.tri=mesh.triangles
        self.centers=self.tri.mean(axis=1)
        self.radius=np.linalg.norm(self.tri-self.centers[:,None,:],axis=2).max()
        self.tree=cKDTree(self.centers)

    def distance(self,point):
        from trimesh.triangles import closest_point
        point=np.asarray(point)
        upper,j=self.tree.query(point)
        batch=self.tri[self.tree.query_ball_point(point,upper+self.radius+1e-9)]
        feet=closest_point(batch,np.repeat(point[None,:],len(batch),axis=0))
        return float(np.linalg.norm(feet-point,axis=1).min())


def review(folder,package):
    t=json.loads((folder/"take.json").read_bytes())
    fs=[json.loads(row) for row in gzip.open(folder/"frames.jsonl.gz","rt",encoding="utf8")]
    records=[{a["id"]:a for a in f["actors"]} for f in fs]
    action=t["action"]; ident=IDS[t["actor"]]; other="1102" if ident=="1101" else "1101"
    def state(actors):
        kits=lambda uid:actors[uid].get("inventory",{}).get("medkit",0)
        if action=="give":return [kits(ident),kits(other)]
        if action=="pickup":return [kits(ident),"1112" in actors]
        if action=="heal":return [kits(ident),actors[ident]["health"]]
        return [actors["1110" if action=="repair" else other]["health"]]
    before=state(records[0]);states=[state(r) for r in records]
    def did(s):
        if action=="give":return s==[before[0]-1,before[1]+1]
        if action=="pickup":return s==[before[0]+1,False]
        if action=="heal":return s==[before[0]-1,min(100,before[1]+50)]
        return s[0]>0
    tick=next((i for i,s in enumerate(states) if did(s)),None)
    changes=[{"seq":fs[i]["seq"],"state":s} for i,s in enumerate(states) if i==0 or s!=states[i-1]]
    surface=None
    if action in ("pickup","repair"):
        target="1112" if action=="pickup" else "1110"
        scene=yaml.safe_load(package.read("assets/scenes/Watt-Station.redflag"))
        e=next(e for e in scene["Entities"] if str(e["Entity"])==target)
        e.update(t["scene"].get("entities",{}).get(target,{}))
        surface=ModelSurface(package.read(e["ModelRendererComponent"]["FilePath"]),e["TransformComponent"])
    def measure(i):
        a=records[i][ident];b=records[i][other];j=a["animation"]["nodes"];k=b["animation"]["nodes"]
        contact=a.get("contact"); legacy=not any("contact" in r[ident] for r in records)
        p=np.array(j["f_middle.01.L"])
        if legacy:p=(p+j["hand.L"])*.5
        row=dict(seq=fs[i]["seq"],clip=a["animation"]["clip"],clip_s=a["animation"]["time"],receiver_clip=b["animation"]["clip"])
        if contact:row["contact"]=contact
        row["hand_world"]=np.round(p,6).tolist()
        if surface:row["mesh_surface_m"]=round(surface.distance(p),5)
        if action in ("give","revive"):
            palm=np.array(k["f_middle.01.L"]) if action=="give" else np.array(k["hand.L"])
            if legacy and action=="give":palm=(palm+k["hand.L"])*.5
            row["hand_to_hand_m"]=round(float(np.linalg.norm(p-palm)),5)
        if action=="heal":row["finger_to_chest_joint_m"]=round(float(np.linalg.norm(p-j["chest"])),5)
        if b.get("contact"):row["receiver_contact"]=b["contact"]
        return row
    contact=[i for i,r in enumerate(records) if r[ident].get("contact")]
    clips=[];events=[];choices=[]
    for f,r in zip(fs,records):
        clip=r[ident]["animation"]["clip"]
        if not clips or clips[-1]["clip"]!=clip:clips.append(dict(seq=f["seq"],seconds=f["seconds"],clip=clip))
        for e in f["events"]:
            if e["kind"]=="模型选择":choices.append(dict(seq=f["seq"],text=e["text"]))
            if e["entity"]==ident or e["entity"]==other:events.append(dict(seq=f["seq"],**e))
    witnesses=[] if tick is None else [measure(i) for i in range(max(tick-2,0),min(len(fs),tick+2))]
    # 较长的修理/扶人会先碰到目标，随后持续工作。分别保留首次、最后一次接触与游戏状态更新帧。
    near=[i for i in contact if records[i][ident]["contact"]["distance"]<=.04 and
          (action!="give" or records[i][other].get("contact",{}).get("distance",100)<=.04)]
    touches_before=[i for i in near if tick is None or i<=tick]
    last_touch=touches_before[-1] if touches_before else None
    # 修理与救援会在原地保持接触。用中间一帧看清双手，避免选到已经松手的第一张窗口图。
    steady=touches_before[len(touches_before)//2] if touches_before else None
    active=next((i for i,r in enumerate(records) if r[ident]["animation"]["clip"]==CLIPS[action]),None)
    later=next((i for i in range((tick or 0)+1,len(fs)) if records[i][ident]["animation"]["clip"] in ("Idle-cycle","walking_gun-cycle","walking_nogun-cycle")
                and not records[i][ident].get("contact")),None) if tick is not None else None
    receiver_idle=None
    if action=="revive" and tick is not None:
        receiver_idle=next((i for i in range(tick,len(fs)) if records[i][other]["animation"]["clip"]=="Idle-cycle"),None)
    contact_values=[r[ident]["contact"]["distance"] for r in records if r[ident].get("contact")]
    return dict(actor=t["actor"],action=action,trigger=t["trigger"],instruction=t["instruction"],archive_sha256=t["archive_sha256"],exe_sha256=t["exe_sha256"],
        camera=t.get("camera_view","side"),scene=t["scene"],frames=len(fs),window_frames=len(t["captures"]),changes=changes,
        event_seq=fs[tick]["seq"] if tick is not None else None,recovery_seq=fs[later]["seq"] if later is not None else None,
        receiver_idle_seq=fs[receiver_idle]["seq"] if receiver_idle is not None else None,
        begin_seq=fs[active]["seq"] if active is not None else None,contact_seq=[fs[i]["seq"] for i in contact],
        steady_touch_seq=fs[steady]["seq"] if steady is not None else None,
        first_touch=measure(near[0]) if near else None,last_touch_before_commit=measure(last_touch) if last_touch is not None else None,
        closest_logged_goal_m=round(min(contact_values),6) if contact_values else None,
        events=events,choices=choices,clips=clips,measurements=witnesses),fs,t


def verify(result):
    label=result["actor"]+"-"+result["action"]
    event=result["event_seq"]
    assert event and result["begin_seq"] and result["begin_seq"]<=event, label+" 未出现完整动作"
    assert result["recovery_seq"] and result["recovery_seq"]>event, label+" 未恢复待机或移动"
    assert len(result["changes"])==2,label+" 存在额外的库存或生命变化"
    witness=next(s for s in result["measurements"] if s["seq"]==event)
    contact=witness.get("contact",{})
    assert "commit_distance" in contact and contact["commit_distance"]<=.04, label+" 生效时的手超出4厘米"
    if result["action"]=="give":
        recv=witness.get("receiver_contact",{})
        assert "commit_distance" in recv and recv["commit_distance"]<=.04,label+" 接收方没有伸到交接点"
    if result["action"]=="revive":
        assert result["receiver_idle_seq"] and result["receiver_idle_seq"]>event,label+" 被扶者没有站起来"


def contact_sheets(takes,out):
    header=ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc",23)
    small=ImageFont.truetype("C:/Windows/Fonts/msyh.ttc",15)
    for who,name in WHO.items():
        rows=[item for item in takes if item[1]["actor"]==who]
        if not rows:continue
        paper=Image.new("RGB",(1170,64+len(rows)*241),(244,242,235));draw=ImageDraw.Draw(paper)
        draw.text((14,13),name+"的动作",font=header,fill=(35,42,47))
        draw.text((185,23),"游戏窗口实拍。每幅图下标出游戏帧，画面保留完整的角色和物件。",font=small,fill=(81,89,94))
        for n,(folder,r,fs,t) in enumerate(rows):
            captures=t["captures"];clock={f["seq"]:f["seconds"] for f in fs};start=r["begin_seq"]
            event=r["event_seq"]
            contact=r["steady_touch_seq"] or event
            if event:
                anchor=clock[event];start=clock[start] if start is not None else max(captures[0]["seconds"],anchor-.8)
                finish=max(clock.get(r["recovery_seq"],anchor+.8),clock.get(r["receiver_idle_seq"],0))
                moments=[("伸手前",max(captures[0]["seconds"],start-.22)),("目标接触" if r["last_touch_before_commit"] else "状态生效",clock[contact]),("动作结束后",finish+.25)]
            else:
                pivot=(r["first_touch"] or {}).get("seq") or fs[len(fs)//2]["seq"];anchor=clock[pivot]
                moments=[("操作前",max(captures[0]["seconds"],anchor-.7)),("操作期间",anchor),("状态未改变",anchor+1)]
            y=64+241*n
            for j,(label,target) in enumerate(moments):
                capture=min(captures,key=lambda c:abs(c["seconds"]-target));x=j*390
                im=Image.open(folder/"images"/f"{capture['seq']:05}.jpg").convert("RGB")
                im.thumbnail((384,216));paper.paste(im,(x,y))
                draw.text((x+5,y+217),f"{ACTIONS[r['action']]}  /  {label} · 第 {capture['seq']} 帧",font=small,fill=(34,47,50))
        paper.save(out/(who+"-frames.jpg"),quality=93)


def recorded_sound(folder,start,length):
    rate=16000;data=np.zeros(math.ceil(length*rate),dtype=np.int32)
    file=folder/"media.jsonl"; sounds=[]
    if not file.exists():return data.astype("<i2"),sounds
    rows=[json.loads(l) for l in file.read_bytes().splitlines()]
    for submitted in (r for r in rows if r.get("event")=="submitted"):
        ident=submitted["id"]
        end=next((r for r in rows if r.get("id")==ident and r["event"] in ("stopped","finished") and r.get("device_samples") is not None),None)
        if not end:continue
        with wave.open(str(folder/submitted["file"]),"rb") as f:
            assert (f.getframerate(),f.getnchannels(),f.getsampwidth())==(rate,1,2)
            samples=np.frombuffer(f.readframes(max(0,end["device_samples"])),dtype="<i2")
        offset=round((submitted["seconds"]-start)*rate)
        a=max(0,-offset);b=min(len(samples),len(data)-offset)
        if b>a:
            data[offset+a:offset+b]+=samples[a:b]
            sounds.append(dict(id=ident,seq=submitted["seq"],event=end["event"],sample_start=a,sample_end=b))
    return np.clip(data,-32768,32767).astype("<i2"),sounds


def hand_detail(im,actor,spectator,small):
    hand=actor.get("contact")
    if not hand or not spectator:return
    camera=spectator["TransformComponent"];pitch,yaw,_=camera["Rotation"]
    u,v,w=np.asarray(hand["hand"])-camera["Translation"]
    x=math.cos(yaw)*u-math.sin(yaw)*w;z=math.sin(yaw)*u+math.cos(yaw)*w
    y=math.cos(pitch)*v+math.sin(pitch)*z;z=-math.sin(pitch)*v+math.cos(pitch)*z
    focal=.5*im.height/math.tan(.5*spectator["CameraComponent"]["Camera"]["PerspectiveFov"])
    if z>=0:return
    x=im.width*.5-focal*x/z;y=im.height*.5+focal*y/z
    # 直接裁同一帧。保留周围手腕与物件，读者可以在主画面核对近景的位置。
    if x<62 or y<46 or x>=im.width-62 or y>=im.height-46:return
    crop=im.crop((round(x-62),round(y-46),round(x+62),round(y+46))).resize((223,166),Image.Resampling.LANCZOS)
    im.paste(crop,(12,27));d=ImageDraw.Draw(im)
    d.rectangle((11,26,235,194),outline=(235,232,220),width=2);d.rectangle((11,6,235,26),fill=(25,29,33))
    d.text((17,6),"手部近景 · 同一帧",font=small,fill=(245,243,233))


def movie(takes,out):
    small=ImageFont.truetype("C:/Windows/Fonts/msyh.ttc",18);bold=ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc",26)
    size=(1104,680);meta=[";FFMETADATA1"];samples=[];chapters=[];frames_written=0
    with tempfile.TemporaryDirectory() as tmp:
        tmp=Path(tmp)
        cmd=["ffmpeg","-loglevel","error","-y","-f","rawvideo","-pix_fmt","rgb24","-s","1104x680","-r",str(RATE),"-i","-",
             "-c:v","libx264","-threads","4","-pix_fmt","yuv420p","-crf","19",str(tmp/"v.mp4")]
        pipe=subprocess.Popen(cmd,stdin=subprocess.PIPE)
        for folder,r,fs,t in takes:
            captured=t["captures"]; timeline={f["seq"]:f["seconds"] for f in fs}; seqs=[x["seconds"] for x in captured]
            event=r["event_seq"]; start=next((f["seconds"] for f in fs if f["seq"]>=5),seqs[0]); close=seqs[-1]
            if t["actor"]=="watt" and t.get("instruction") and r["choices"] and r["camera"]!="package original":
                start=max(start,timeline[r["choices"][0]["seq"]]-.27)
            if event:
                close=min(close,max(timeline[event]+1.75,timeline.get(r["recovery_seq"],0)+.65,timeline.get(r["receiver_idle_seq"],0)+.45))
            else:close=min(close,start+3.6)
            media=folder/"media.jsonl"
            if media.exists():
                for line in media.read_bytes().splitlines():
                    end=json.loads(line)
                    if end.get("event") in ("finished","stopped") and end.get("device_samples",0):
                        close=min(seqs[-1],max(close,end["seconds"]+.08))
            title=WHO[t["actor"]]+" · "+ACTIONS[t["action"]]
            first=frames_written;duration=max(1,math.ceil((close-start)*RATE))
            sound,voices=recorded_sound(folder,start,duration/RATE);samples.append(sound)
            frame_actors={f["seq"]:next(a for a in f["actors"] if a["id"]==IDS[t["actor"]]) for f in fs}
            if event:assert start<timeline[event]<=close,folder.name+" 的剪辑没有保留状态生效帧"
            if r["begin_seq"] and event:assert start<=timeline[r["begin_seq"]],folder.name+" 的剪辑从伸手以后才开始"
            art=None;last=None
            for n in range(duration):
                now=start+n/RATE;i=max(0,bisect.bisect_right(seqs,now)-1);c=captured[i]
                if last!=i:
                    im=Image.open(folder/"images"/f"{c['seq']:05}.jpg").convert("RGB").resize((1104,621),Image.Resampling.LANCZOS)
                    hand_detail(im,frame_actors[c["seq"]],t.get("scene",{}).get("spectator"),small)
                    art=Image.new("RGB",size,(25,29,33));art.paste(im);d=ImageDraw.Draw(art);d.text((21,628),title,fill=(255,251,243),font=bold)
                    phase=("状态已更新" if event and c["seq"]>=event else "动作中" if r["begin_seq"] and c["seq"]>=r["begin_seq"] else "接近目标")
                    if not event:phase="本局未发生该项状态变化"
                    d.text((300,640),phase,fill=(156,222,202) if event and c["seq"]>=event else (212,215,218),font=small)
                    d.text((847,640),f"游戏帧 {c['seq']}   {c['seconds']:.2f} s",fill=(212,215,218),font=small);last=i
                pipe.stdin.write(art.tobytes());frames_written+=1
            chapters.append(dict(title=title,capture=folder.name,start=round(first/RATE,3),end=round(frames_written/RATE,3),
                                 original_seconds=[round(start,3),round(close,3)],voices=voices))
            meta+=["[CHAPTER]","TIMEBASE=1/1000",f"START={round(first*1000/RATE)}",f"END={round(frames_written*1000/RATE)}",f"title={title}"]
        pipe.stdin.close();assert pipe.wait()==0
        (tmp/"m.txt").write_text("\n".join(meta)+"\n",encoding="utf8")
        with wave.open(str(tmp/"s.wav"),"wb") as f:
            f.setparams((1,2,16000,0,"NONE","not compressed"));f.writeframes(np.concatenate(samples).tobytes())
        args=["ffmpeg","-loglevel","error","-y","-i",str(tmp/"v.mp4"),"-i",str(tmp/"s.wav"),"-i",str(tmp/"m.txt"),"-map","0:v"]
        if any(c["voices"] for c in chapters):args+=["-map","1:a","-c:a","aac","-b:a","96k"]
        subprocess.run(args+["-map_metadata","2","-map_chapters","2","-c:v","copy","-movflags","+faststart",str(out/"actions.mp4")],check=True)
    dump(out/"chapters.json",chapters)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("archive",type=Path);p.add_argument("raw",type=Path);p.add_argument("output",type=Path)
    p.add_argument("--film",action="store_true");p.add_argument("--verify",action="store_true");a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    takes=[]
    with zipfile.ZipFile(a.archive) as package:
        audio={hashlib.sha256(package.read(name)).hexdigest():name for name in package.namelist()
               if name.startswith("assets/audio/watt/") and name.endswith(".wav")}
        for who in WHO:
            for action in ACTIONS:
                folder=a.raw/(who+"-"+action)
                if not (folder/"take.json").exists():continue
                r,fs,t=review(folder,package);takes.append((folder,r,fs,t))
                if a.verify:verify(r)
                media=folder/"media.jsonl"
                if media.is_file():
                    shutil.copy2(media,a.output/(folder.name+"-audio.jsonl"))
                    r["voice_assets"]={}
                    for wav in folder.glob("speech-*.wav"):
                        digest=hashlib.sha256(wav.read_bytes()).hexdigest()
                        r["voice_assets"][wav.name]={"sha256":digest,"package_file":audio.get(digest)}
                        if a.verify and who=="watt":assert digest in audio,"录音与发行包的实际语音不匹配"
                shutil.copy2(folder/"frames.jsonl.gz",a.output/(folder.name+".jsonl.gz"))
                print(folder.name,r["event_seq"],[(w.get("mesh_surface_m"),w.get("hand_to_hand_m"),
                      w.get("contact",{}).get("distance")) for w in r["measurements"]],flush=True)
    if a.verify:assert len(takes)==10,"两人的五个动作还未全部拍摄"
    dump(a.output/"measurements.json",[r for _,r,_,_ in takes])
    contact_sheets(takes,a.output)
    if a.film:movie(takes,a.output)


if __name__=="__main__":main()
