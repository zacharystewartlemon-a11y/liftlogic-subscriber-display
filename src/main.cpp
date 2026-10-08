#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Free_Fonts.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>
#include "logo_brain.h"

TFT_eSPI tft;
WebServer server(80);
Preferences prefs;

constexpr char CHANNEL_ID[]="UC0F9K9qnsopawSgePiOls3g";
constexpr char FW[]="0.4.2";
constexpr char TZ_INFO[]="MST7MDT,M3.2.0/2,M11.1.0/2";
constexpr char OTA_MANIFEST[]="https://zacharystewartlemon-a11y.github.io/liftlogic-subscriber-display/latest.json";
constexpr unsigned long OTA_INTERVAL_MS=6UL*60UL*60UL*1000UL;
constexpr unsigned long OTA_FIRST_CHECK_MS=60UL*1000UL;

constexpr uint8_t BL_PIN=21, BL_CH=7;
constexpr uint32_t BL_FREQ=20000;
constexpr uint8_t T_IRQ=36,T_MOSI=32,T_MISO=39,T_CLK=25,T_CS=33;
constexpr int TOUCH_X_MIN=200,TOUCH_X_MAX=3700,TOUCH_Y_MIN=240,TOUCH_Y_MAX=3800;

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(T_CS,T_IRQ);

const uint16_t BLUE=TFT_CYAN, PURPLE=0x801F, PANEL=TFT_BLACK, LINE=0x1082, DIM=0x9CF3;
const uint16_t REFRESHES[]={15,30,60,300};
const uint16_t WAKES[]={15,30,60,120};

uint8_t brightness=80;
uint16_t refreshSec=15,sleepStart=1380,sleepEnd=420,wakeSec=30;
uint32_t goalTarget=500;
bool sleepOn=false,sleeping=false,lastWifi=false,autoUpdate=true,flip180=false;
bool otaInitialChecked=false,otaCheckRequested=false,otaBusy=false;
String apiKey,count="--",shown="",statusText="Waiting",otaStatus="Not checked yet";
unsigned long lastPoll=0,wakeUntil=0,lastTouch=0,lastOtaCheck=0;

enum Screen{MAIN,SETTINGS,SLEEPSET,UPDATESET,GOALSET};
Screen screen=MAIN;

void backlight(uint8_t p){ ledcWrite(BL_CH,map(constrain(p,0,100),0,100,0,255)); }
void saveBrightness(int p){
  brightness=constrain(p,0,100);
  prefs.putUChar("brightness",brightness);
  if(!sleeping) backlight(brightness);
}
bool tempWake(){ return wakeUntil && (long)(wakeUntil-millis())>0; }
bool nowMinutes(uint16_t &m){
  struct tm ti;
  if(!getLocalTime(&ti,20)) return false;
  m=ti.tm_hour*60+ti.tm_min; return true;
}
bool inSleepWindow(){
  if(!sleepOn) return false;
  uint16_t n; if(!nowMinutes(n)||sleepStart==sleepEnd) return false;
  if(sleepStart<sleepEnd) return n>=sleepStart&&n<sleepEnd;
  return n>=sleepStart||n<sleepEnd;
}
void updateSleep(){
  bool want=inSleepWindow()&&!tempWake();
  if(want!=sleeping){ sleeping=want; backlight(want?0:brightness); }
}
String time12(uint16_t m){
  int h=(m/60)%24,mm=m%60,h12=h%12; if(!h12)h12=12;
  char b[16]; snprintf(b,sizeof(b),"%d:%02d %s",h12,mm,h>=12?"PM":"AM"); return String(b);
}
String time24(uint16_t m){
  char b[6]; snprintf(b,sizeof(b),"%02u:%02u",m/60,m%60); return String(b);
}
bool parseTime(const String&s,uint16_t&m){
  if(s.length()!=5||s[2]!=':')return false;
  int h=s.substring(0,2).toInt(),mm=s.substring(3).toInt();
  if(h<0||h>23||mm<0||mm>59)return false;
  m=h*60+mm; return true;
}

