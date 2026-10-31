#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include <poll.h>
#include "ecmdata.h"
#include "sockets.h"
#include "msg-cccam.h"
#include "config.h"
#include "dcw.h"
#include "dcwfilter.h"
int main(void) {
 struct cardserver_data cs, cycle_cs; struct config_data defaults;
 int reason=0, fail=0, used=0;
 int global_checksum, global_repeat, global_cycle, global_pattern;
 uint8_t bad[16]={1,2,3,7,4,5,6,15,7,8,9,24,10,11,12,33};
 memset(&cs,0,sizeof cs); cs.card.caid=0x1884; cs.option.skipcwc=1; cs.option.cw_checksum_exempt_active=1; cs.option.cw_checksum_exempt_provider=0; cs.option.cw_checksum_exempt_sid=0x1136;
#define CHECK(n, cond) do { if(!(cond)){printf("FAIL %s reason %d\n",n,reason);fail++;} else printf("PASS %s\n",n); } while(0)
 CHECK("trace used",acceptDCW_profile_ecm_trace_reason(bad,&cs,0x1884,0,0x1136,&reason,&used) && used);
 CHECK("match",acceptDCW_profile_ecm_reason(bad,&cs,0x1884,0,0x1136,&reason));
 CHECK("wrong caid",!acceptDCW_profile_ecm_reason(bad,&cs,0x1885,0,0x1136,&reason) && reason==DCW_INVALID_CHECKSUM);
 CHECK("wrong sid",!acceptDCW_profile_ecm_reason(bad,&cs,0x1884,0,0x1137,&reason) && reason==DCW_INVALID_CHECKSUM);
 CHECK("wrong provider",!acceptDCW_profile_ecm_reason(bad,&cs,0x1884,1,0x1136,&reason) && reason==DCW_INVALID_CHECKSUM);
 CHECK("no provider API",!acceptDCW_profile_ca_sid_reason(bad,&cs,0x1884,0x1136,&reason) && reason==DCW_INVALID_CHECKSUM);
 CHECK("null profile",!acceptDCW_profile_ecm_reason(bad,NULL,0x1884,0,0x1136,&reason) && reason==DCW_INVALID_CHECKSUM);
 cs.option.skipcwc=0; cs.option.dcwfilter_checksum=DCWFILTER_OFF;
 CHECK("trace no-op",acceptDCW_profile_ecm_trace_reason(bad,&cs,0x1884,0,0x1136,&reason,&used) && !used);
 cs.option.skipcwc=1; cs.option.cw_checksum_exempt_active=0;
 CHECK("off",!acceptDCW_profile_ecm_reason(bad,&cs,0x1884,0,0x1136,&reason) && reason==DCW_INVALID_CHECKSUM);
 dcw_filter_checksum=1; dcw_filter_repeat=0; dcw_filter_cycle=0; dcw_filter_pattern=1;
 dcw_filter_globals_snapshot(&global_checksum,&global_repeat,&global_cycle,&global_pattern);
 CHECK("global gate snapshot",global_checksum==1 && global_repeat==0 && global_cycle==0 && global_pattern==1);
 memset(&cycle_cs,0,sizeof cycle_cs);
 cycle_cs.option.dcwfilter_cycle=DCWFILTER_INHERIT;
 CHECK("cycle inherits global off",!dcw_profile_cycle_enabled(&cycle_cs));
 dcw_filter_cycle=1;
 CHECK("cycle inherits global on",dcw_profile_cycle_enabled(&cycle_cs));
 cycle_cs.option.dcwfilter_cycle=DCWFILTER_OFF;
 CHECK("profile cycle off overrides global",!dcw_profile_cycle_enabled(&cycle_cs));
 cycle_cs.option.dcwfilter_cycle=DCWFILTER_ON;
 CHECK("profile cycle on overrides global",dcw_profile_cycle_enabled(&cycle_cs));
 dcw_filter_repeat=0; dcw_filter_cycle=1; dcw_filter_pattern=1;
 init_config(&defaults);
 CHECK("config defaults staged",defaults.dcw_filters.checksum==1 && defaults.dcw_filters.repeat==1 && defaults.dcw_filters.cycle==0 && defaults.dcw_filters.pattern==0);
 dcw_filter_globals_snapshot(&global_checksum,&global_repeat,&global_cycle,&global_pattern);
 CHECK("parsing leaves active gates untouched",global_checksum==1 && global_repeat==0 && global_cycle==1 && global_pattern==1);
 dcw_filter_globals_publish(&defaults);
 dcw_filter_globals_snapshot(&global_checksum,&global_repeat,&global_cycle,&global_pattern);
 CHECK("config commit publishes gates together",global_checksum==1 && global_repeat==1 && global_cycle==0 && global_pattern==0);
 return fail!=0;
}
