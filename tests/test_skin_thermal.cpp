// Exercise the decision function used by AutoModeCtl, not a test-side copy.
#include "ThermalDecision.hpp"
#include <cstdio>
static int veto_temp=60, skin_temp=47, junction_hard=100; static bool gate=true;
int thermalDecisionC(int tempC,int skinC){
    return kin::thermalDecisionC(tempC,skinC,gate,veto_temp,skin_temp,junction_hard);
}
int main(){
  struct C{const char*n;int tj,sk,want;bool vetoWant;};
  C cs[]={
    {"无皮肤传感器(skin=-1)→回退结温90",90,-1,90,true},
    {"虚拟皮肤读0(sentinel)→回退结温90,仍降频",90,0,90,true},
    {"皮肤超范围75→回退结温90",90,75,90,true},
    {"低温环境外壳18℃(真实读数)→td=31,不误否决",90,18,31,false},
    {"外壳凉40+结温90→td53<veto,不误否决",90,40,53,false},
    {"外壳热50+结温90→td63>=veto,正常否决",90,50,63,true},
    {"结温失控100+外壳凉40→硬网强制否决",100,40,60,true},
  };
  int pass=0,tot=0;
  for(auto&c:cs){int td=thermalDecisionC(c.tj,c.sk);bool veto=(td>=0&&td>=veto_temp);
    bool ok=(td==c.want)&&(veto==c.vetoWant);tot++;if(ok)pass++;
    printf("%s td=%d 否决=%d  %s\n",ok?"PASS":"FAIL",td,veto,c.n);}
  gate=false; int g=thermalDecisionC(90,40);
  bool gok=(g==90); tot++; if(gok)pass++;
  printf("%s gate关→逐字结温90 (td=%d)\n",gok?"PASS":"FAIL",g);
  printf("=== %d/%d ===\n",pass,tot); return pass==tot?0:1;
}