void text(const String&s,int x,int y,uint16_t c,int f,uint8_t d=MC_DATUM){
  tft.setFreeFont(nullptr); tft.setTextDatum(d); tft.setTextColor(c,TFT_BLACK); tft.drawString(s,x,y,f);
}
void freeText(const String&s,int x,int y,uint16_t c,const GFXfont*f,uint8_t d=MC_DATUM){
  tft.setTextDatum(d); tft.setTextColor(c); tft.setFreeFont(f); tft.drawString(s,x,y,GFXFF); tft.setFreeFont(nullptr);
}
void brain(int x,int y){
  uint16_t row[LL_BRAIN_W];
  for(int py=0;py<LL_BRAIN_H;py++){
    for(int px=0;px<LL_BRAIN_W;px++){
      int i=py*LL_BRAIN_W+px;
      uint8_t packed=pgm_read_byte(&LL_BRAIN_PIXELS[i/4]);
      uint8_t pi=(packed>>(6-2*(i&3)))&3;
      row[px]=pgm_read_word(&LL_BRAIN_PALETTE[pi]);
    }
    tft.pushImage(x,y+py,LL_BRAIN_W,1,row);
  }
}
void brand(){
  brain(28,5);
  freeText("LiftLogic",194,26,TFT_WHITE,FSSB18);
}
void youtube(int x,int y){
  tft.fillRoundRect(x,y,34,23,6,TFT_RED);
  tft.fillTriangle(x+13,y+5,x+13,y+18,x+24,y+11,TFT_WHITE);
}
void wifiIcon(){
  tft.fillRect(0,205,58,35,TFT_BLACK);
  uint16_t c=WiFi.status()==WL_CONNECTED?BLUE:TFT_DARKGREY;
  int x=19,b=229;
  tft.fillRect(x,b-4,4,4,c); tft.fillRect(x+7,b-8,4,8,c);
  tft.fillRect(x+14,b-13,4,13,c); tft.fillRect(x+21,b-18,4,18,c);
}
void dots(){
  tft.fillRect(260,205,60,35,TFT_BLACK);
  for(int i=0;i<3;i++)tft.fillCircle(279+i*10,222,3,TFT_WHITE);
}
void drawGoal(){
  if(screen!=MAIN)return;
  tft.fillRect(58,201,202,39,TFT_BLACK);
  uint32_t current=0;
  bool numeric=count.length()>0;
  for(size_t i=0;i<count.length();i++) if(!isDigit(count[i])) numeric=false;
  if(numeric) current=(uint32_t)strtoul(count.c_str(),nullptr,10);
  const int x=78,y=207,w=164,h=6;
  tft.drawRoundRect(x,y,w,h,3,PURPLE);
  if(numeric&&goalTarget>0){
    uint32_t clamped=current>goalTarget?goalTarget:current;
    int fill=(int)((uint64_t)(w-2)*clamped/goalTarget);
    if(fill>0)tft.fillRoundRect(x+1,y+1,fill,h-2,2,BLUE);
  }
  String label=String("GOAL  ")+(numeric?String(current):String("--"))+" / "+String(goalTarget);
  text(label,160,226,TFT_WHITE,2);
}
void drawCount(bool force=false){
  if(screen!=MAIN||(!force&&count==shown))return;
  tft.fillRoundRect(42,116,236,70,10,PANEL);
  freeText(count,160,150,TFT_WHITE,FSSB24);
  shown=count;
  drawGoal();
}
void mainScreen(){
  screen=MAIN; tft.fillScreen(TFT_BLACK);
  uint16_t grid=tft.color565(3,10,18);
  for(int x=-80;x<360;x+=42)tft.drawLine(x,58,x+90,203,grid);
  brand();
  tft.fillRoundRect(31,64,258,136,14,PANEL);
  tft.drawRoundRect(31,64,258,136,14,BLUE);
  tft.drawRoundRect(33,66,254,132,12,PURPLE);
  youtube(67,80); text("SUBSCRIBERS",190,92,TFT_WHITE,4);
  tft.drawFastHLine(60,108,200,LINE);
  shown=""; drawCount(true); wifiIcon(); dots(); drawGoal();
}
void setupScreen(){
  tft.fillScreen(TFT_BLACK); brand();
  freeText("Wi-Fi setup",160,98,TFT_WHITE,FSSB18);
  text("Connect to LiftLogic-Setup",160,140,BLUE,2);
  text("then follow the setup page",160,169,DIM,2);
}
void apiScreen(){
  tft.fillScreen(TFT_BLACK); brand();
  freeText("API key needed",160,95,TFT_WHITE,FSSB18);
  text(WiFi.localIP().toString(),160,140,BLUE,4);
  text("Open this address to finish setup",160,178,DIM,2);
}
void row(int y,const String&l,const String&v){
  tft.drawFastHLine(12,y+36,296,LINE);
  text(l,18,y+18,DIM,2,ML_DATUM); text(v,298,y+18,TFT_WHITE,2,MR_DATUM);
}
void settingsScreen(){
  screen=SETTINGS; tft.fillScreen(TFT_BLACK);
  text("<",18,20,BLUE,4,ML_DATUM); freeText("Settings",160,21,TFT_WHITE,FSSB12);
  row(40,"Brightness",String(brightness)+"%    -   +");
  row(78,"Refresh",refreshSec==15?"15 sec  >":refreshSec==30?"30 sec  >":refreshSec==60?"1 min  >":"5 min  >");
  row(116,"Auto sleep",String(sleepOn?"ON":"OFF")+"   >");
  row(154,"Sleep settings",time12(sleepStart)+"  >");
  row(192,"Software",String("v")+FW+"   >");
}
void sleepScreen(){
  screen=SLEEPSET; tft.fillScreen(TFT_BLACK);
  text("<",18,20,BLUE,4,ML_DATUM); freeText("Sleep timer",160,21,TFT_WHITE,FSSB12);
  row(48,"Sleep start",time12(sleepStart)); text("-",225,66,BLUE,4); text("+",292,66,BLUE,4);
  row(96,"Wake time",time12(sleepEnd)); text("-",225,114,BLUE,4); text("+",292,114,BLUE,4);
  row(144,"Tap wake",String(wakeSec)+" sec  >");
  text("Times save automatically",160,208,DIM,2);
}
void updateScreen(){
  screen=UPDATESET; tft.fillScreen(TFT_BLACK);
  text("<",18,20,BLUE,4,ML_DATUM); freeText("Software",160,21,TFT_WHITE,FSSB12);
  row(40,"Installed",String("v")+FW);
  row(78,"Auto update",autoUpdate?"ON   >":"OFF   >");
  row(116,"Check now","Tap   >");
  row(154,"Screen flip",flip180?"180 deg   >":"Normal   >");
  text(otaStatus,160,224,DIM,2);
}
void goalScreen(){
  screen=GOALSET; tft.fillScreen(TFT_BLACK);
  text("<",18,20,BLUE,4,ML_DATUM); freeText("Subscriber goal",160,21,TFT_WHITE,FSSB12);
  row(50,"Current",count);
  row(96,"Target",String(goalTarget));
  text("-",220,114,BLUE,4); text("+",292,114,BLUE,4);
  text("Adjusts by 100 subscribers",160,177,DIM,2);
  text("Goal is saved on the device",160,210,DIM,2);
}
void adjustGoal(int delta){
  int64_t next=(int64_t)goalTarget+delta;
  if(next<100)next=100;
  if(next>100000000)next=100000000;
  goalTarget=(uint32_t)next;
  prefs.putUInt("goal",goalTarget);
  goalScreen();
}
void redrawCurrent(){
  if(screen==MAIN)mainScreen();
  else if(screen==SETTINGS)settingsScreen();
  else if(screen==SLEEPSET)sleepScreen();
  else if(screen==UPDATESET)updateScreen();
  else goalScreen();
}
void setFlip(bool flipped){
  flip180=flipped;
  prefs.putBool("flip180",flip180);
  tft.setRotation(flip180?3:1);
  touch.setRotation(flip180?3:1);
  redrawCurrent();
}


