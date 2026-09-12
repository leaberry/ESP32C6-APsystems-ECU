#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

struct HaEnergyEntry { char serial[13]={}; uint64_t committed=0,pending=0; double fraction=0; };
struct HaEnergyMeter {
  HaEnergyEntry entries[64]; uint64_t aggregate=0;
  int find(const char *serial,bool create=false) {
    for(int i=0;i<64;++i) if(!strcmp(entries[i].serial,serial)) return i;
    if(create) for(int i=0;i<64;++i) if(!entries[i].serial[0]) {
      if(strlen(serial)!=12) return -1;
      memcpy(entries[i].serial,serial,13);return i;
    }
    return -1;
  }
  bool credit(const char *serial,double wh) {
    if(!isfinite(wh)||wh<=0||wh>10000) return true;
    int slot=find(serial,true);if(slot<0)return false;
    auto &entry=entries[slot];entry.fraction+=wh*1000;
    uint64_t whole=(uint64_t)floor(entry.fraction);entry.fraction-=whole;
    if(UINT64_MAX-entry.pending<whole)return false;
    entry.pending+=whole;return true;
  }

};
inline bool haHexId(const char *s) {
  if(strlen(s)!=12)return false;
  for(int i=0;i<12;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='A'&&s[i]<='F')||(s[i]>='a'&&s[i]<='f')))return false;
  return true;
}
inline bool haPrefixValid(const char *s) {
  size_t n=strlen(s);if(!n||n>48)return false;
  for(size_t i=0;i<n;++i)if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='A'&&s[i]<='Z')||
    (s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'||(s[i]=='/'&&i&&i+1<n&&s[i-1]!='/')))return false;
  return true;
}
