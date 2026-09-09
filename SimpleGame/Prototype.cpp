#include "stdafx.h"
#include "Prototype.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace {
const Color Cyan(.22f,.94f,.91f), Pink(.97f,.26f,.59f), Amber(1,.7f,.3f);
const Color White(.82f,.9f,.94f), Muted(.37f,.5f,.59f), Panel(.025f,.055f,.09f,.96f);
Color Accent(unsigned style) {
    const Color palette[] = {Cyan,Pink,Color(.53f,.45f,1),Amber,Color(.32f,.68f,1)};
    return palette[style%5];
}
bool SameDevice(const Device& a,const Device& b) { return a.valid && b.valid && a.key==b.key && a.type==b.type; }
std::string Number(int64_t n) { return std::to_string(n); }
std::string Decimal(float n) { std::ostringstream out; out << std::fixed << std::setprecision(2) << n; return out.str(); }
}

Prototype::Prototype(Renderer& renderer,const std::filesystem::path& savePath)
    : r_(renderer),savePath_(savePath) {
    player_={105,185};
    world_.Load(savePath_,player_);
    saveAllowed_=!world_.HasSaveWarning();
    if(!saveAllowed_) Notify("SAVE UNREADABLE - ORIGINAL FILE PRESERVED");
    world_.Stream(player_,StreamRadius());
    if(!world_.CanWalk(player_)) {
        const auto key=world_.KeyAt(player_);
        player_={key.x*World::ChunkSize+50,key.y*World::ChunkSize+50};
    }
    camera_=player_;
    target_=world_.NearestDevice(player_);
}
int Prototype::StreamRadius() const {
    const double reach=(r_.Width()/(4*.82*zoom_)+r_.Height()/(4*.43*zoom_)+280/(.86*zoom_));
    return std::clamp(static_cast<int>(std::ceil(reach/World::ChunkSize))+1,2,6);
}
void Prototype::Notify(const std::string& message) { toast_=message; toastTime_=6; }
bool Prototype::Save() {
    if(!saveAllowed_) { Notify("SAVE DISABLED - MOVE THE UNREADABLE SAVE FIRST"); return false; }
    if(!world_.Save(savePath_,player_)) {
        Notify("SAVE FAILED - CHECK FOLDER WRITE ACCESS");
        std::cerr << "Could not save progress: " << savePath_ << '\n';
        return false;
    }
    return true;
}
void Prototype::ReleaseKeys() { keys_.fill(false); hackProgress_=0; }
void Prototype::Key(unsigned char key,bool down) {
    if(key>='A'&&key<='Z') key=static_cast<unsigned char>(key+32);
    const bool pressed=down&&!keys_[key];
    keys_[key]=down;
    if(!pressed) return;
    if(key=='\t') scan_=!scan_;
    if(key=='p') { paused_=!paused_; hackProgress_=0; }
    if(key=='+'||key=='=') zoom_=std::min(1.4f,zoom_+.1f);
    if(key=='-') zoom_=std::max(.7f,zoom_-.1f);
    if(key=='k' && Save()) Notify("PROGRESS SAVED");
    auto& effects=r_.PostEffects();
    if(key=='o') { effects.enabled=!effects.enabled; Notify(effects.enabled?"POST EFFECTS ENABLED":"POST EFFECTS DISABLED / ORIGINAL VIEW"); }
    if(key=='b') { effects.bloom=!effects.bloom; Notify(effects.bloom?"BLOOM ENABLED":"BLOOM DISABLED"); }
    if(key=='v') { effects.vignette=!effects.vignette; Notify(effects.vignette?"VIGNETTE ENABLED":"VIGNETTE DISABLED"); }
    if(key=='f') { effects.edgeBlur=!effects.edgeBlur; Notify(effects.edgeBlur?"EDGE BLUR ENABLED":"EDGE BLUR DISABLED"); }
    if(key=='['||key==']') {
        effects.exposure=std::clamp(effects.exposure+(key==']'?.1f:-.1f),.25f,4.f);
        Notify("EXPOSURE / "+Decimal(effects.exposure));
    }
    if(key==','||key=='.') {
        effects.bloomStrength=std::clamp(effects.bloomStrength+(key=='.'?.05f:-.05f),0.f,2.f);
        Notify("BLOOM STRENGTH / "+Decimal(effects.bloomStrength));
    }
    if(key=='1'||key=='2') {
        effects.vignetteStrength=std::clamp(effects.vignetteStrength+(key=='2'?.05f:-.05f),0.f,.95f);
        Notify("VIGNETTE STRENGTH / "+Decimal(effects.vignetteStrength));
    }
    if(key=='3'||key=='4') {
        effects.edgeBlurStrength=std::clamp(effects.edgeBlurStrength+(key=='4'?.05f:-.05f),0.f,1.f);
        Notify("EDGE BLUR STRENGTH / "+Decimal(effects.edgeBlurStrength));
    }
}
void Prototype::Update(float dt) {
    if(paused_) return;
    dt=std::clamp(dt,0.f,.05f);
    time_+=dt; toastTime_=std::max(0.f,toastTime_-dt); saveTime_+=dt;
    lockdown_=std::max(0.f,lockdown_-dt);
    float sx=float(keys_['d'])-float(keys_['a']),sy=float(keys_['s'])-float(keys_['w']);
    float length=std::sqrt(sx*sx+sy*sy);
    moving_=length>0;
    if(moving_) {
        sx/=length; sy/=length;
        const double speed=keys_[' ' ]?230:150;
        // WASD follows screen axes; small substeps prevent tunnelling through walls.
        const double dx=(sx+sy)*.70710678*speed*dt,dy=(sy-sx)*.70710678*speed*dt;
        const int steps=std::max(1,static_cast<int>(std::ceil(std::max(std::abs(dx),std::abs(dy))/4)));
        for(int i=0;i<steps;++i) {
            WorldPoint next{player_.x+dx/steps,player_.y};
            if(world_.CanWalk(next)) player_=next;
            next={player_.x,player_.y+dy/steps};
            if(world_.CanWalk(next)) player_=next;
        }
    }
    world_.Stream(player_,StreamRadius());
    const double follow=1-std::exp(-dt*8);
    camera_.x+=(player_.x-camera_.x)*follow; camera_.y+=(player_.y-camera_.y)*follow;
    target_=world_.NearestDevice(player_);
    const bool wantsHack=keys_['e']&&target_.valid&&!moving_&&lockdown_<=0;
    if(wantsHack) {
        if(!SameDevice(target_,hacking_)) { hacking_=target_; hackProgress_=0; }
        hackProgress_+=dt/(world_.Credits()>=150?1.35f:1.8f);
        if(hackProgress_>=1) { Hack(target_); hackProgress_=0; keys_['e']=false; }
    } else { hackProgress_=0; hacking_.valid=false; }
    bool watched=false;
    const auto key=world_.KeyAt(player_);
    const auto camera=world_.Devices(key)[1];
    if(world_.Powered(key)&&!world_.Changes(key).cameraOff&&
        std::hypot(player_.x-camera.position.x,player_.y-camera.position.y)<155) watched=true;
    if(watched && wantsHack) trace_=std::min(100.f,trace_+dt*9);
    else if(!wantsHack) trace_=std::max(0.f,trace_-dt*3);
    if(trace_>=100) { lockdown_=8; trace_=65; hackProgress_=0; Notify("CONNECTION LOCKED - WAIT 8 SECONDS"); }
    const auto& changes=world_.Changes(key);
    const double roomX=key.x*World::ChunkSize+235,roomY=key.y*World::ChunkSize+210;
    if(changes.doorOpen&&!changes.dataTaken&&std::hypot(player_.x-roomX,player_.y-roomY)<24) {
        world_.Change(key).dataTaken=true; Notify("ARCHIVE RECOVERED / +75 CR"); Save();
    }
    if(saveTime_>20) { Save(); saveTime_=0; }
}
void Prototype::Hack(const Device& target) {
    if(target.type==DeviceType::Power) {
        if(world_.Powered(target.key)) { Notify("RELAY ONLINE - NO REPAIR REQUIRED"); return; }
        world_.Change(target.key).eventSolved=true;
        Notify("POWER RESTORED / +150 CR / FASTER UPLINK UNLOCKED");
    } else if(target.type==DeviceType::Camera) {
        if(!world_.Powered(target.key)) { Notify("CAMERA HAS NO POWER - RESTORE THE RELAY FIRST"); return; }
        auto& change=world_.Change(target.key); change.cameraOff=!change.cameraOff;
        Notify(change.cameraOff?"CAMERA LOOP ACTIVE - LOCAL TRACE SUPPRESSED":"CAMERA SURVEILLANCE RESTORED");
        trace_=std::min(100.f,trace_+14);
    } else {
        if(!world_.Powered(target.key)) { Notify("DOOR OFFLINE - RESTORE THE RELAY FIRST"); return; }
        auto& change=world_.Change(target.key);
        const double x=target.key.x*World::ChunkSize+145,y=target.key.y*World::ChunkSize+145;
        if(change.doorOpen && player_.x>x-8 && player_.x<x+188 && player_.y>y-8 && player_.y<y+178) {
            Notify("STEP OUT OF THE ROOM BEFORE LOCKING THE DOOR"); return;
        }
        change.doorOpen=!change.doorOpen;
        Notify(change.doorOpen?"ACCESS GRANTED - ENTER AND RECOVER THE ARCHIVE":"ACCESS DOOR LOCKED");
        trace_=std::min(100.f,trace_+22);
    }
    Save();
}
Point Prototype::Project(double x,double y,float height) const {
    // Subtract camera in double precision before sending small floats to OpenGL.
    const double dx=x-camera_.x,dy=y-camera_.y;
    return {r_.Width()*.5f+float((dx-dy)*.82)*zoom_,r_.Height()*.56f+float((dx+dy)*.43-height)*zoom_};
}
void Prototype::Ground(const Chunk& chunk) {
    const double x=chunk.key.x*World::ChunkSize,y=chunk.key.y*World::ChunkSize;
    auto quad=[&](double a,double b,double w,double d,Color c) {
        r_.Quad(Project(a,b),Project(a+w,b),Project(a+w,b+d),Project(a,b+d),c);
    };
    quad(x,y,640,640,Color(.047f,.073f,.105f));
    quad(x+102,y+102,535,535,Color(.08f,.11f,.15f));
    // Two connected arterial roads along the north and west edges of every chunk.
    quad(x,y,98,640,Color(.025f,.047f,.073f));
    quad(x,y,640,98,Color(.025f,.047f,.073f));
    for(int i=110;i<640;i+=48) {
        r_.Line(Project(x+49,y+i),Project(x+49,y+i+21),1,Color(.29f,.37f,.4f,.6f));
        r_.Line(Project(x+i,y+49),Project(x+i+21,y+49),1,Color(.29f,.37f,.4f,.6f));
    }
    for(int i=0;i<6;++i) {
        quad(x+110+i*10,y+8,4,80,Color(.25f,.33f,.39f,.65f));
        quad(x+8,y+110+i*10,80,4,Color(.25f,.33f,.39f,.65f));
    }
    r_.Line(Project(x+100,y+100),Project(x+640,y+100),2,Color(.18f,.28f,.34f));
    r_.Line(Project(x+100,y+100),Project(x+100,y+640),2,Color(.18f,.28f,.34f));
    for(int i=150;i<640;i+=90) {
        r_.Line(Project(x+i,y+105),Project(x+i,y+640),.7f,Color(.11f,.16f,.2f));
        r_.Line(Project(x+105,y+i),Project(x+640,y+i),.7f,Color(.11f,.16f,.2f));
    }
    for(const auto& b:chunk.buildings) {
        quad(b.x+8,b.y+25,b.width+35,b.depth+32,Color(0,.01f,.025f,.55f));
    }
    // Decorative road traffic; it does not implement NPC navigation or collisions.
    const uint64_t hash=World::Hash(static_cast<uint64_t>(chunk.key.x)^World::Hash(static_cast<uint64_t>(chunk.key.y)));
    const double carY=y+std::fmod(time_*65+double(hash%600),620.0);
    quad(x+18,carY,24,39,Color(.17f,.25f,.33f));
    quad(x+20,carY+7,20,12,Color(.27f,.49f,.58f));
    // Draw the light surface only; the post-process bloom creates its spread.
    r_.Line(Project(x+18,carY+39),Project(x+42,carY+39),2,White.Emissive(4));
    r_.Line(Project(x+18,carY),Project(x+42,carY),2,Pink.Emissive(3));
    if(scan_) {
        const auto devices=world_.Devices(chunk.key);
        for(size_t i=1;i<devices.size();++i) {
            const Point a=Project(devices[0].position.x,devices[0].position.y),b=Project(devices[i].position.x,devices[i].position.y);
            for(int j=0;j<12;j+=2) r_.Line(a+(b-a)*(j/12.f),a+(b-a)*((j+1)/12.f),1,Cyan.Alpha(.3f));
        }
    }
}
void Prototype::DrawBuilding(const Building& b,const ChunkKey& key) {
    const Point a=Project(b.x,b.y),c=Project(b.x+b.width,b.y+b.depth);
    if(c.y< -80 || a.y-b.height*zoom_>r_.Height()+100 ||
        std::max(a.x,c.x)+200*zoom_<0 || std::min(a.x,c.x)-200*zoom_>r_.Width()) return;
    const Point d=Project(b.x,b.y+b.depth),e=Project(b.x+b.width,b.y);
    const bool power=world_.Powered(key),open=b.accessRoom&&world_.Changes(key).doorOpen;
    const Color accent=power?Accent(b.style).Emissive(3):Color(.15f,.22f,.26f);
    const float h=open?18:b.height;
    const Point up(0,-h*zoom_),ar=a+up,cr=c+up,dr=d+up,er=e+up;
    r_.Quad(d,c,cr,dr,Color(.075f,.105f,.165f));
    r_.Quad(e,c,cr,er,Color(.045f,.065f,.12f));
    r_.Quad(ar,er,cr,dr,Color(.12f,.16f,.22f));
    r_.Line(ar,er,1,Color(.3f,.36f,.43f));
    r_.Line(ar,dr,1,Color(.26f,.32f,.4f));
    r_.Line(dr,cr,2,accent.Alpha(.7f)); r_.Line(er,cr,2,accent.Alpha(.6f));
    if(open) {
        r_.Quad(Project(b.x+9,b.y+9,19),Project(b.x+b.width-9,b.y+9,19),
            Project(b.x+b.width-9,b.y+b.depth-9,19),Project(b.x+9,b.y+b.depth-9,19),Color(.035f,.095f,.12f));
        for(int i=25;i<170;i+=25) r_.Line(Project(b.x+i,b.y+12,20),Project(b.x+i,b.y+b.depth-12,20),1,Cyan.Alpha(.14f));
        const Point archive=Project(b.x+90,b.y+65,25);
        if(!world_.Changes(key).dataTaken) {
            r_.Quad(archive+Point(0,-10),archive+Point(9,0),archive+Point(0,10),archive+Point(-9,0),Cyan.Emissive(3));
            r_.Text(archive.x-28,archive.y-25,"DATA",White,1.4f);
        }
        return;
    }
    // Individually seeded windows on both visible building faces.
    for(int floor=18;floor<int(b.height)-12;floor+=20) for(int col=14;col<165;col+=23) {
        const bool lit=(World::Hash(uint64_t(floor*181+col*19+b.style))%5)!=0;
        const Color window=power&&lit?accent.Alpha(.4f+.4f*float((floor+col)%3)/2):Color(.085f,.135f,.19f);
        r_.Quad(Project(b.x+col,b.y+b.depth+.1,floor),Project(b.x+col+10,b.y+b.depth+.1,floor),
            Project(b.x+col+10,b.y+b.depth+.1,floor+9),Project(b.x+col,b.y+b.depth+.1,floor+9),window);
        r_.Quad(Project(b.x+b.width+.1,b.y+col,floor),Project(b.x+b.width+.1,b.y+col+10,floor),
            Project(b.x+b.width+.1,b.y+col+10,floor+9),Project(b.x+b.width+.1,b.y+col,floor+9),window.Alpha(window.a*.7f));
    }
    const Point ra=Project(b.x+30,b.y+32,h+1),rb=Project(b.x+84,b.y+32,h+1),
        rc=Project(b.x+84,b.y+80,h+1),rd=Project(b.x+30,b.y+80,h+1);
    r_.Quad(ra,rb,rc,rd,Color(.075f,.105f,.145f));
    r_.Line(ra,rb,2,Muted.Alpha(.4f));
    for(int i=0;i<4;++i) r_.Line(ra+Point(-i*5,5+i*3),rb+Point(-i*5,5+i*3),1,Muted.Alpha(.25f));
    const Point antenna=Project(b.x+130,b.y+45,h);
    r_.Line(antenna,antenna+Point(0,-25*zoom_),2,Muted);
    r_.Circle(antenna+Point(0,-25*zoom_),2,Pink.Alpha(.55f+.4f*std::sin(time_*2+b.style)));
    if(power) {
        const Point sign=Project(b.x+40,b.y+b.depth+1,h-25);
        const char* names[]={"NOVA","CYBER","NEXUS","RAMEN","HOTEL"};
        r_.Rect(sign.x-6,sign.y-5,68*zoom_,19*zoom_,Panel);
        r_.Text(sign.x,sign.y,names[b.style%5],accent,1.7f*zoom_);
        r_.Line(Project(b.x+b.width,b.y+b.depth,9),Project(b.x+b.width,b.y+b.depth,h-6),2,accent.Alpha(.8f));
    }
}
void Prototype::DrawDevice(const Device& device) {
    Point p=Project(device.position.x,device.position.y);
    if(p.x< -80||p.x>r_.Width()+80||p.y< -80||p.y>r_.Height()+80)return;
    const bool powered=world_.Powered(device.key),selected=SameDevice(device,target_);
    Color color=powered?Cyan.Emissive(2.5f):Amber.Emissive(.8f);
    const auto& change=world_.Changes(device.key);
    if(device.type==DeviceType::Camera) {
        color=change.cameraOff?Muted:powered?Pink.Emissive(3):Muted;
        if(scan_&&powered&&!change.cameraOff) {
            r_.Triangle(p,Project(device.position.x+125,device.position.y+95),Project(device.position.x-25,device.position.y+140),Pink.Alpha(.055f));
        }
        r_.Line(p,p+Point(0,-37*zoom_),3,Muted);
        r_.Rect(p.x-8*zoom_,p.y-44*zoom_,17*zoom_,9*zoom_,Color(.2f,.3f,.37f));
        r_.Circle(p+Point(8*zoom_,-39*zoom_),2.5f,color);
    } else if(device.type==DeviceType::Power) {
        r_.Rect(p.x-9*zoom_,p.y-22*zoom_,18*zoom_,25*zoom_,Color(.15f,.23f,.28f));
        r_.Rect(p.x-6*zoom_,p.y-19*zoom_,12*zoom_,9*zoom_,color);
        r_.Line(p+Point(-4, -5),p+Point(4,-5),2,color);
    } else {
        const Point left=Project(device.position.x-26,device.position.y,0),right=Project(device.position.x+26,device.position.y,0);
        color=change.doorOpen?Cyan.Emissive(2):powered?Pink.Emissive(2):Amber.Emissive(.8f);
        r_.Line(left,left+Point(0,-30*zoom_),3,color);r_.Line(right,right+Point(0,-30*zoom_),3,color);
        r_.Line(left+Point(0,-30*zoom_),right+Point(0,-30*zoom_),3,color);
        if(!change.doorOpen)r_.Quad(left,right,right+Point(0,-28*zoom_),left+Point(0,-28*zoom_),color.Alpha(.35f));
    }
    if(scan_||selected) {
        // Selection graphics are markers, not additional luminous surfaces.
        const Color marker=color.Emissive(0);
        r_.Ring(p,selected?18:11,1,marker.Alpha(selected?.95f:.45f));
        if(selected) {
            r_.Line(p+Point(0,-50),p+Point(0,-68),1,marker);
            const char* label=device.type==DeviceType::Power?"RELAY":device.type==DeviceType::Camera?"CAMERA":"DOOR";
            r_.Text(p.x-24,p.y-83,label,marker,1.5f);
        }
    }
}
void Prototype::DrawPlayer() {
    const Point p=Project(player_.x,player_.y);
    const float step=moving_?std::sin(time_*14)*3:0;
    r_.Circle(p+Point(0,2),10,Color(0,0,0,.5f));
    r_.Line(p+Point(-4,-2),p+Point(-4+step,-11),4,Color(.13f,.2f,.26f));
    r_.Line(p+Point(4,-2),p+Point(4-step,-11),4,Color(.13f,.2f,.26f));
    r_.Quad(p+Point(-8,-25),p+Point(6,-25),p+Point(9,-8),p+Point(-8,-8),Color(.11f,.17f,.23f));
    r_.Line(p+Point(-7,-23),p+Point(-10,-10+step),3,Cyan);
    r_.Line(p+Point(7,-22),p+Point(11,-13-step),3,Color(.25f,.37f,.44f));
    r_.Circle(p+Point(0,-30),6,Color(.16f,.23f,.3f));
    r_.Line(p+Point(-4,-31),p+Point(4,-31),2,Cyan.Emissive(3));
    r_.Line(p+Point(-3,-20),p+Point(3,-20),2,Cyan.Emissive(2));
}
void Prototype::Draw() {
    r_.Begin(Color(.025f,.042f,.075f));
    for(const auto& entry:world_.Chunks()) Ground(entry.second);
    struct Item { double depth; const Building* building; ChunkKey key; Device device; int type; };
    std::vector<Item> items;
    auto actorDepth=[&](WorldPoint point) {
        double depth=point.x+point.y;
        const auto found=world_.Chunks().find(world_.KeyAt(point));
        if(found==world_.Chunks().end())return depth;
        for(const auto& b:found->second.buildings) {
            const bool south=point.y>=b.y+b.depth && point.y<b.y+b.depth+55 && point.x>=b.x-8 && point.x<=b.x+b.width+8;
            const bool east=point.x>=b.x+b.width && point.x<b.x+b.width+55 && point.y>=b.y-8 && point.y<=b.y+b.depth+8;
            const bool room=b.accessRoom&&world_.Changes(found->first).doorOpen&&
                point.x>b.x&&point.x<b.x+b.width&&point.y>b.y&&point.y<b.y+b.depth;
            if(south||east||room) depth=std::max(depth,b.x+b.y+b.width+b.depth+1);
        }
        return depth;
    };
    for(const auto& entry:world_.Chunks()) {
        for(const auto& building:entry.second.buildings) items.push_back({building.x+building.y+building.width+building.depth,&building,entry.first,{},0});
        for(const auto& device:world_.Devices(entry.first)) items.push_back({actorDepth(device.position),nullptr,entry.first,device,1});
    }
    items.push_back({actorDepth(player_),nullptr,{},{},2});
    std::stable_sort(items.begin(),items.end(),[](const Item& a,const Item& b){return a.depth<b.depth;});
    for(const auto& item:items) {
        if(item.type==0) DrawBuilding(*item.building,item.key);
        else if(item.type==1) DrawDevice(item.device);
        else DrawPlayer();
    }
    const Point p=Project(player_.x,player_.y);
    // An always-visible locator preserves orientation behind tall buildings.
    r_.Ring(p,13,1,Cyan.Alpha(.65f));
    r_.Circle(p+Point(0,-48),2,Cyan);
    if(scan_) {
        const float radius=45+std::fmod(time_*28,95.f);
        for(int i=0;i<48;++i) {
            const double a=i*6.2831853/48,b=(i+1)*6.2831853/48;
            r_.Line(Project(player_.x+std::cos(a)*radius,player_.y+std::sin(a)*radius),
                Project(player_.x+std::cos(b)*radius,player_.y+std::sin(b)*radius),1,Cyan.Alpha((1-(radius-45)/95)*.18f));
        }
    }
    if(target_.valid && (scan_||hackProgress_>0)) {
        const Point endpoint=Project(target_.position.x,target_.position.y);
        r_.Line(p+Point(0,-18),endpoint,1.5f,Cyan.Alpha(.45f));
        r_.Ring(endpoint,20+std::sin(time_*3)*2,1,Cyan);
    }
    r_.BeginOverlay();
    Hud();
    r_.End();
}
void Prototype::Hud() {
    const float w=float(r_.Width()),h=float(r_.Height());
    const float ui=std::min(1.f,std::min(w/1120.f,h/680.f));
    auto rect=[&](float x,float y,float a,float b,Color c){r_.Rect(x*ui,y*ui,a*ui,b*ui,c);};
    auto text=[&](float x,float y,const std::string& s,Color c,float scale=1.6f){r_.Text(x*ui,y*ui,s,c,scale*ui);};
    const float uw=w/ui,uh=h/ui;
    rect(0,0,uw,94,Panel);
    rect(28,26,4,39,Cyan);
    text(47,25,"NIGHT / LINK",White,3.4f);
    text(49,61,"ENDLESS CITY   /   HACKING RPG",Muted,1.3f);
    text(uw-287,24,"23:48  /  PERMANENT NIGHT",Cyan,1.5f);
    const auto key=world_.KeyAt(player_);
    text(uw-287,52,"SECTOR "+Number(key.x)+" : "+Number(key.y),White,1.4f);
    rect(28,92,uw-56,1,Color(.16f,.29f,.34f));

    const bool power=world_.Powered(key);
    rect(28,120,282,155,Panel);rect(28,120,3,155,power?Cyan:Amber);
    text(45,137,"CITY EVENT / 01",Muted,1.3f);
    text(45,160,power?"GRID ONLINE":"LOCAL BLACKOUT",power?Cyan:Amber,2.1f);
    if(!power) {
        text(45,191,"FIND THE AMBER RELAY",White,1.4f);
        text(45,213,"HOLD E TO RESTORE POWER",White,1.4f);
        text(45,247,"REWARD 150 CR + UPLINK",Muted,1.3f);
    } else {
        text(45,191,"HACK THE ACCESS DOOR",White,1.4f);
        text(45,213,"ENTER / RECOVER ARCHIVE",White,1.4f);
        text(45,247,world_.Changes(key).dataTaken?"ARCHIVE SECURED / +75 CR":"ARCHIVE REWARD / 75 CR",Muted,1.3f);
    }
    rect(28,288,282,68,Panel);
    text(45,303,"TRACE",Muted,1.3f);text(245,303,Number(int(trace_))+"%",trace_>60?Pink:Cyan,1.3f);
    rect(45,330,246,4,Color(.12f,.19f,.23f));rect(45,330,246*trace_/100,4,trace_>60?Pink:Cyan);

    const float mapX=uw-218,mapY=121;
    rect(mapX,mapY,190,210,Panel);text(mapX+15,mapY+15,"LOCAL NETWORK",Muted,1.3f);
    const float cell=45,ox=mapX+27,oy=mapY+48;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        const ChunkKey k{key.x+x,key.y+y};
        rect(ox+(x+1)*cell,oy+(y+1)*cell,cell-3,cell-3,world_.Powered(k)?Color(.08f,.2f,.23f):Color(.25f,.17f,.10f));
        rect(ox+(x+1)*cell,oy+(y+1)*cell,3,cell-3,Muted.Alpha(.4f));
    }
    const float localX=float((player_.x-key.x*640)/640),localY=float((player_.y-key.y*640)/640);
    r_.Circle({(ox+cell+localX*cell)*ui,(oy+cell+localY*cell)*ui},3*ui,Cyan);
    text(mapX+15,mapY+191,"NORTH UP / 3 X 3",Muted,1.1f);
    text(uw-213,350,"CREDITS  "+Number(world_.Credits()),White,1.6f);
    text(uw-213,374,world_.Credits()>=150?"UPLINK / LEVEL 02":"UPLINK / LEVEL 01",Cyan,1.25f);
    const auto& effects=r_.PostEffects();
    rect(uw-218,400,190,140,Panel);
    text(uw-203,414,r_.PostEffectsAvailable()?std::string("O POST FX / ")+(effects.enabled?"ON":"OFF"):"POST FX UNAVAILABLE",Cyan,1.15f);
    text(uw-203,439,std::string("B BLOOM ")+(effects.bloom?Decimal(effects.bloomStrength):"OFF"),Muted,1.15f);
    text(uw-203,459,std::string("V SHADE ")+(effects.vignette?Decimal(effects.vignetteStrength):"OFF"),Muted,1.15f);
    text(uw-203,479,std::string("F EDGE ")+(effects.edgeBlur?Decimal(effects.edgeBlurStrength):"OFF"),Muted,1.15f);
    text(uw-203,514,"[ ] EXPOSURE "+Decimal(effects.exposure),White,1.1f);

    const float bottom=uh-109;
    rect(0,bottom,uw,109,Panel);rect(28,bottom,uw-56,1,Color(.16f,.29f,.34f));
    text(29,bottom+17,"WASD  MOVE",White,1.35f);
    text(196,bottom+17,"SPACE  RUN",White,1.35f);
    text(367,bottom+17,"TAB  SCAN",scan_?Cyan:Muted,1.35f);
    text(528,bottom+17,"E  HOLD TO HACK",White,1.35f);
    text(uw-234,bottom+17,"P PAUSE / ESC EXIT",Muted,1.2f);
    const std::string device=target_.type==DeviceType::Power?"POWER RELAY":target_.type==DeviceType::Camera?"SECURITY CAMERA":"ACCESS DOOR";
    text(29,bottom+48,lockdown_>0?"CONNECTION LOCKED":target_.valid?"TARGET / "+device:"NO DEVICE IN RANGE",lockdown_>0?Pink:Cyan,1.8f);
    text(29,bottom+80,"+/- ZOOM   K SAVE   /   AUTO SAVE 20S",Muted,1.1f);
    rect(uw-365,bottom+51,334,5,Color(.12f,.19f,.23f));
    rect(uw-365,bottom+51,334*std::min(1.f,hackProgress_),5,Cyan);
    text(uw-365,bottom+72,hackProgress_>0?"ESTABLISHING UPLINK...":"HOLD STILL TO ESTABLISH UPLINK",Muted,1.1f);
    if(toastTime_>0) {
        const float width=std::min(uw-60,float(toast_.size())*8.4f+32);
        rect((uw-width)/2,bottom-48,width,33,Panel);
        text((uw-width)/2+16,bottom-37,toast_,White,1.4f);
    }
    if(paused_) {
        rect(0,94,uw,bottom-94,Color(.01f,.025f,.05f,.65f));
        text(uw/2-72,uh/2-15,"PAUSED",White,3);
        text(uw/2-104,uh/2+24,"PRESS P TO RESUME",Cyan,1.4f);
    }
}