bool versionNewer(const String&latest,const String&current){
  int la=0,lb=0,lc=0,ca=0,cb=0,cc=0;
  if(sscanf(latest.c_str(),"%d.%d.%d",&la,&lb,&lc)<1)return false;
  sscanf(current.c_str(),"%d.%d.%d",&ca,&cb,&cc);
  if(la!=ca)return la>ca;
  if(lb!=cb)return lb>cb;
  return lc>cc;
}
void otaMessage(const String&title,const String&detail){
  tft.fillScreen(TFT_BLACK); brand();
  freeText(title,160,104,TFT_WHITE,FSSB18);
  text(detail,160,148,BLUE,2);
}
bool checkForUpdate(bool installIfAvailable){
  if(WiFi.status()!=WL_CONNECTED||otaBusy)return false;
  otaBusy=true;
  otaStatus="Checking...";
  if(screen==UPDATESET)updateScreen();

  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(10000);
  if(!http.begin(client,OTA_MANIFEST)){
    otaStatus="Update check failed"; otaBusy=false; if(screen==UPDATESET)updateScreen(); return false;
  }
  int code=http.GET();
  if(code!=HTTP_CODE_OK){
    otaStatus="Update check error "+String(code); http.end(); otaBusy=false; if(screen==UPDATESET)updateScreen(); return false;
  }
  String payload=http.getString(); http.end();
  JsonDocument doc;
  if(deserializeJson(doc,payload)){
    otaStatus="Bad update response"; otaBusy=false; if(screen==UPDATESET)updateScreen(); return false;
  }
  String latest=doc["version"]|"";
  String firmware=doc["firmware"]|"";
  if(latest.isEmpty()||firmware.isEmpty()){
    otaStatus="Update info missing"; otaBusy=false; if(screen==UPDATESET)updateScreen(); return false;
  }
  if(!versionNewer(latest,FW)){
    otaStatus=String("Up to date · v")+FW; otaBusy=false; if(screen==UPDATESET)updateScreen(); return true;
  }
  otaStatus=String("v")+latest+" available";
  if(!installIfAvailable){otaBusy=false;if(screen==UPDATESET)updateScreen();return true;}

  otaMessage("Updating...",String("Installing v")+latest);
  HTTPUpdate updater;
  updater.rebootOnUpdate(true);
  WiFiClientSecure fwClient; fwClient.setInsecure();
  t_httpUpdate_return result=updater.update(fwClient,firmware);
  if(result==HTTP_UPDATE_FAILED)otaStatus=String("Update failed: ")+updater.getLastErrorString();
  else if(result==HTTP_UPDATE_NO_UPDATES)otaStatus="Already up to date";
  otaBusy=false;
  if(apiKey.isEmpty())apiScreen();else mainScreen();
  return result==HTTP_UPDATE_OK;
}

bool fetchCount(){
  if(apiKey.isEmpty()||WiFi.status()!=WL_CONNECTED)return false;
  String u="https://www.googleapis.com/youtube/v3/channels?part=statistics&id="+String(CHANNEL_ID)
    +"&fields=items/statistics(subscriberCount,hiddenSubscriberCount)&key="+apiKey;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(8000);
  if(!http.begin(client,u)){statusText="Connection failed";return false;}
  int code=http.GET();
  if(code!=HTTP_CODE_OK){statusText="YouTube "+String(code);http.end();return false;}
  String payload=http.getString(); http.end();
  JsonDocument doc;
  if(deserializeJson(doc,payload)||doc["items"].size()==0){statusText="Bad response";return false;}
  String n;
  if((bool)(doc["items"][0]["statistics"]["hiddenSubscriberCount"]|false)) n="HIDDEN";
  else{
    const char*c=doc["items"][0]["statistics"]["subscriberCount"];
    if(!c){statusText="No count";return false;} n=String(c);
  }
  statusText="Live";
  if(n!=count){count=n;drawCount();}
  return true;
}
int refreshIndex(){for(int i=0;i<4;i++)if(REFRESHES[i]==refreshSec)return i;return 0;}
int wakeIndex(){for(int i=0;i<4;i++)if(WAKES[i]==wakeSec)return i;return 1;}
void cycleRefresh(){refreshSec=REFRESHES[(refreshIndex()+1)%4];prefs.putUShort("refresh_s",refreshSec);lastPoll=millis();}
void cycleWake(){wakeSec=WAKES[(wakeIndex()+1)%4];prefs.putUShort("wake_s",wakeSec);}
void adjust(uint16_t&v,int d){int n=(int)v+d;while(n<0)n+=1440;while(n>=1440)n-=1440;v=n;}

bool readTouch(int&x,int&y){
  if(millis()-lastTouch<220||!touch.touched())return false;
  TS_Point p=touch.getPoint();
  x=constrain(map(p.x,TOUCH_X_MIN,TOUCH_X_MAX,0,319),0,319);
  y=constrain(map(p.y,TOUCH_Y_MIN,TOUCH_Y_MAX,0,239),0,239);
  lastTouch=millis(); return true;
}
void handleTouch(){
  int x,y;if(!readTouch(x,y))return;
  if(sleeping){wakeUntil=millis()+(unsigned long)wakeSec*1000UL;sleeping=false;backlight(brightness);return;}
  if(inSleepWindow())wakeUntil=millis()+(unsigned long)wakeSec*1000UL;

  if(screen==MAIN){
    if(x>=245&&y>=180)settingsScreen();
    else if(x>=58&&x<260&&y>=198)goalScreen();
    return;
  }
  if(screen==SETTINGS){
    if(y<42&&x<75)mainScreen();
    else if(y>=40&&y<78){if(x>=260)saveBrightness(brightness+10);else if(x>=190)saveBrightness(brightness-10);settingsScreen();}
    else if(y>=78&&y<116){cycleRefresh();settingsScreen();}
    else if(y>=116&&y<154){sleepOn=!sleepOn;prefs.putBool("sleep_on",sleepOn);settingsScreen();updateSleep();}
    else if(y>=154&&y<192)sleepScreen();
    else if(y>=192)updateScreen();
    return;
  }
  if(screen==SLEEPSET){
    if(y<42&&x<75)settingsScreen();
    else if(y>=48&&y<96&&x>=190){adjust(sleepStart,x>=260?30:-30);prefs.putUShort("sleep_start",sleepStart);sleepScreen();}
    else if(y>=96&&y<144&&x>=190){adjust(sleepEnd,x>=260?30:-30);prefs.putUShort("sleep_end",sleepEnd);sleepScreen();}
    else if(y>=144&&y<190){cycleWake();sleepScreen();}
    return;
  }
  if(screen==UPDATESET){
    if(y<42&&x<75)settingsScreen();
    else if(y>=78&&y<116){autoUpdate=!autoUpdate;prefs.putBool("auto_update",autoUpdate);updateScreen();}
    else if(y>=116&&y<154){otaCheckRequested=true;otaStatus="Update check queued";updateScreen();}
    else if(y>=154&&y<198){setFlip(!flip180);}
    return;
  }
  if(screen==GOALSET){
    if(y<42&&x<75)mainScreen();
    else if(y>=96&&y<145&&x>=185){
      adjustGoal(x>=258?100:-100);
    }
  }
}

String page(){
  String h=R"HTML(<!doctype html><meta name=viewport content="width=device-width,initial-scale=1"><title>LiftLogic Display</title>
<style>:root{color-scheme:dark;--a:#19b8ff;--b:#8b4dff}*{box-sizing:border-box}body{font-family:system-ui;max-width:620px;margin:auto;padding:28px 18px;background:#07080b;color:#f7f7fb}.brand{font-size:30px;font-weight:850}.bar{height:3px;background:linear-gradient(90deg,var(--a),var(--b));margin:10px 0 22px}.card{background:#11141a;border:1px solid #29303b;border-radius:16px;padding:18px;margin:14px 0}label{display:block;margin-top:12px}input,select,button{width:100%;padding:11px;margin-top:7px;border-radius:9px;border:1px solid #343b49;background:#090b10;color:white}button{border:0;background:linear-gradient(90deg,var(--a),var(--b));font-weight:700}.muted{color:#9ca3af}</style>
<div class=brand>LiftLogic</div><div class=muted>Subscriber Display · v)HTML";
  h+=FW;
  h+=R"HTML(</div><div class=bar></div><div class=card><b>Status</b><p>Subscribers: )HTML";
  h+=count; h+="<br>Wi-Fi: "+WiFi.SSID()+"<br>IP: "+WiFi.localIP().toString();
  h+=R"HTML(</p><button onclick="fetch('/refresh').then(()=>location.reload())">Refresh now</button></div>
<div class=card><b>Subscriber goal</b><p>Current progress: <b>)HTML"+count+R"HTML( / )HTML"+String(goalTarget)+R"HTML(</b></p>
<input id=goal type=number min=100 max=100000000 step=100 value=")HTML"+String(goalTarget)+R"HTML(><button id=gs>Save goal</button></div>
<div class=card><b>Display</b><label>Brightness <span id=bv></span></label><input id=b type=range min=0 max=100 step=5><button id=bs>Save brightness</button>
<label>Refresh interval</label><select id=p><option value=15>15 seconds</option><option value=30>30 seconds</option><option value=60>1 minute</option><option value=300>5 minutes</option></select><button id=ps>Save refresh interval</button>
<label><input id=flip style="width:auto" type=checkbox )HTML"+String(flip180?"checked":"")+R"HTML(> Flip screen 180°</label></div>
<div class=card><b>Sleep timer</b><form method=post action=/sleep><label><input style="width:auto" type=checkbox name=enabled value=1 )HTML";
  if(sleepOn)h+="checked";
  h+=R"HTML(> Enable automatic sleep</label><label>Sleep at</label><input type=time name=start value=")HTML"+time24(sleepStart)+
     R"HTML("><label>Wake at</label><input type=time name=end value=")HTML"+time24(sleepEnd)+
     R"HTML("><label>Tap-to-wake</label><select name=wake><option value=15>15 seconds</option><option value=30>30 seconds</option><option value=60>1 minute</option><option value=120>2 minutes</option></select><button>Save sleep settings</button></form><p class=muted>Mountain Time. During sleep the backlight turns fully off; tap the screen to wake it temporarily.</p></div>
<div class=card><b>Software update</b><p>Installed: v)HTML"+String(FW)+R"HTML(<br>Automatic updates: <b>)HTML"+String(autoUpdate?"On":"Off")+R"HTML(</b></p>
<label><input id=au style="width:auto" type=checkbox )HTML"+String(autoUpdate?"checked":"")+R"HTML(> Install new LiftLogic firmware automatically</label>
<button id=uc>Check for update now</button><p id=us class=muted>)HTML"+otaStatus+R"HTML(</p></div>
<div class=card><b>YouTube API</b><p class=muted>)HTML";
  h+=apiKey.isEmpty()?"No API key saved.":"API key saved on this display.";
  h+=R"HTML(</p><form method=post action=/apikey><input name=apikey type=password autocomplete=off placeholder="Paste a new API key"><button>Save API key</button></form></div>
<script>const b=document.getElementById('b'),bv=document.getElementById('bv'),p=document.getElementById('p');b.value=)HTML"+String(brightness)+
     R"HTML(;bv.textContent=b.value+'%';b.oninput=()=>bv.textContent=b.value+'%';p.value=)HTML"+String(refreshSec)+
     R"HTML(;document.getElementById('bs').onclick=()=>fetch('/brightness?value='+b.value);document.getElementById('ps').onclick=()=>fetch('/poll?seconds='+p.value);document.querySelector('select[name=wake]').value=)HTML"+String(wakeSec)+
     R"HTML(;document.getElementById('au').onchange=e=>fetch('/autoupdate?enabled='+(e.target.checked?1:0));document.getElementById('uc').onclick=async()=>{document.getElementById('us').textContent='Update check queued...';await fetch('/update');};document.getElementById('flip').onchange=e=>fetch('/flip?enabled='+(e.target.checked?1:0));document.getElementById('gs').onclick=()=>fetch('/goal?target='+document.getElementById('goal').value);</script>)HTML";
  return h;
}

void web(){
  server.on("/",HTTP_GET,[]{server.send(200,"text/html",page());});
  server.on("/goal",HTTP_GET,[]{
    if(!server.hasArg("target")){server.send(400,"text/plain","Missing target");return;}
    long v=server.arg("target").toInt();
    if(v<100){server.send(400,"text/plain","Goal must be at least 100");return;}
    goalTarget=(uint32_t)v;prefs.putUInt("goal",goalTarget);
    if(screen==MAIN)drawGoal();
    server.send(200,"text/plain","OK");
  });
  server.on("/brightness",HTTP_GET,[]{
    if(!server.hasArg("value")){server.send(400,"text/plain","Missing");return;}
    saveBrightness(server.arg("value").toInt());server.send(200,"text/plain","OK");
  });
  server.on("/poll",HTTP_GET,[]{
    int v=server.arg("seconds").toInt();bool ok=false;for(uint16_t a:REFRESHES)if(v==a)ok=true;
    if(!ok){server.send(400,"text/plain","Bad interval");return;}
    refreshSec=v;prefs.putUShort("refresh_s",refreshSec);lastPoll=millis();server.send(200,"text/plain","OK");
  });
  server.on("/sleep",HTTP_POST,[]{
    sleepOn=server.hasArg("enabled");uint16_t v;
    if(parseTime(server.arg("start"),v))sleepStart=v;if(parseTime(server.arg("end"),v))sleepEnd=v;
    int w=server.arg("wake").toInt();for(uint16_t a:WAKES)if(w==a)wakeSec=w;
    prefs.putBool("sleep_on",sleepOn);prefs.putUShort("sleep_start",sleepStart);prefs.putUShort("sleep_end",sleepEnd);prefs.putUShort("wake_s",wakeSec);
    updateSleep();server.sendHeader("Location","/",true);server.send(303,"text/plain","");
  });
  server.on("/apikey",HTTP_POST,[]{
    if(!server.hasArg("apikey")||server.arg("apikey").length()<10){server.send(400,"text/plain","Invalid key");return;}
    apiKey=server.arg("apikey");apiKey.trim();prefs.putString("yt_api_key",apiKey);count="--";mainScreen();fetchCount();
    server.sendHeader("Location","/",true);server.send(303,"text/plain","");
  });
  server.on("/refresh",HTTP_GET,[]{bool ok=fetchCount();server.send(ok?200:500,"text/plain",ok?count:statusText);});
  server.on("/autoupdate",HTTP_GET,[]{
    autoUpdate=server.arg("enabled")=="1";prefs.putBool("auto_update",autoUpdate);server.send(200,"text/plain","OK");
  });
  server.on("/flip",HTTP_GET,[]{
    setFlip(server.arg("enabled")=="1");server.send(200,"text/plain","OK");
  });
  server.on("/update",HTTP_GET,[]{
    otaCheckRequested=true;server.send(202,"text/plain","Update check queued. The display will reboot automatically if a newer version is available.");
  });
  server.begin();
}

void setup(){
  Serial.begin(115200);prefs.begin("liftlogic",false);
  brightness=prefs.getUChar("brightness",80);apiKey=prefs.getString("yt_api_key","");
  refreshSec=prefs.getUShort("refresh_s",15);sleepOn=prefs.getBool("sleep_on",false);
  sleepStart=prefs.getUShort("sleep_start",1380);sleepEnd=prefs.getUShort("sleep_end",420);wakeSec=prefs.getUShort("wake_s",30);
  autoUpdate=prefs.getBool("auto_update",true);
  flip180=prefs.getBool("flip180",false);
  goalTarget=prefs.getUInt("goal",500);
  if(goalTarget<100)goalTarget=500;

  tft.init();tft.setRotation(flip180?3:1);
  ledcSetup(BL_CH,BL_FREQ,8);ledcAttachPin(BL_PIN,BL_CH);backlight(brightness);
  touchSPI.begin(T_CLK,T_MISO,T_MOSI,T_CS);touch.begin(touchSPI);touch.setRotation(flip180?3:1);

  setupScreen();WiFi.mode(WIFI_STA);WiFiManager wm;wm.setHostname("liftlogic-display");
  if(!wm.autoConnect("LiftLogic-Setup")){delay(2000);ESP.restart();}
  configTzTime(TZ_INFO,"pool.ntp.org","time.nist.gov");web();lastWifi=true;

  if(apiKey.isEmpty())apiScreen();else{mainScreen();fetchCount();}
  lastOtaCheck=millis();
  updateSleep();
}
void loop(){
  server.handleClient();handleTouch();updateSleep();
  bool wc=WiFi.status()==WL_CONNECTED;
  if(wc!=lastWifi){lastWifi=wc;if(screen==MAIN)wifiIcon();}
  if(!wc){static unsigned long r=0;if(millis()-r>10000){r=millis();WiFi.reconnect();}}
  else if(!apiKey.isEmpty()&&millis()-lastPoll>=(unsigned long)refreshSec*1000UL){lastPoll=millis();fetchCount();}

  if(wc&&!otaBusy){
    bool dueFirst=autoUpdate&&!otaInitialChecked&&(millis()-lastOtaCheck>=OTA_FIRST_CHECK_MS);
    bool dueRegular=autoUpdate&&otaInitialChecked&&(millis()-lastOtaCheck>=OTA_INTERVAL_MS);
    if(otaCheckRequested||dueFirst||dueRegular){
      bool manual=otaCheckRequested;
      otaCheckRequested=false;otaInitialChecked=true;lastOtaCheck=millis();
      checkForUpdate(manual||autoUpdate);
    }
  }
  delay(2);
}
